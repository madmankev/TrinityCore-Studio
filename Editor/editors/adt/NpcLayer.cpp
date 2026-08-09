// NpcLayer — see NpcLayer.h.

#include "editors/adt/NpcLayer.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "clientdata/ClientData.h"
#include "clientdata/DbcStore.h"
#include "db/IDatabase.h"
#include "model/M2Loader.h"
#include "model/ModelUploadBuild.h"
#include "schema/Waypoint.h"
#include "viewer/Picking.h"

namespace we
{
namespace
{
constexpr int   kModelsPerFrame = 2;      // budget: heavy M2 parse/decode per new model
constexpr int   kMaxModels      = 512;    // memory guard on distinct uploaded creature models
constexpr float kBaseWalkSpeed  = 2.5f;   // yards/sec at speed_walk == 1.0
constexpr float kMaxStepMs      = 100.0f; // clamp dt so a frame hitch doesn't teleport NPCs

// Model-facing calibration: added to the simulated heading before the yaw-about-Z rotation.
// TrinityCore orientation and the viewer's world axes match (facing = (cos o, sin o) in X/Y),
// so this only corrects the M2's own local forward axis. CALIBRATION CHECKPOINT: verify
// against a known spawn and adjust (likely 0, +/-pi/2, or pi) — mirrors AdtLoader's yaw offset.
constexpr float kHeadingOffset = 0.0f;

// WoW AnimationData.dbc ids used here.
constexpr uint16_t kAnimStand = 0;
constexpr uint16_t kAnimWalk  = 4;
constexpr uint16_t kAnimRun   = 5;

// Cheap per-NPC LCG (deterministic per guid) — Math/rand-free, fine for wander targets.
float NextRand(uint32_t& s)
{
    s = s * 1664525u + 1013904223u;
    return static_cast<float>(s >> 8) * (1.0f / 16777216.0f);
}

std::string LowerStr(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

void NpcLayer::Init(ClientData* cd, DbcStore* dbc, IRenderer* renderer)
{
    cd_ = cd;
    dbc_ = dbc;
    renderer_ = renderer;
    if (cd_ && dbc_)
        dresser_ = std::make_unique<ModelDresser>(*cd_, *dbc_);
}

void NpcLayer::SetSpawns(std::vector<MapSpawn> spawns)
{
    npcs_.clear();
    npcs_.reserve(spawns.size());
    for (const MapSpawn& s : spawns)
    {
        Npc n;
        n.spawn = s;
        n.pos = glm::vec3(s.x, s.y, s.z);
        n.heading = s.o;
        n.rng = s.guid ? s.guid : (s.entry * 2654435761u + 1u);
        npcs_.push_back(std::move(n));
    }
}

void NpcLayer::AddSpawn(const MapSpawn& s)
{
    Npc n;
    n.spawn = s;
    n.pos = glm::vec3(s.x, s.y, s.z);
    n.heading = s.o;
    n.rng = s.guid ? s.guid : (s.entry * 2654435761u + 1u);
    npcs_.push_back(std::move(n));
}

bool NpcLayer::RemoveSpawn(uint32_t guid)
{
    for (size_t i = 0; i < npcs_.size(); ++i)
        if (npcs_[i].spawn.guid == guid)
        {
            npcs_.erase(npcs_.begin() + i);
            return true;
        }
    return false;
}

void NpcLayer::SnapshotSpawns(std::vector<MapSpawn>& out) const
{
    out.clear();
    out.reserve(npcs_.size());
    for (const Npc& n : npcs_)
        out.push_back(n.spawn);
}

void NpcLayer::Clear()
{
    if (renderer_)
    {
        for (auto& kv : models_)
            if (kv.second.handle)
                renderer_->DestroyModel(kv.second.handle);
        for (auto& kv : heldModels_)
            if (kv.second.handle)
                renderer_->DestroyModel(kv.second.handle);
    }
    models_.clear();
    failedDisplays_.clear();
    directModelFallbacks_.clear();
    heldModels_.clear();
    failedHeld_.clear();
    npcs_.clear();
    near_.clear();
    displays_.clear();
    modelPaths_.clear();
    displayMapsLoaded_ = false;
    drawn_ = 0;
    cappedLastFrame_ = false;
}

void NpcLayer::EnsureDisplayMaps()
{
    if (displayMapsLoaded_ || !cd_ || !dbc_ || !cd_->IsOpen())
        return;
    displays_ = dbc_->LoadCreatureDisplays(*cd_);
    modelPaths_ = dbc_->LoadCreatureModelPaths(*cd_);
    displayExtras_ = dbc_->LoadCreatureDisplayExtras(*cd_);
    displayMapsLoaded_ = true;
}

int NpcLayer::FindSequence(const m2::M2Model& m, uint16_t animationId)
{
    for (size_t i = 0; i < m.sequences.size(); ++i)
        if (m.sequences[i].animationId == animationId)
            return static_cast<int>(i);
    return -1;
}

void NpcLayer::DressCharacterNpc(const std::string& path, const DbcStore::CreatureDisplayExtra& extra,
                                 m2::M2Model& model, BlpImage& bodyOut, int& bodySlotOut,
                                 std::vector<uint8_t>& visibleOut)
{
    bodySlotOut = -1;
    if (!dresser_)
        return;

    CharCustomize cc;
    cc.skinColor = extra.skin;
    cc.faceVariation = extra.face;
    cc.hairVariation = extra.hairStyle;
    cc.hairColor = extra.hairColor;
    cc.facialVariation = extra.facialHair;

    // The 11 NPCItemDisplay columns are ItemDisplayInfo ids for the worn armour, in equipment-slot
    // order. Only the composite/geoset slots (attachment id 0) contribute to the body atlas + geoset
    // selection; head/shoulder are attached models handled in the weapon/attachment phase.
    static const EquipSlot kNpcSlots[11] = {
        EquipSlot::Head, EquipSlot::Shoulder, EquipSlot::Shirt, EquipSlot::Chest, EquipSlot::Waist,
        EquipSlot::Legs, EquipSlot::Feet, EquipSlot::Wrist, EquipSlot::Hands, EquipSlot::Tabard,
        EquipSlot::Back,
    };
    std::vector<EquippedItem> equipped;
    for (int i = 0; i < 11; ++i)
    {
        if (!extra.npcItemDisplay[i] || EquipSlotAttachment(kNpcSlots[i]) != 0)
            continue;
        DbcStore::ItemDisplay it;
        if (dresser_->ItemDisplayById(extra.npcItemDisplay[i], it))
            equipped.push_back({kNpcSlots[i], it});
    }

    // Set the body (type 1) + hair (type 6) texture paths so the slots read as resolved; the actual
    // body pixels come from the composite grafted into the type-1 slot at upload (bodyOut).
    const std::string body = dresser_->CharacterBodyTexture(extra.race, extra.sex, cc);
    const std::string hair = dresser_->CharacterHairTexture(extra.race, extra.sex, cc);
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
    {
        uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
        if (t == 1 && !body.empty()) model.texturePaths[i] = body;
        if (t == 6 && !hair.empty()) model.texturePaths[i] = hair;
        if (t == 1) bodySlotOut = (int)i;
    }
    bodyOut = dresser_->ComposeCharacterBody(extra.race, extra.sex, cc, equipped);
    dresser_->CharacterGeosets(extra.race, extra.sex, cc, model, visibleOut, equipped);
    // Fill any leftover runtime slots (e.g. a type-8 extra) with defaults so they aren't white.
    dresser_->ResolveDefaultTextures(path, model);
}

NpcLayer::NpcModel* NpcLayer::EnsureModel(uint32_t displayId)
{
    if (displayId == 0)
        return nullptr;
    auto it = models_.find(displayId);
    if (it != models_.end())
        return &it->second;
    if (failedDisplays_.count(displayId))
        return nullptr;
    if (modelBudget_ <= 0 || static_cast<int>(models_.size()) >= kMaxModels)
        return nullptr;   // try again a later frame (budget) — not a failure

    // Normal path: CreatureDisplayInfo display id -> CreatureModelData model id -> .m2 path.
    // A few custom/legacy database packs use a CreatureModelData id directly in modelid1 instead;
    // fall back to that direct lookup so those NPCs remain visible instead of silently disappearing.
    const DbcStore::CreatureDisplay* display = nullptr;
    uint32_t modelId = displayId;
    auto dit = displays_.find(displayId);
    if (dit != displays_.end())
    {
        display = &dit->second;
        modelId = display->modelId;
    }
    else
        directModelFallbacks_.insert(displayId);

    auto mp = modelPaths_.find(modelId);
    if (mp == modelPaths_.end())
    {
        failedDisplays_.insert(displayId);
        return nullptr;
    }
    const std::string& path = mp->second;

    m2::M2Model model;
    std::string err;
    if (!m2::Load(*cd_, path, model, &err))
    {
        failedDisplays_.insert(displayId);
        --modelBudget_;
        return nullptr;
    }
    // Dress the runtime texture slots so nothing renders flat white. A character-model NPC (one with
    // an ExtendedDisplayInfoID) is composited from CreatureDisplayInfoExtra like the Model Viewer;
    // any other creature just gets its monster-skins + a default fallback.
    BlpImage bodyComposite;          // composited character body atlas (grafted into the type-1 slot)
    int bodySlot = -1;               // index of the type-1 body texture slot (-1 = not a character)
    std::vector<uint8_t> geosetVisible;   // per-submesh visibility for hair/facial/armour geosets
    const DbcStore::CreatureDisplayExtra* extra = nullptr;
    if (display && display->extendedDisplayId && ClassifyModel(path) == ModelClass::Character)
    {
        auto eit = displayExtras_.find(display->extendedDisplayId);
        if (eit != displayExtras_.end())
            extra = &eit->second;
    }
    if (dresser_ && extra)
    {
        DressCharacterNpc(path, *extra, model, bodyComposite, bodySlot, geosetVisible);
    }
    else if (dresser_)
    {
        const std::string blankSkins[3] = {};
        dresser_->ApplyCreatureSkins(path, display ? display->skins : blankSkins, model);
        dresser_->ResolveDefaultTextures(path, model);
    }

    ModelUpload up = m2::BuildUpload(*cd_, model);
    // Scene NPCs are placed by folding the world transform into their bone palette. Some static
    // creature M2s (and a number of custom models) carry zero bones or zero vertex weights; unlike
    // ADT instancing they have no separate instance matrix, so without this fallback they render at
    // the world origin and appear missing. Give every unweighted vertex a root bone and retain a
    // one-bone palette for such models.
    if (up.boneCount == 0)
        up.boneCount = 1;
    for (ModelVertexGpu& v : up.vertices)
    {
        const float weight = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
        if (weight <= 1e-6f)
        {
            v.boneIndices[0] = 0;
            v.boneWeights[0] = 1.0f;
            v.boneWeights[1] = v.boneWeights[2] = v.boneWeights[3] = 0.0f;
        }
    }
    // Character body: replace the decoded base-skin slot with the composited body (base skin +
    // face + underwear + armour regions), exactly as ModelViewerModule::Rebuild does.
    if (bodySlot >= 0 && bodyComposite.valid() && bodySlot < (int)up.textures.size())
    {
        up.textures[bodySlot].rgba = bodyComposite.rgba;
        up.textures[bodySlot].w = bodyComposite.width;
        up.textures[bodySlot].h = bodyComposite.height;
    }
    glm::vec3 aabbMin(1e30f), aabbMax(-1e30f);
    for (const ModelVertexGpu& v : up.vertices)
    {
        aabbMin = glm::min(aabbMin, glm::vec3(v.pos[0], v.pos[1], v.pos[2]));
        aabbMax = glm::max(aabbMax, glm::vec3(v.pos[0], v.pos[1], v.pos[2]));
    }
    ModelHandle h = renderer_->CreateModel(up);
    if (!h)
    {
        failedDisplays_.insert(displayId);
        --modelBudget_;
        return nullptr;
    }
    // A fresh upload defaults to all-visible; apply the character's geoset selection (hair/facial/
    // armour groups + helmet hiding). The mask is per-upload-handle, which is correct here because
    // every spawn of this displayId shares the handle AND the same appearance.
    if (!geosetVisible.empty())
        renderer_->SetSubmeshVisibility(h, geosetVisible.data(), (int)geosetVisible.size());

    NpcModel nm;
    nm.handle = h;
    nm.m2 = std::make_shared<m2::M2Model>(std::move(model));
    nm.displayScale = (display && std::isfinite(display->scale) && display->scale > 0.0f)
                          ? display->scale
                          : 1.0f;
    nm.boundsCenter = nm.m2->boundsCenter;
    nm.boundsRadius = nm.m2->boundsRadius;
    nm.boundsMin = aabbMin;
    nm.boundsMax = aabbMax;
    nm.animator.SetModel(nm.m2.get(), cd_, path);
    int stand = FindSequence(*nm.m2, kAnimStand);
    nm.standSeq = stand >= 0 ? stand : 0;
    int walk = FindSequence(*nm.m2, kAnimWalk);
    if (walk < 0)
        walk = FindSequence(*nm.m2, kAnimRun);
    nm.walkSeq = walk >= 0 ? walk : nm.standSeq;
    --modelBudget_;

    auto res = models_.emplace(displayId, std::move(nm));
    return &res.first->second;
}

void NpcLayer::Simulate(Npc& n, float dtMs, IDatabase* db)
{
    dtMs = std::min(dtMs, kMaxStepMs);
    n.animTime += dtMs;

    // Paused (waypoint delay / wander rest): stand still.
    if (n.pauseMs > 0.0f)
    {
        n.pauseMs -= dtMs;
        n.moving = false;
        return;
    }

    const float walkStep = kBaseWalkSpeed * (n.spawn.speedWalk > 0.0f ? n.spawn.speedWalk : 1.0f) *
                           dtMs / 1000.0f;

    // Move toward `tgt` (XY); returns true on arrival. Faces the movement direction. The caller
    // supplies the step so a waypoint's move_type=Run previews at the template run multiplier.
    auto moveToward = [&](const glm::vec3& tgt, float step) -> bool
    {
        glm::vec2 cur(n.pos.x, n.pos.y);
        glm::vec2 to(tgt.x - cur.x, tgt.y - cur.y);
        float dist = glm::length(to);
        if (dist <= step || dist < 0.05f)
        {
            n.pos = tgt;
            n.moving = false;
            return true;
        }
        glm::vec2 dir = to / dist;
        n.pos.x += dir.x * step;
        n.pos.y += dir.y * step;
        n.pos.z = tgt.z;   // no terrain sampling; ride the target's Z
        n.heading = std::atan2(dir.y, dir.x);
        n.moving = true;
        return false;
    };

    const uint8_t mt = n.spawn.movementType;

    if (mt == 1 && n.spawn.wanderDistance > 0.5f)   // random wander around the spawn
    {
        if (!n.hasTarget)
        {
            float ang = NextRand(n.rng) * 6.2831853f;
            float r = n.spawn.wanderDistance * std::sqrt(NextRand(n.rng));
            n.target = glm::vec3(n.spawn.x + std::cos(ang) * r, n.spawn.y + std::sin(ang) * r, n.spawn.z);
            n.hasTarget = true;
        }
        if (moveToward(n.target, walkStep))
        {
            n.hasTarget = false;
            n.pauseMs = 1500.0f + NextRand(n.rng) * 2500.0f;   // rest 1.5–4s
        }
        return;
    }

    if (mt == 2)   // waypoint path
    {
        if (!n.pathTried)
        {
            if (!db)
            {
                n.moving = false;
                return;   // retry once a DB is available
            }
            n.pathTried = true;
            if (n.spawn.pathId != 0)
            {
                MapSpawnRepository repo;
                WaypointPath wp;
                repo.LoadWaypointPath(*db, n.spawn.pathId, wp);
                n.path.reserve(wp.points.size());
                n.pathDelay.reserve(wp.points.size());
                n.pathOrientation.reserve(wp.points.size());
                n.pathMoveType.reserve(wp.points.size());
                for (const WaypointPoint& p : wp.points)
                {
                    n.path.emplace_back(p.x, p.y, p.z);
                    n.pathDelay.push_back(static_cast<float>(p.delay));
                    n.pathOrientation.push_back(p.o);
                    n.pathMoveType.push_back(p.moveType);
                }
            }
        }
        if (n.path.size() < 2)   // no usable path -> stand at home
        {
            n.moving = false;
            n.pos = glm::vec3(n.spawn.x, n.spawn.y, n.spawn.z);
            return;
        }
        if (n.pathIdx >= static_cast<int>(n.path.size()))
            n.pathIdx = 0;
        const uint8_t pointMove = n.pathIdx < static_cast<int>(n.pathMoveType.size())
                                      ? n.pathMoveType[n.pathIdx]
                                      : 0;
        const float speedMultiplier = (pointMove == 1 && n.spawn.speedRun > 0.0f)
                                          ? n.spawn.speedRun
                                          : (n.spawn.speedWalk > 0.0f ? n.spawn.speedWalk : 1.0f);
        const float pathStep = kBaseWalkSpeed * speedMultiplier * dtMs / 1000.0f;
        if (moveToward(n.path[n.pathIdx], pathStep))
        {
            n.pauseMs = n.pathDelay[n.pathIdx];
            // In TrinityCore a zero waypoint orientation means "do not force facing". A non-zero
            // orientation is applied once the actor reaches the point, which also makes the World
            // Editor preview reflect orientation edits instead of only storing them.
            if (n.pathIdx < static_cast<int>(n.pathOrientation.size()) &&
                std::fabs(n.pathOrientation[n.pathIdx]) > 1e-6f)
                n.heading = n.pathOrientation[n.pathIdx];
            n.pathIdx = (n.pathIdx + 1) % static_cast<int>(n.path.size());
        }
        return;
    }

    // Idle (mt 0) or anything unsupported: stand at the home position.
    n.moving = false;
    n.pos = glm::vec3(n.spawn.x, n.spawn.y, n.spawn.z);
    n.heading = n.spawn.o;
}

const NpcLayer::HeldModel* NpcLayer::EnsureHeldModel(const std::string& path, const std::string& objectSkin)
{
    const std::string key = LowerStr(path);
    auto it = heldModels_.find(key);
    if (it != heldModels_.end())
        return &it->second;
    if (failedHeld_.count(key))
        return nullptr;

    m2::M2Model model;
    if (!m2::Load(*cd_, path, model, nullptr))
    {
        failedHeld_.insert(key);
        return nullptr;
    }
    if (dresser_)
        dresser_->ApplyItemObjectSkin(path, objectSkin, model);   // type-2 object skin (else white)
    ModelUpload up = m2::BuildUpload(*cd_, model);
    if (up.boneCount == 0)
        up.boneCount = 1;
    for (ModelVertexGpu& v : up.vertices)
    {
        const float weight = v.boneWeights[0] + v.boneWeights[1] + v.boneWeights[2] + v.boneWeights[3];
        if (weight <= 1e-6f)
        {
            v.boneIndices[0] = 0;
            v.boneWeights[0] = 1.0f;
            v.boneWeights[1] = v.boneWeights[2] = v.boneWeights[3] = 0.0f;
        }
    }
    ModelHandle h = renderer_->CreateModel(up);
    if (!h)
    {
        failedHeld_.insert(key);
        return nullptr;
    }
    HeldModel hm;
    hm.handle = h;
    m2::M2Animator anim;
    anim.SetModel(&model, cd_, path);
    anim.Evaluate(0, 0.0f, glm::mat4(1.0f), hm.localBones);   // static bind pose (weapons don't billboard)
    if (hm.localBones.empty())
        hm.localBones.assign(1, glm::mat4(1.0f));
    auto res = heldModels_.emplace(key, std::move(hm));
    return &res.first->second;
}

void NpcLayer::EnsureAttachments(Npc& n, const NpcModel& /*parent*/, uint32_t parentDisplayId)
{
    if (!dresser_)
        return;
    if (n.attachTried && n.attachmentParentDisplayId == parentDisplayId)
        return;
    // A game-event display override can replace the parent M2 but leave this spawn's equipment
    // unchanged. Re-resolve attachment IDs against the new model so held weapons do not remain
    // tied to bones from the old body.
    n.attachTried = true;
    n.attachmentParentDisplayId = parentDisplayId;
    n.attachments.clear();
    // spawn.weaponDisplay[0..2] = main / off / ranged ItemDisplayInfo ids.
    static const EquipSlot kSlots[3] = { EquipSlot::MainHand, EquipSlot::OffHand, EquipSlot::Ranged };
    auto componentFolder = [](EquipSlot s) -> const char* {
        return s == EquipSlot::OffHand ? "Shield" : "Weapon";   // off-hand is often a shield
    };
    for (int i = 0; i < 3; ++i)
    {
        const uint32_t displayId = n.spawn.weaponDisplay[i];
        if (displayId == 0)
            continue;
        const uint32_t attachId = EquipSlotAttachment(kSlots[i]);
        if (attachId == 0)
            continue;
        DbcStore::ItemDisplay it;
        if (!dresser_->ItemDisplayById(displayId, it) || it.modelName[0].empty())
            continue;
        // Try the slot's component folder, then fall back to Weapon (some client data is inconsistent).
        std::string path = std::string("Item\\ObjectComponents\\") + componentFolder(kSlots[i]) + "\\" + it.modelName[0];
        const HeldModel* hm = EnsureHeldModel(path, it.modelTexture[0]);
        if (!hm)
        {
            path = "Item\\ObjectComponents\\Weapon\\" + it.modelName[0];
            hm = EnsureHeldModel(path, it.modelTexture[0]);
            if (!hm)
                continue;
        }
        NpcAttach at;
        at.attachId = attachId;
        at.model = hm;
        n.attachments.push_back(std::move(at));
    }
}

void NpcLayer::Build(const glm::vec3& focus, const glm::vec3& origin, const glm::mat4& view,
                     float dtMs, IDatabase* db, std::vector<SceneInstanceGpu>& outScene,
                     int maxDraw, float cullDist, const SpawnFilter& filter)
{
    drawn_ = 0;
    cappedLastFrame_ = false;
    modelBudget_ = kModelsPerFrame;
    if (!renderer_ || npcs_.empty())
        return;
    EnsureDisplayMaps();
    if (!displayMapsLoaded_)
        return;

    const glm::vec3 worldFocus = focus + origin;
    const float cull2 = cullDist * cullDist;

    // Simulate every NPC within the cull radius; collect them for a nearest-first draw pass.
    near_.clear();
    for (int i = 0; i < static_cast<int>(npcs_.size()); ++i)
    {
        Npc& n = npcs_[i];
        if (!filter.Visible(n.spawn.phaseMask, n.spawn.spawnMask, n.spawn.eventEntry,
                            n.spawn.poolHidden, n.spawn.groupManual))
            continue;   // filtered out (phase / difficulty / event / pool / spawn group)
        glm::vec3 d = n.pos - worldFocus;
        float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (d2 > cull2)
            continue;
        Simulate(n, dtMs, db);
        near_.emplace_back(d2, i);
    }
    std::sort(near_.begin(), near_.end(),
              [](const std::pair<float, int>& a, const std::pair<float, int>& b)
              { return a.first < b.first; });
    if (static_cast<int>(near_.size()) > maxDraw)
        cappedLastFrame_ = true;

    const int limit = std::min(static_cast<int>(near_.size()), maxDraw);
    for (int k = 0; k < limit; ++k)
    {
        Npc& n = npcs_[near_[k].second];
        // Resolve the display the server would currently send for this spawn. The base value comes
        // from creature.modelid/displayid or its template model row; a selected game event may
        // temporarily replace it through game_event_model_equip.
        const ResolvedCreatureDisplay display = n.spawn.ResolveDisplay(filter);
        NpcModel* m = EnsureModel(display.displayId);
        if (!m)
            continue;   // pending model creation (budget) or unresolved display

        int seq = n.moving ? m->walkSeq : m->standSeq;
        if (seq < 0)
            seq = 0;
        float dur = static_cast<float>(m->animator.Duration(seq));
        float t = dur > 0.0f ? std::fmod(n.animTime, dur) : 0.0f;
        m->animator.Evaluate(seq, t, view, n.palette);   // M2-local palette
        // Static M2s may have no bones, but their upload has a synthesized root bone above so
        // the world placement still reaches the vertex shader instead of leaving the mesh at origin.
        if (n.palette.empty())
            n.palette.assign(1, glm::mat4(1.0f));

        // Fold the world transform into the palette (world = local + origin; Z not offset).
        const glm::vec3 local(n.pos.x - origin.x, n.pos.y - origin.y, n.pos.z);
        glm::mat4 X(1.0f);
        X = glm::translate(X, local);
        X = glm::rotate(X, n.heading + kHeadingOffset, glm::vec3(0.0f, 0.0f, 1.0f));
        // Worldserver template scale, AzerothCore's selected DisplayScale, and the client's own
        // CreatureDisplayInfo scale all multiply. Applying all three is what keeps an NPC rendered
        // from a server display id at the same physical size as it has in-game.
        const float templateScale = n.spawn.scale > 0.0f ? n.spawn.scale : 1.0f;
        const float serverDisplayScale = display.serverScale > 0.0f ? display.serverScale : 1.0f;
        const float visualScale = templateScale * serverDisplayScale * m->displayScale;
        if (visualScale != 1.0f)
            X = glm::scale(X, glm::vec3(visualScale));
        for (glm::mat4& b : n.palette)
            b = X * b;

        SceneInstanceGpu si{};
        si.handle = m->handle;
        si.boneMatrices = n.palette.empty() ? nullptr : reinterpret_cast<const float*>(n.palette.data());
        si.boneCount = static_cast<int>(n.palette.size());
        si.worldOrigin[0] = local.x;
        si.worldOrigin[1] = local.y;
        si.worldOrigin[2] = local.z;
        if (n.spawn.guid == highlightGuid_ && highlightColor_.a > 0.0f)
        {
            si.highlight[0] = highlightColor_.r; si.highlight[1] = highlightColor_.g;
            si.highlight[2] = highlightColor_.b; si.highlight[3] = highlightColor_.a;
        }
        if (n.spawn.guid == outlineGuid_ && outlineColor_.a > 0.0f)
        {
            si.outline[0] = outlineColor_.r; si.outline[1] = outlineColor_.g;
            si.outline[2] = outlineColor_.b; si.outline[3] = outlineColor_.a;
        }
        outScene.push_back(si);
        ++drawn_;

        // Held weapons: fold each item's bind pose by the parent's (already world-folded) attachment
        // bone and append as its own instance. n.palette is world-space here, so n.palette[att.bone]
        // is the attachment bone's world matrix.
        if (modelBudget_ > 0)
            EnsureAttachments(n, *m, display.displayId);
        for (NpcAttach& at : n.attachments)
        {
            if (!at.model || !at.model->handle)
                continue;
            glm::mat4 attWorld(1.0f);
            bool found = false;
            for (const m2::M2Attachment& att : m->m2->attachments)
                if (att.id == at.attachId && att.bone < n.palette.size())
                {
                    attWorld = n.palette[att.bone] *
                               glm::translate(glm::mat4(1.0f), glm::vec3(att.pos[0], att.pos[1], att.pos[2]));
                    found = true;
                    break;
                }
            if (!found)
                continue;   // parent lacks this attachment point — don't float the weapon at origin
            const std::vector<glm::mat4>& lb = at.model->localBones;
            at.palette.resize(lb.size());
            for (size_t j = 0; j < lb.size(); ++j)
                at.palette[j] = attWorld * lb[j];

            SceneInstanceGpu wi{};
            wi.handle = at.model->handle;
            wi.boneMatrices = at.palette.empty() ? nullptr : reinterpret_cast<const float*>(at.palette.data());
            wi.boneCount = static_cast<int>(at.palette.size());
            wi.worldOrigin[0] = local.x;
            wi.worldOrigin[1] = local.y;
            wi.worldOrigin[2] = local.z;
            outScene.push_back(wi);
            ++drawn_;
        }
    }
}

uint32_t NpcLayer::Pick(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& focus,
                        const glm::vec3& origin, float cullDist, const SpawnFilter& filter,
                        float& outDist)
{
    const glm::vec3 worldFocus = focus + origin;
    const float cull2 = cullDist * cullDist;
    uint32_t best = 0;
    float bestT = 1e30f;
    for (Npc& n : npcs_)
    {
        if (!filter.Visible(n.spawn.phaseMask, n.spawn.spawnMask, n.spawn.eventEntry,
                            n.spawn.poolHidden, n.spawn.groupManual))
            continue;
        const glm::vec3 dd = n.pos - worldFocus;
        if (dd.x * dd.x + dd.y * dd.y + dd.z * dd.z > cull2)
            continue;
        // Pick against the model where it is drawn (current simulated position), tight ray-vs-OBB.
        // Use the same event-aware display and all server/client display scales as Build so a
        // selected override never has a visibly offset or undersized pick volume.
        const ResolvedCreatureDisplay display = n.spawn.ResolveDisplay(filter);
        auto it = models_.find(display.displayId);
        const float templateScale = n.spawn.scale > 0.0f ? n.spawn.scale : 1.0f;
        const float serverScale = display.serverScale > 0.0f ? display.serverScale : 1.0f;
        const float clientScale = it != models_.end() ? it->second.displayScale : 1.0f;
        const float scale = templateScale * serverScale * clientScale;
        const glm::vec3 local(n.pos.x - origin.x, n.pos.y - origin.y, n.pos.z);
        glm::mat4 X(1.0f);
        X = glm::translate(X, local);
        X = glm::rotate(X, n.heading + kHeadingOffset, glm::vec3(0.0f, 0.0f, 1.0f));
        X = glm::scale(X, glm::vec3(scale));
        const glm::vec3 bmin = (it != models_.end()) ? it->second.boundsMin : glm::vec3(-0.5f);
        const glm::vec3 bmax = (it != models_.end()) ? it->second.boundsMax : glm::vec3(0.5f);
        const float t = RayObb(ro, rd, X, bmin, bmax);
        if (t >= 0.0f && t < bestT) { bestT = t; best = n.spawn.guid; }
    }
    outDist = best ? bestT : -1.0f;
    return best;
}

const MapSpawn* NpcLayer::FindSpawn(uint32_t guid) const
{
    for (const Npc& n : npcs_)
        if (n.spawn.guid == guid)
            return &n.spawn;
    return nullptr;
}

bool NpcLayer::HomeMatrix(uint32_t guid, const glm::vec3& origin, glm::mat4& out) const
{
    for (const Npc& n : npcs_)
    {
        if (n.spawn.guid != guid)
            continue;
        // Gizmo transforms use the persistent/home display (events do not alter a spawn's saved
        // placement). Include the server template-model scale and the known client display scale
        // so the visual transform remains consistent with a normally rendered NPC.
        const ResolvedCreatureDisplay display = n.spawn.ResolveDisplay(SpawnFilter{});
        const auto model = models_.find(display.displayId);
        const float templateScale = n.spawn.scale > 0.0f ? n.spawn.scale : 1.0f;
        const float serverScale = display.serverScale > 0.0f ? display.serverScale : 1.0f;
        const float clientScale = model != models_.end() ? model->second.displayScale : 1.0f;
        const float scale = templateScale * serverScale * clientScale;
        const glm::vec3 local(n.spawn.x - origin.x, n.spawn.y - origin.y, n.spawn.z);
        glm::mat4 X(1.0f);
        X = glm::translate(X, local);
        X = glm::rotate(X, n.spawn.o, glm::vec3(0.0f, 0.0f, 1.0f));
        X = glm::scale(X, glm::vec3(scale));
        out = X;
        return true;
    }
    return false;
}

bool NpcLayer::SetSpawnHome(uint32_t guid, float x, float y, float z, float o)
{
    for (Npc& n : npcs_)
    {
        if (n.spawn.guid != guid)
            continue;
        n.spawn.x = x; n.spawn.y = y; n.spawn.z = z; n.spawn.o = o;
        // Snap the live simulation to the new home so it visibly moves under the gizmo.
        n.pos = glm::vec3(x, y, z);
        n.heading = o;
        n.moving = false;
        n.hasTarget = false;
        n.pauseMs = 0.0f;
        return true;
    }
    return false;
}

bool NpcLayer::UpdateSpawnEditable(uint32_t guid, const MapSpawn& fields)
{
    for (Npc& n : npcs_)
    {
        if (n.spawn.guid != guid)
            continue;
        const bool moved = n.spawn.x != fields.x || n.spawn.y != fields.y ||
                           n.spawn.z != fields.z || n.spawn.o != fields.o;
        const bool behaviourChanged = n.spawn.movementType != fields.movementType ||
                                      n.spawn.wanderDistance != fields.wanderDistance;
        n.spawn.x = fields.x;
        n.spawn.y = fields.y;
        n.spawn.z = fields.z;
        n.spawn.o = fields.o;
        n.spawn.movementType = fields.movementType;
        n.spawn.wanderDistance = fields.wanderDistance;
        n.spawn.phaseMask = fields.phaseMask;   // filter reads these live each frame
        n.spawn.spawnMask = fields.spawnMask;
        const bool displayChanged = n.spawn.displayId != fields.displayId ||
                                    n.spawn.displayScale != fields.displayScale ||
                                    n.spawn.displaySource != fields.displaySource ||
                                    n.spawn.spawnDisplayId != fields.spawnDisplayId ||
                                    n.spawn.spawnDisplayScale != fields.spawnDisplayScale ||
                                    n.spawn.templateDisplayId != fields.templateDisplayId ||
                                    n.spawn.templateDisplayScale != fields.templateDisplayScale ||
                                    n.spawn.templateDisplaySource != fields.templateDisplaySource;
        n.spawn.displayId = fields.displayId;   // caller-resolved; Build chooses event override per frame
        n.spawn.displayScale = fields.displayScale;
        n.spawn.displaySource = fields.displaySource;
        n.spawn.spawnDisplayId = fields.spawnDisplayId;
        n.spawn.hasSpawnDisplayOverrideColumn = fields.hasSpawnDisplayOverrideColumn;
        n.spawn.spawnDisplayScale = fields.spawnDisplayScale;
        n.spawn.templateDisplayId = fields.templateDisplayId;
        n.spawn.templateDisplayScale = fields.templateDisplayScale;
        n.spawn.templateDisplaySource = fields.templateDisplaySource;
        n.spawn.templateDisplayIndex = fields.templateDisplayIndex;
        n.spawn.templateDisplayCount = fields.templateDisplayCount;
        n.spawn.eventDisplayOverrides = fields.eventDisplayOverrides;
        if (displayChanged)
        {
            n.attachTried = false;
            n.attachmentParentDisplayId = 0;
            n.attachments.clear();
        }
        // Re-seed the simulation when the home or movement behaviour changed so the change shows.
        if (moved || behaviourChanged)
        {
            n.pos = glm::vec3(fields.x, fields.y, fields.z);
            n.heading = fields.o;
            n.moving = false;
            n.hasTarget = false;
            n.pauseMs = 0.0f;
            n.pathTried = false;   // re-evaluate the waypoint path for the (possibly new) MovementType
            n.path.clear();
            n.pathDelay.clear();
            n.pathOrientation.clear();
            n.pathMoveType.clear();
            n.pathIdx = 0;
        }
        return true;
    }
    return false;
}

bool NpcLayer::SetSpawnPathBinding(uint32_t guid, uint32_t pathId, uint32_t spawnPathId,
                                   uint32_t templatePathId, bool hasSpawnAddon, uint8_t movementType)
{
    for (Npc& n : npcs_)
    {
        if (n.spawn.guid != guid)
            continue;
        n.spawn.pathId = pathId;
        n.spawn.spawnPathId = spawnPathId;
        n.spawn.templatePathId = templatePathId;
        n.spawn.hasSpawnAddon = hasSpawnAddon;
        n.spawn.movementType = movementType;
        // A path assignment/clear must invalidate the lazy DB cache immediately. Keep the actor at
        // its home until a preview path is supplied or Build lazily reloads the new binding.
        n.pathTried = false;
        n.path.clear();
        n.pathDelay.clear();
        n.pathOrientation.clear();
        n.pathMoveType.clear();
        n.pathIdx = 0;
        n.pauseMs = 0.0f;
        n.hasTarget = false;
        n.pos = glm::vec3(n.spawn.x, n.spawn.y, n.spawn.z);
        n.heading = n.spawn.o;
        n.moving = false;
        return true;
    }
    return false;
}

bool NpcLayer::SetWaypointPath(uint32_t guid, const WaypointPath& path)
{
    for (Npc& n : npcs_)
    {
        if (n.spawn.guid != guid)
            continue;
        n.spawn.pathId = path.id;
        n.path.clear();
        n.pathDelay.clear();
        n.pathOrientation.clear();
        n.pathMoveType.clear();
        n.path.reserve(path.points.size());
        n.pathDelay.reserve(path.points.size());
        n.pathOrientation.reserve(path.points.size());
        n.pathMoveType.reserve(path.points.size());
        for (const WaypointPoint& p : path.points)
        {
            n.path.emplace_back(p.x, p.y, p.z);
            n.pathDelay.push_back(static_cast<float>(p.delay));
            n.pathOrientation.push_back(p.o);
            n.pathMoveType.push_back(p.moveType);
        }
        n.pathTried = true;  // this working copy is authoritative until the next binding change/reload
        n.pathIdx = 0;
        n.pauseMs = 0.0f;
        n.hasTarget = false;
        n.pos = glm::vec3(n.spawn.x, n.spawn.y, n.spawn.z);
        n.heading = n.spawn.o;
        n.moving = false;
        return true;
    }
    return false;
}

} // namespace we
