// NpcLayer — see NpcLayer.h.

#include "editors/adt/NpcLayer.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "clientdata/ClientData.h"
#include "clientdata/DbcStore.h"
#include "db/IDatabase.h"
#include "model/M2Loader.h"
#include "model/ModelUploadBuild.h"
#include "schema/Waypoint.h"

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
} // namespace

void NpcLayer::Init(ClientData* cd, DbcStore* dbc, IRenderer* renderer)
{
    cd_ = cd;
    dbc_ = dbc;
    renderer_ = renderer;
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

void NpcLayer::Clear()
{
    if (renderer_)
        for (auto& kv : models_)
            if (kv.second.handle)
                renderer_->DestroyModel(kv.second.handle);
    models_.clear();
    failedDisplays_.clear();
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
    displayMapsLoaded_ = true;
}

int NpcLayer::FindSequence(const m2::M2Model& m, uint16_t animationId)
{
    for (size_t i = 0; i < m.sequences.size(); ++i)
        if (m.sequences[i].animationId == animationId)
            return static_cast<int>(i);
    return -1;
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

    // displayId -> modelId -> .m2 path (+ skins).
    auto dit = displays_.find(displayId);
    if (dit == displays_.end())
    {
        failedDisplays_.insert(displayId);
        return nullptr;
    }
    auto mp = modelPaths_.find(dit->second.modelId);
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
    // Apply the display's body-texture skins to runtime slots (texture types 11/12/13),
    // combined with the model's directory — same as the --m2-shot-display harness.
    std::string dir;
    {
        size_t s = path.find_last_of("\\/");
        dir = (s == std::string::npos) ? "" : path.substr(0, s);
    }
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
    {
        uint32_t tt = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
        if (tt >= 11 && tt <= 13 && !dit->second.skins[tt - 11].empty())
            model.texturePaths[i] = dir + "\\" + dit->second.skins[tt - 11] + ".blp";
    }

    ModelUpload up = m2::BuildUpload(*cd_, model);
    ModelHandle h = renderer_->CreateModel(up);
    if (!h)
    {
        failedDisplays_.insert(displayId);
        --modelBudget_;
        return nullptr;
    }

    NpcModel nm;
    nm.handle = h;
    nm.m2 = std::make_shared<m2::M2Model>(std::move(model));
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

    const float speed = kBaseWalkSpeed * (n.spawn.speedWalk > 0.0f ? n.spawn.speedWalk : 1.0f);
    const float step = speed * dtMs / 1000.0f;

    // Move toward `tgt` (XY); returns true on arrival. Faces the movement direction.
    auto moveToward = [&](const glm::vec3& tgt) -> bool
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
        if (moveToward(n.target))
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
                for (const WaypointPoint& p : wp.points)
                {
                    n.path.emplace_back(p.x, p.y, p.z);
                    n.pathDelay.push_back(static_cast<float>(p.delay));
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
        if (moveToward(n.path[n.pathIdx]))
        {
            n.pauseMs = n.pathDelay[n.pathIdx];
            n.pathIdx = (n.pathIdx + 1) % static_cast<int>(n.path.size());
        }
        return;
    }

    // Idle (mt 0) or anything unsupported: stand at the home position.
    n.moving = false;
    n.pos = glm::vec3(n.spawn.x, n.spawn.y, n.spawn.z);
    n.heading = n.spawn.o;
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
        NpcModel* m = EnsureModel(n.spawn.displayId);
        if (!m)
            continue;   // pending model creation (budget) or unresolved display

        int seq = n.moving ? m->walkSeq : m->standSeq;
        if (seq < 0)
            seq = 0;
        float dur = static_cast<float>(m->animator.Duration(seq));
        float t = dur > 0.0f ? std::fmod(n.animTime, dur) : 0.0f;
        m->animator.Evaluate(seq, t, view, n.palette);   // M2-local palette

        // Fold the world transform into the palette (world = local + origin; Z not offset).
        const glm::vec3 local(n.pos.x - origin.x, n.pos.y - origin.y, n.pos.z);
        glm::mat4 X(1.0f);
        X = glm::translate(X, local);
        X = glm::rotate(X, n.heading + kHeadingOffset, glm::vec3(0.0f, 0.0f, 1.0f));
        if (n.spawn.scale != 1.0f && n.spawn.scale > 0.0f)
            X = glm::scale(X, glm::vec3(n.spawn.scale));
        for (glm::mat4& b : n.palette)
            b = X * b;

        SceneInstanceGpu si{};
        si.handle = m->handle;
        si.boneMatrices = n.palette.empty() ? nullptr : reinterpret_cast<const float*>(n.palette.data());
        si.boneCount = static_cast<int>(n.palette.size());
        si.worldOrigin[0] = local.x;
        si.worldOrigin[1] = local.y;
        si.worldOrigin[2] = local.z;
        outScene.push_back(si);
        ++drawn_;
    }
}
} // namespace we
