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
#include "model/ModelDress.h"
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

    // Append one spawn (renders next frame via EnsureModel). Used by the viewer's "add NPC" flow.
    void AddSpawn(const MapSpawn& s);
    // Remove a spawn by guid (delete / undo of an add). Returns false if not present.
    bool RemoveSpawn(uint32_t guid);

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

    // Diagnostics for the World Editor when spawn markers/waypoints appear but client M2 models do
    // not. A terrain-capable AzerothCore server Data directory is not a replacement for a full WoW
    // client Data directory containing CreatureDisplayInfo/CreatureModelData and M2 assets.
    bool modelDataReady() const { return displayMapsLoaded_ && !modelPaths_.empty(); }
    int displayInfoCount() const { return static_cast<int>(displays_.size()); }
    int modelPathCount() const { return static_cast<int>(modelPaths_.size()); }
    int failedDisplayCount() const { return static_cast<int>(failedDisplays_.size()); }
    int directModelFallbackCount() const { return static_cast<int>(directModelFallbacks_.size()); }

    // Copy the map's canonical spawn homes for UI tooling such as the World Outliner. This is
    // intentionally a snapshot (rather than exposing Npc internals), so callers cannot mutate the
    // simulation state without going through the explicit edit methods below.
    void SnapshotSpawns(std::vector<MapSpawn>& out) const;

    // --- object selection / manipulation (ADT viewer) ---
    // Ray-pick the nearest visible NPC (ray + origin in the streamer-local frame). Honors the same
    // visibility filter + cull distance as Build. Returns guid + hit distance, or 0 on a miss.
    uint32_t Pick(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& focus,
                  const glm::vec3& origin, float cullDist, const SpawnFilter& filter, float& outDist);
    const MapSpawn* FindSpawn(uint32_t guid) const;   // spawn home (for save/read-back)
    // The home placement matrix (local frame) for a spawn — seeds the gizmo (scale = spawn.scale).
    bool HomeMatrix(uint32_t guid, const glm::vec3& origin, glm::mat4& out) const;
    // Move a spawn's home to (x,y,z,orientation) and snap its live simulation there so it visibly
    // moves under the gizmo. Returns false if the guid is gone.
    bool SetSpawnHome(uint32_t guid, float x, float y, float z, float o);
    // Apply an instance edit's render/sim-relevant fields (position/orientation, movementType,
    // wanderDistance, phase/spawn masks, and the caller-resolved displayId) onto a spawn and re-seed
    // its simulation. Build re-resolves the model from spawn.displayId each frame, so a display change
    // takes effect automatically. Returns false if the guid is gone.
    bool UpdateSpawnEditable(uint32_t guid, const MapSpawn& fields);
    // Update the resolved path source/binding after a visual path is assigned or cleared. This only
    // changes the in-memory World Editor state; the repository performs the matching DB transaction.
    bool SetSpawnPathBinding(uint32_t guid, uint32_t pathId, uint32_t spawnPathId,
                             uint32_t templatePathId, bool hasSpawnAddon, uint8_t movementType);
    // Push a working waypoint path into one NPC's simulator, so edits preview immediately before Save.
    // The next map reload still reads the canonical rows from the database.
    bool SetWaypointPath(uint32_t guid, const WaypointPath& path);
    // Highlight one guid (hover tint) on the next Build (guid 0 / alpha 0 clears it).
    void SetHighlight(uint32_t guid, const glm::vec4& color) { highlightGuid_ = guid; highlightColor_ = color; }
    // Selection outline for one guid: rgb color + width in .a (guid 0 / width 0 clears).
    void SetOutline(uint32_t guid, const glm::vec4& colorWidth) { outlineGuid_ = guid; outlineColor_ = colorWidth; }

