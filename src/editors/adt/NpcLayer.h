#pragma once

// NpcLayer — renders DB creature spawns on the ADT terrain and locally SIMULATES their
// movement (idle / random-wander / waypoint path), animated with their client M2 files.
// It is a second, DB-sourced object source living alongside the AdtStreamer's ADT doodads:
// each frame it simulates + animates the near NPCs and appends SceneInstanceGpu entries to
// the same list the module hands to IRenderer::RenderWorld. No live server is involved —
// the DB gives home positions + movement definitions; this class plays them back.
//
// Threading/renderer: like the streamer's main-thread work, ALL model create/destroy runs
// on the render thread (inside the viewer's frame), so Build()/Clear() must be called there.

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
#include "clientdata/DbcStore.h"   // DbcStore::CreatureDisplay (nested type used below)
#include "editors/adt/MapSpawnRepository.h"

namespace we
{
class ClientData;
class IDatabase;

class NpcLayer
{
public:
    // Bind the shared services (client data + DBC store for model resolution, renderer for
    // GPU model create/destroy). Safe to call repeatedly; only stores pointers.
    void Init(ClientData* cd, DbcStore* dbc, IRenderer* renderer);

    // Replace the spawn set (resets per-NPC simulation to spawn/home state). Does NOT touch
    // already-created GPU models (shared across spawns by displayId).
    void SetSpawns(std::vector<MapSpawn> spawns);

    // Destroy all GPU models and drop spawns + cached DBC maps. Renderer must still be alive.
    void Clear();

    // Per-frame: simulate + animate the nearest NPCs and append their SceneInstanceGpu to
    // `outScene`. `focus` is the camera focus in the tile-local frame; `origin` is the
    // streamer's map origin (world = local + origin); `view` orients billboard bones; `db`
    // (may be null) lazily loads waypoint paths. `cullDist` (yards) bounds simulation;
    // `maxDraw` caps animated NPCs (nearest first) to keep the frame cheap.
    void Build(const glm::vec3& focus, const glm::vec3& origin, const glm::mat4& view, float dtMs,
               IDatabase* db, std::vector<SceneInstanceGpu>& outScene, int maxDraw, float cullDist,
               const SpawnFilter& filter);

    int spawnCount() const { return static_cast<int>(npcs_.size()); }
    int drawnCount() const { return drawn_; }
    int modelCount() const { return static_cast<int>(models_.size()); }
    bool cappedLastFrame() const { return cappedLastFrame_; }

private:
    // One uploaded creature model (deduped by displayId, since skins bake into textures).
    struct NpcModel
    {
        ModelHandle handle = 0;
        std::shared_ptr<m2::M2Model> m2;
        m2::M2Animator animator;
        int standSeq = 0;   // sequence index for AnimationData "Stand" (id 0)
        int walkSeq = 0;    // sequence index for "Walk" (id 4), fallback run/stand
    };

    // Per-spawn simulation + render state.
    struct Npc
    {
        MapSpawn spawn;
        glm::vec3 pos{0.0f};     // current simulated world position (TrinityCore coords)
        float heading = 0.0f;    // radians, faces movement while walking
        bool  moving = false;
        float animTime = 0.0f;   // ms into the current sequence
        float pauseMs = 0.0f;    // remaining pause (waypoint delay / wander rest)
        glm::vec3 target{0.0f};
        bool  hasTarget = false;
        uint32_t rng = 0;        // per-NPC LCG state (deterministic per guid)
        // waypoint path (loaded lazily when first simulated with a DB available)
        bool pathTried = false;
        std::vector<glm::vec3> path;
        std::vector<float>     pathDelay;   // ms, parallel to path
        int   pathIdx = 0;
        std::vector<glm::mat4> palette;     // per-frame folded palette (SceneInstanceGpu points here)
    };

    void EnsureDisplayMaps();
    NpcModel* EnsureModel(uint32_t displayId);   // build (budgeted) or fetch; null if pending/failed
    void Simulate(Npc& n, float dtMs, IDatabase* db);
    static int FindSequence(const m2::M2Model& m, uint16_t animationId);

    ClientData* cd_ = nullptr;
    DbcStore*   dbc_ = nullptr;
    IRenderer*  renderer_ = nullptr;

    bool displayMapsLoaded_ = false;
    std::unordered_map<uint32_t, DbcStore::CreatureDisplay> displays_;   // displayId -> model+skins
    std::unordered_map<uint32_t, std::string>              modelPaths_;  // modelId -> .m2 path

    std::vector<Npc> npcs_;
    std::unordered_map<uint32_t, NpcModel> models_;   // by displayId
    std::unordered_set<uint32_t> failedDisplays_;     // displayIds that couldn't resolve/load

    std::vector<std::pair<float, int>> near_;   // per-frame scratch: (dist^2, npc index)

    int  drawn_ = 0;
    int  modelBudget_ = 0;          // new models allowed to create this frame
    bool cappedLastFrame_ = false;  // more near NPCs than maxDraw (coverage truncated)
};
} // namespace we
