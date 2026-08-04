#pragma once

// GameObjectLayer — renders DB GameObject spawns on the ADT terrain, alongside NpcLayer.
// Like the NPC layer it resolves each spawn to a client model, places it, and appends
// SceneInstanceGpu entries to the list the module hands to IRenderer::RenderWorld. GOs differ
// from creatures in three ways: the model may be an M2 OR a WMO, the spawn carries a full 3D
// quaternion rotation (not just yaw), and most GOs are static (idle animation loops in place;
// moving transports are a later phase). No live server — the DB gives home placement + state.
//
// Threading/renderer: all model create/destroy runs on the render thread (inside the viewer's
// frame), so Build()/Clear() must be called there.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "gfx/IRenderer.h"
#include "model/M2Animator.h"
#include "model/M2Types.h"
#include "clientdata/DbcStore.h"   // nested key/node types used in members below
#include "editors/adt/MapSpawnRepository.h"

namespace we
{
class ClientData;

class GameObjectLayer
{
public:
    void Init(ClientData* cd, DbcStore* dbc, IRenderer* renderer);
    void SetGameObjects(std::vector<MapGameObject> gos);
    void SetMoTransports(std::vector<MoTransportDef> defs);   // boats/zeppelins (type 15)
    void Clear();   // destroy GPU models + drop GOs + cached DBC map (renderer must be alive)

    // Per-frame: place + animate the nearest GameObjects and append their SceneInstanceGpu to
    // `outScene`. `focus` is the camera focus (tile-local); `origin` the streamer map origin
    // (world = local + origin); `view` orients M2 billboard bones. `currentMapId` gates which
    // MO_TRANSPORT route segments render. `cullDist` (yards) + `maxDraw` bound the work.
    void Build(const glm::vec3& focus, const glm::vec3& origin, const glm::mat4& view, float dtMs,
               uint32_t currentMapId, const SpawnFilter& filter,
               std::vector<SceneInstanceGpu>& outScene, int maxDraw, float cullDist);

    int count() const { return static_cast<int>(gos_.size()); }
    int drawnCount() const { return drawn_; }
    int modelCount() const { return static_cast<int>(models_.size()); }
    bool cappedLastFrame() const { return cappedLastFrame_; }

private:
    // One uploaded GO model (deduped by displayId). WMO models render as a 1-bone identity
    // palette (bone 0 = the placement matrix); M2 models animate their idle sequence.
    struct GoModel
    {
        ModelHandle handle = 0;
        bool isWmo = false;
        std::shared_ptr<m2::M2Model> m2;      // null for WMO
        m2::M2Animator animator;              // M2 only
        std::vector<glm::mat4> localBones;    // shared animated palette this frame (WMO = {identity})
        bool animatedThisFrame = false;
    };

    struct Go
    {
        MapGameObject data;
        std::vector<glm::mat4> palette;   // per-frame folded palette (SceneInstanceGpu points here)
    };

    // A type-11 transport's looped local keyframe path (elevator/lift), from TransportAnimation.dbc.
    struct TransportAnim
    {
        std::vector<DbcStore::TransportPosKey> pos;
        std::vector<DbcStore::TransportRotKey> rot;
        float period = 0.0f;   // ms (loop length = max keyframe time)
    };

    // A type-15 MO_TRANSPORT (boat/zeppelin) moving along a taxi route.
    struct MoTransport
    {
        MoTransportDef def;
        std::vector<DbcStore::TaxiNode> nodes;
        std::vector<float> segLen;   // length of segment i->i+1 (0 across a map boundary)
        float totalLen = 0.0f;
        float dist = 0.0f;           // simulated distance along the route (loops)
        bool  resolved = false;
        std::vector<glm::mat4> palette;
    };

    void EnsureModelMap();
    GoModel* EnsureModel(uint32_t displayId);          // build (budgeted) or fetch; null if pending/failed
    void EnsureAnimated(GoModel& m);   // compute the model's looped palette once/frame (uses frameView_)
    glm::mat4 PlacementMatrix(const MapGameObject& g, const glm::vec3& origin,
                              const glm::mat4* animLocal) const;
    glm::mat4 TransportLocal(const TransportAnim& t) const;   // interpolated elevator offset at animTime_
    void BuildMoTransports(const glm::vec3& origin, uint32_t currentMapId,
                           std::vector<SceneInstanceGpu>& outScene, float cullDist,
                           const glm::vec3& worldFocus, float dtMs);

    ClientData* cd_ = nullptr;
    DbcStore*   dbc_ = nullptr;
    IRenderer*  renderer_ = nullptr;

    bool modelMapLoaded_ = false;
    std::unordered_map<uint32_t, std::string> displayPaths_;   // displayId -> model path (.m2 / .wmo)

    std::vector<Go> gos_;
    std::unordered_map<uint32_t, GoModel> models_;   // by displayId
    std::unordered_set<uint32_t> failed_;

    // Transport movement data (DBC, loaded once with the model map).
    std::unordered_map<uint32_t, TransportAnim> transportAnims_;   // by gameobject entry (type 11)
    std::unordered_map<uint32_t, std::vector<DbcStore::TaxiNode>> taxiPaths_;   // by path id (type 15)
    std::vector<MoTransport> moTransports_;

    std::vector<std::pair<float, int>> near_;   // per-frame scratch: (dist^2, go index)
    float animTime_ = 0.0f;
    glm::mat4 frameView_{1.0f};   // this frame's camera view (for M2 billboard bones)

    int  drawn_ = 0;
    int  modelBudget_ = 0;
    bool cappedLastFrame_ = false;
};
} // namespace we
