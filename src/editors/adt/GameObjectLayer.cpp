// GameObjectLayer — see GameObjectLayer.h.

#include "editors/adt/GameObjectLayer.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "clientdata/ClientData.h"
#include "clientdata/DbcStore.h"
#include "model/M2Loader.h"
#include "model/ModelUploadBuild.h"
#include "wmo/WmoLoader.h"
#include "wmo/WmoTypes.h"
#include "wmo/WmoUploadBuild.h"

namespace we
{
namespace
{
constexpr int   kModelsPerFrame = 2;      // budget: WMO/M2 parse + texture decode per new model
constexpr int   kMaxModels      = 512;    // memory guard on distinct uploaded GO models
constexpr float kMaxStepMs      = 100.0f; // clamp dt so a frame hitch doesn't skip animation far

bool EndsWithNoCase(const std::string& s, const char* suffix)
{
    const size_t n = std::char_traits<char>::length(suffix);
    if (s.size() < n)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    return true;
}
} // namespace

void GameObjectLayer::Init(ClientData* cd, DbcStore* dbc, IRenderer* renderer)
{
    cd_ = cd;
    dbc_ = dbc;
    renderer_ = renderer;
}

void GameObjectLayer::SetGameObjects(std::vector<MapGameObject> gos)
{
    gos_.clear();
    gos_.reserve(gos.size());
    for (const MapGameObject& g : gos)
    {
        Go go;
        go.data = g;
        gos_.push_back(std::move(go));
    }
}

void GameObjectLayer::SetMoTransports(std::vector<MoTransportDef> defs)
{
    moTransports_.clear();
    moTransports_.reserve(defs.size());
    for (const MoTransportDef& d : defs)
    {
        MoTransport m;
        m.def = d;
        moTransports_.push_back(std::move(m));
    }
}

void GameObjectLayer::Clear()
{
    if (renderer_)
        for (auto& kv : models_)
            if (kv.second.handle)
                renderer_->DestroyModel(kv.second.handle);
    models_.clear();
    failed_.clear();
    gos_.clear();
    moTransports_.clear();
    near_.clear();
    displayPaths_.clear();
    transportAnims_.clear();
    taxiPaths_.clear();
    modelMapLoaded_ = false;
    drawn_ = 0;
    cappedLastFrame_ = false;
}

void GameObjectLayer::EnsureModelMap()
{
    if (modelMapLoaded_ || !cd_ || !dbc_ || !cd_->IsOpen())
        return;
    displayPaths_ = dbc_->LoadGameObjectModelPaths(*cd_);

    // Type-11 transport keyframes: assemble per-entry translation (+ optional rotation) with a
    // loop period = the largest keyframe time.
    auto pos = dbc_->LoadTransportAnimation(*cd_);
    auto rot = dbc_->LoadTransportRotation(*cd_);
    for (auto& kv : pos)
    {
        TransportAnim ta;
        ta.pos = std::move(kv.second);
        if (!ta.pos.empty())
            ta.period = static_cast<float>(ta.pos.back().timeMs);
        auto rit = rot.find(kv.first);
        if (rit != rot.end())
        {
            ta.rot = std::move(rit->second);
            if (!ta.rot.empty())
                ta.period = std::max(ta.period, static_cast<float>(ta.rot.back().timeMs));
        }
        if (ta.period > 0.0f)
            transportAnims_[kv.first] = std::move(ta);
    }

    // Type-15 MO_TRANSPORT taxi routes.
    taxiPaths_ = dbc_->LoadTaxiPathNodes(*cd_);
    modelMapLoaded_ = true;
}

glm::mat4 GameObjectLayer::PlacementMatrix(const MapGameObject& g, const glm::vec3& origin,
                                           const glm::mat4* animLocal) const
{
    // DB coords are already the viewer's world frame (X=N-S, Y=E-W, Z=up); only subtract origin.
    const glm::vec3 local(g.x - origin.x, g.y - origin.y, g.z);

    // rotation0..3 = quaternion (x,y,z,w) in the game frame; glm's ctor takes (w,x,y,z). Fall
    // back to a yaw-about-Z from `orientation` when the quaternion is unset (all-zero rows).
    glm::quat q(g.rot[3], g.rot[0], g.rot[1], g.rot[2]);
    const float len2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (len2 < 1e-6f)
        q = glm::angleAxis(g.o, glm::vec3(0.0f, 0.0f, 1.0f));
    else
        q = glm::normalize(q);

    glm::mat4 M(1.0f);
    M = glm::translate(M, local);
    M = M * glm::mat4_cast(q);
    if (animLocal)   // transport (type 11): local keyframe offset, in the placement's frame
        M = M * (*animLocal);
    if (g.size != 1.0f && g.size > 0.0f)
        M = glm::scale(M, glm::vec3(g.size));
    return M;
}

// Interpolate a type-11 transport's local offset (+ optional rotation) at the current loop time.
glm::mat4 GameObjectLayer::TransportLocal(const TransportAnim& t) const
{
    glm::mat4 M(1.0f);
    if (t.period <= 0.0f)
        return M;
    const float tm = std::fmod(animTime_, t.period);

    if (!t.pos.empty())
    {
        glm::vec3 p(t.pos.front().x, t.pos.front().y, t.pos.front().z);
        for (size_t i = 0; i + 1 < t.pos.size(); ++i)
        {
            const auto& a = t.pos[i];
            const auto& b = t.pos[i + 1];
            if (tm >= static_cast<float>(a.timeMs) && tm <= static_cast<float>(b.timeMs))
            {
                float span = static_cast<float>(b.timeMs - a.timeMs);
                float f = span > 0.0f ? (tm - static_cast<float>(a.timeMs)) / span : 0.0f;
                p = glm::vec3(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f);
                break;
            }
            if (i + 2 == t.pos.size())   // past the last segment -> hold the final key
                p = glm::vec3(b.x, b.y, b.z);
        }
        M = glm::translate(M, p);
    }

    if (!t.rot.empty())
    {
        glm::quat q(t.rot.front().rot[3], t.rot.front().rot[0], t.rot.front().rot[1], t.rot.front().rot[2]);
        for (size_t i = 0; i + 1 < t.rot.size(); ++i)
        {
            const auto& a = t.rot[i];
            const auto& b = t.rot[i + 1];
            if (tm >= static_cast<float>(a.timeMs) && tm <= static_cast<float>(b.timeMs))
            {
                float span = static_cast<float>(b.timeMs - a.timeMs);
                float f = span > 0.0f ? (tm - static_cast<float>(a.timeMs)) / span : 0.0f;
                glm::quat qa(a.rot[3], a.rot[0], a.rot[1], a.rot[2]);
                glm::quat qb(b.rot[3], b.rot[0], b.rot[1], b.rot[2]);
                q = glm::slerp(glm::normalize(qa), glm::normalize(qb), f);
                break;
            }
        }
        M = M * glm::mat4_cast(glm::normalize(q));
    }
    return M;
}

GameObjectLayer::GoModel* GameObjectLayer::EnsureModel(uint32_t displayId)
{
    if (displayId == 0)
        return nullptr;
    auto it = models_.find(displayId);
    if (it != models_.end())
        return &it->second;
    if (failed_.count(displayId))
        return nullptr;
    if (modelBudget_ <= 0 || static_cast<int>(models_.size()) >= kMaxModels)
        return nullptr;

    auto pit = displayPaths_.find(displayId);
    if (pit == displayPaths_.end())
    {
        failed_.insert(displayId);
        return nullptr;
    }
    const std::string& path = pit->second;

    GoModel gm;
    gm.isWmo = EndsWithNoCase(path, ".wmo");

    if (gm.isWmo)
    {
        wmo::WmoModel wm;
        wmo::WmoLoadOptions wopt;
        wopt.doodads = false;   // GO placements are the shell only; keep it cheap
        wopt.liquid = false;
        if (!wmo::Load(*cd_, path, wm, wopt, nullptr, nullptr))
        {
            failed_.insert(displayId);
            --modelBudget_;
            return nullptr;
        }
        ModelUpload up = wmo::BuildUpload(*cd_, wm);
        // WMO shell rides bone 0 = the placement matrix (a 1-bone identity palette).
        for (ModelVertexGpu& v : up.vertices)
        {
            v.boneIndices[0] = 0;
            v.boneWeights[0] = 1.0f;
        }
        up.boneCount = 1;
        gm.handle = renderer_->CreateModel(up);
        if (!gm.handle)
        {
            failed_.insert(displayId);
            --modelBudget_;
            return nullptr;
        }
        gm.localBones = {glm::mat4(1.0f)};
    }
    else
    {
        gm.m2 = std::make_shared<m2::M2Model>();
        if (!m2::Load(*cd_, path, *gm.m2, nullptr))
        {
            failed_.insert(displayId);
            --modelBudget_;
            return nullptr;
        }
        ModelUpload up = m2::BuildUpload(*cd_, *gm.m2);
        gm.handle = renderer_->CreateModel(up);
        if (!gm.handle)
        {
            failed_.insert(displayId);
            --modelBudget_;
            return nullptr;
        }
        gm.animator.SetModel(gm.m2.get(), cd_, path);
        gm.animator.Evaluate(0, 0.0f, glm::mat4(1.0f), gm.localBones);   // bind pose
    }

    --modelBudget_;
    auto res = models_.emplace(displayId, std::move(gm));
    return &res.first->second;
}

void GameObjectLayer::EnsureAnimated(GoModel& m)
{
    // Loop the idle sequence (0) once per frame; the palette is shared by every instance of
    // this model (GOs are stationary, so they share the animation phase). WMOs stay static.
    if (m.isWmo || m.animatedThisFrame || !m.m2)
        return;
    float dur = static_cast<float>(m.animator.Duration(0));
    float t = dur > 0.0f ? std::fmod(animTime_, dur) : 0.0f;
    m.animator.Evaluate(0, t, frameView_, m.localBones);
    m.animatedThisFrame = true;
}

void GameObjectLayer::Build(const glm::vec3& focus, const glm::vec3& origin, const glm::mat4& view,
                            float dtMs, uint32_t currentMapId, const SpawnFilter& filter,
                            std::vector<SceneInstanceGpu>& outScene, int maxDraw, float cullDist)
{
    drawn_ = 0;
    cappedLastFrame_ = false;
    modelBudget_ = kModelsPerFrame;
    if (!renderer_)
        return;
    EnsureModelMap();
    if (!modelMapLoaded_)
        return;

    frameView_ = view;
    animTime_ += std::min(dtMs, kMaxStepMs);
    for (auto& kv : models_)
        kv.second.animatedThisFrame = false;   // reset shared palettes for this frame

    const glm::vec3 worldFocus = focus + origin;
    const float cull2 = cullDist * cullDist;

    near_.clear();
    for (int i = 0; i < static_cast<int>(gos_.size()); ++i)
    {
        const MapGameObject& g = gos_[i].data;
        if (!filter.Visible(g.phaseMask, g.spawnMask, g.eventEntry, g.poolHidden, g.groupManual))
            continue;   // filtered out (phase / difficulty / event / pool / spawn group)
        glm::vec3 d(g.x - worldFocus.x, g.y - worldFocus.y, g.z - worldFocus.z);
        float d2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (d2 > cull2)
            continue;
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
        Go& go = gos_[near_[k].second];
        GoModel* m = EnsureModel(go.data.displayId);
        if (!m)
            continue;   // pending model creation (budget) or unresolved display

        EnsureAnimated(*m);   // no-op for WMO

        // Type-11 transports (elevators/lifts) carry a looped local keyframe offset.
        const glm::mat4* animPtr = nullptr;
        glm::mat4 animLocal;
        if (go.data.type == 11)
        {
            auto tit = transportAnims_.find(go.data.entry);
            if (tit != transportAnims_.end())
            {
                animLocal = TransportLocal(tit->second);
                animPtr = &animLocal;
            }
        }

        const glm::mat4 M = PlacementMatrix(go.data, origin, animPtr);
        go.palette.resize(m->localBones.size());
        for (size_t j = 0; j < m->localBones.size(); ++j)
            go.palette[j] = M * m->localBones[j];

        SceneInstanceGpu si{};
        si.handle = m->handle;
        si.boneMatrices = go.palette.empty() ? nullptr : reinterpret_cast<const float*>(go.palette.data());
        si.boneCount = static_cast<int>(go.palette.size());
        si.worldOrigin[0] = go.data.x - origin.x;
        si.worldOrigin[1] = go.data.y - origin.y;
        si.worldOrigin[2] = go.data.z;
        outScene.push_back(si);
        ++drawn_;
    }

    // Boats/zeppelins (type 15): simulate along their taxi routes; render when on this map.
    BuildMoTransports(origin, currentMapId, outScene, cullDist * 3.0f, worldFocus, dtMs);
}

void GameObjectLayer::BuildMoTransports(const glm::vec3& origin, uint32_t currentMapId,
                                        std::vector<SceneInstanceGpu>& outScene, float cullDist,
                                        const glm::vec3& worldFocus, float dtMs)
{
    if (moTransports_.empty())
        return;
    const float cull2 = cullDist * cullDist;
    const float step = std::min(dtMs, kMaxStepMs);

    for (MoTransport& mo : moTransports_)
    {
        if (!mo.resolved)
        {
            auto pit = taxiPaths_.find(mo.def.taxiPathId);
            if (pit != taxiPaths_.end() && pit->second.size() >= 2)
            {
                mo.nodes = pit->second;
                mo.segLen.assign(mo.nodes.size(), 0.0f);
                mo.totalLen = 0.0f;
                for (size_t i = 0; i + 1 < mo.nodes.size(); ++i)
                {
                    const auto& a = mo.nodes[i];
                    const auto& b = mo.nodes[i + 1];
                    float len = 0.0f;
                    if (a.mapId == b.mapId)   // cross-map hops are instant (length 0)
                    {
                        float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
                        len = std::sqrt(dx * dx + dy * dy + dz * dz);
                    }
                    mo.segLen[i] = len;
                    mo.totalLen += len;
                }
            }
            mo.resolved = true;
        }
        if (mo.nodes.size() < 2 || mo.totalLen <= 0.0f)
            continue;

        float speed = mo.def.moveSpeed > 0.0f ? mo.def.moveSpeed : 10.0f;
        if (speed > 100.0f)   // guard odd data
            speed = 100.0f;
        mo.dist += speed * step / 1000.0f;
        mo.dist = std::fmod(mo.dist, mo.totalLen);

        // Locate the current segment.
        float acc = 0.0f;
        size_t seg = 0;
        for (; seg + 1 < mo.nodes.size(); ++seg)
        {
            if (mo.segLen[seg] <= 0.0f)
                continue;
            if (acc + mo.segLen[seg] >= mo.dist)
                break;
            acc += mo.segLen[seg];
        }
        if (seg + 1 >= mo.nodes.size() || mo.segLen[seg] <= 0.0f)
            continue;

        const auto& a = mo.nodes[seg];
        const auto& b = mo.nodes[seg + 1];
        if (a.mapId != currentMapId)
            continue;   // route currently on another map

        const float f = (mo.dist - acc) / mo.segLen[seg];
        const glm::vec3 wpos(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f);
        glm::vec3 dd(wpos.x - worldFocus.x, wpos.y - worldFocus.y, wpos.z - worldFocus.z);
        if (dd.x * dd.x + dd.y * dd.y + dd.z * dd.z > cull2)
            continue;

        GoModel* m = EnsureModel(mo.def.displayId);
        if (!m)
            continue;
        EnsureAnimated(*m);

        const glm::vec3 local(wpos.x - origin.x, wpos.y - origin.y, wpos.z);
        const float heading = std::atan2(b.y - a.y, b.x - a.x);
        glm::mat4 M(1.0f);
        M = glm::translate(M, local);
        M = glm::rotate(M, heading, glm::vec3(0.0f, 0.0f, 1.0f));
        if (mo.def.size != 1.0f && mo.def.size > 0.0f)
            M = glm::scale(M, glm::vec3(mo.def.size));

        mo.palette.resize(m->localBones.size());
        for (size_t j = 0; j < m->localBones.size(); ++j)
            mo.palette[j] = M * m->localBones[j];

        SceneInstanceGpu si{};
        si.handle = m->handle;
        si.boneMatrices = mo.palette.empty() ? nullptr : reinterpret_cast<const float*>(mo.palette.data());
        si.boneCount = static_cast<int>(mo.palette.size());
        si.worldOrigin[0] = local.x;
        si.worldOrigin[1] = local.y;
        si.worldOrigin[2] = local.z;
        outScene.push_back(si);
        ++drawn_;
    }
}
} // namespace we
