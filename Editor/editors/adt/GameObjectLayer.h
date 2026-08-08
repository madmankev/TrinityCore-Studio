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
#include "model/ModelDress.h"
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
    void AddGameObject(const MapGameObject& g);   // append one (renders next frame); "add GO" flow
    bool RemoveGameObject(uint32_t guid);         // remove by guid (delete / undo of an add)
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

    // Copy canonical map-spawn data for World Editor browsing/search. The returned values are
    // snapshots; mutations still go through FindSpawn()/the repository so render state stays valid.
    void SnapshotGameObjects(std::vector<MapGameObject>& out) const;

    // --- object selection / manipulation (ADT viewer) ---
    // Ray-pick the nearest visible GameObject (ray + origin in the streamer-local frame). Honors
    // the same visibility filter + cull distance as Build. Returns the spawn's guid + hit distance
    // in `outDist`, or 0 on a miss (bounding-sphere test against loaded model bounds).
    uint32_t Pick(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& focus,
                  const glm::vec3& origin, float cullDist, const SpawnFilter& filter, float& outDist);
    // Find a spawn's mutable data by guid (for the gizmo to read/write position/rotation/size).
    MapGameObject* FindSpawn(uint32_t guid);
    // The world (local-frame) placement matrix for a spawn (no transport anim) — seeds the gizmo.
    glm::mat4 SpawnMatrix(const MapGameObject& g, const glm::vec3& origin) const
    {
        return PlacementMatrix(g, origin, nullptr);
    }
    // Highlight one guid (hover tint) on the next Build (guid 0 / alpha 0 clears it).
    void SetHighlight(uint32_t guid, const glm::vec4& color) { highlightGuid_ = guid; highlightColor_ = color; }
    // Selection outline for one guid: rgb color + width in .a (guid 0 / width 0 clears).
    void SetOutline(uint32_t guid, const glm::vec4& colorWidth) { outlineGuid_ = guid; outlineColor_ = colorWidth; }

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
        glm::vec3 boundsCenter{0.0f};         // model-space bounding sphere (for distance/cull)
        float     boundsRadius = 1.0f;
        glm::vec3 boundsMin{0.0f};            // model-space AABB (for tight ray-vs-OBB picking)
        glm::vec3 boundsMax{0.0f};
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
    std::unique_ptr<ModelDresser> dresser_;   // fills runtime texture slots so GO models aren't white

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

    // Hover highlight: the guid tinted this frame (0 = none).
    uint32_t  highlightGuid_ = 0;
    glm::vec4 highlightColor_{0.0f};
    // Selection outline: the guid outlined this frame (0 = none).
    uint32_t  outlineGuid_ = 0;
    glm::vec4 outlineColor_{0.0f};

    int  drawn_ = 0;
    int  modelBudget_ = 0;
    bool cappedLastFrame_ = false;
};
} // namespace we