private:
    // One uploaded creature model (deduped by displayId, since skins bake into textures).
    struct NpcModel
    {
        ModelHandle handle = 0;
        std::shared_ptr<m2::M2Model> m2;
        m2::M2Animator animator;
        int standSeq = 0;   // sequence index for AnimationData "Stand" (id 0)
        int walkSeq = 0;    // sequence index for "Walk" (id 4), fallback run/stand
        glm::vec3 boundsCenter{0.0f};   // model-space bounding sphere (for distance/cull)
        float     boundsRadius = 1.0f;
        glm::vec3 boundsMin{0.0f};      // model-space AABB (for tight ray-vs-OBB picking)
        glm::vec3 boundsMax{0.0f};
    };

    // One uploaded held item (weapon/shield), shared across all NPCs that wield the same model.
    // localBones is the item's static bind pose; per-frame each wielding NPC folds it by the
    // parent attachment bone (see Build). Keyed by held-model path.
    struct HeldModel
    {
        ModelHandle handle = 0;
        std::vector<glm::mat4> localBones;   // bind-pose palette (Evaluate at t=0)
    };

    // A weapon attached to one NPC: which parent attachment point + which shared held model, plus
    // per-frame scratch for the folded palette (kept alive on the Npc for the frame's RenderWorld).
    struct NpcAttach
    {
        uint32_t attachId = 0;             // parent M2 attachment id (1 = right hand, 2 = left hand)
        const HeldModel* model = nullptr;  // stable across rehash (unordered_map node stability)
        std::vector<glm::mat4> palette;
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
        std::vector<float>     pathDelay;       // ms, parallel to path
        std::vector<float>     pathOrientation; // radians; 0 = keep movement-facing
        std::vector<uint8_t>   pathMoveType;    // 0 walk, 1 run, 2 land, 3 take off
        int   pathIdx = 0;
        std::vector<glm::mat4> palette;     // per-frame folded palette (SceneInstanceGpu points here)
        // Held weapons (resolved once when first drawn from spawn.weaponDisplay).
        bool  attachTried = false;
        std::vector<NpcAttach> attachments;
    };

    void EnsureDisplayMaps();
    NpcModel* EnsureModel(uint32_t displayId);   // build (budgeted) or fetch; null if pending/failed
    // Composite a character-model NPC's appearance from its CreatureDisplayInfoExtra: set the body
    // (type 1) + hair (type 6) texture paths on `model`, output the composited body atlas (grafted
    // into the type-1 slot at upload), the type-1 slot index, and per-submesh geoset visibility.
    void DressCharacterNpc(const std::string& path, const DbcStore::CreatureDisplayExtra& extra,
                           m2::M2Model& model, BlpImage& bodyOut, int& bodySlotOut,
                           std::vector<uint8_t>& visibleOut);
    // Load (or fetch) a held weapon/shield model, apply its object skin, and cache its bind pose.
    const HeldModel* EnsureHeldModel(const std::string& path, const std::string& objectSkin);
    // Resolve an NPC's held weapons from spawn.weaponDisplay into n.attachments (once, budgeted).
    void EnsureAttachments(Npc& n, const NpcModel& parent);
    void Simulate(Npc& n, float dtMs, IDatabase* db);
    static int FindSequence(const m2::M2Model& m, uint16_t animationId);

    ClientData* cd_ = nullptr;
    DbcStore*   dbc_ = nullptr;
    IRenderer*  renderer_ = nullptr;
    std::unique_ptr<ModelDresser> dresser_;   // resolves runtime skins (creature/character/item)

    bool displayMapsLoaded_ = false;
    std::unordered_map<uint32_t, DbcStore::CreatureDisplay> displays_;   // displayId -> model+skins
    std::unordered_map<uint32_t, std::string>              modelPaths_;  // modelId -> .m2 path
    // extendedDisplayId -> character-model NPC customization + worn armour (empty for non-char NPCs).
    std::unordered_map<uint32_t, DbcStore::CreatureDisplayExtra> displayExtras_;

    std::vector<Npc> npcs_;
    std::unordered_map<uint32_t, NpcModel> models_;   // by displayId
    std::unordered_set<uint32_t> failedDisplays_;     // displayIds that couldn't resolve/load
    // Custom databases occasionally store a CreatureModelData id directly instead of a
    // CreatureDisplayInfo id. We support that non-standard but useful fallback and expose its count.
    std::unordered_set<uint32_t> directModelFallbacks_;
    std::unordered_map<std::string, HeldModel> heldModels_;   // by held-model path (shared weapons)
    std::unordered_set<std::string> failedHeld_;              // held-model paths that couldn't load

    std::vector<std::pair<float, int>> near_;   // per-frame scratch: (dist^2, npc index)

    // Hover highlight: the guid tinted this frame (0 = none).
    uint32_t  highlightGuid_ = 0;
    glm::vec4 highlightColor_{0.0f};
    // Selection outline: the guid outlined this frame (0 = none).
    uint32_t  outlineGuid_ = 0;
    glm::vec4 outlineColor_{0.0f};

    int  drawn_ = 0;
    int  modelBudget_ = 0;          // new models allowed to create this frame
    bool cappedLastFrame_ = false;  // more near NPCs than maxDraw (coverage truncated)
};
} // namespace we
