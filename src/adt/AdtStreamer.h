#pragma once

// AdtStreamer — streams a whole WoW map around the camera. Background worker threads do all file
// IO + parse + BLP decode (the expensive part), producing fully CPU-decoded uploads; the main
// thread only creates GPU resources (budgeted per frame) and renders. Objects that span tiles are
// deduped by their MDDF/MODF `uniqueId` and refcounted by the tiles that reference them, so a large
// WMO loads and draws exactly once. WMO-only maps (dungeons/raids) load a single global WMO instead.
//
// Threading: the renderer is single-threaded — only the main thread calls Create*/Destroy*/Render.
// Workers touch only ClientData (mutex-guarded reads), the pure loaders, and the request/result
// queues. See AGENTS.md.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "gfx/IRenderer.h"
#include "model/M2Animator.h"
#include "model/M2EffectSystem.h"
#include "model/M2Types.h"
#include "adt/AdtLoader.h"
#include "adt/AdtTypes.h"
#include "clientdata/DbcStore.h"

namespace we
{
class ClientData;

class AdtStreamer
{
public:
    ~AdtStreamer() { Shutdown(); }

    void Init(ClientData* cd, DbcStore* dbc, IRenderer* renderer, int workerCount = 3);
    void Shutdown();   // stop workers + free all GPU resources

    // Open a map (parse its WDT). Terrain maps stream tiles; WMO-only maps load a global WMO.
    // Bumps the generation and clears prior state. Returns false if the WDT is missing.
    bool OpenMap(const std::string& mapDir);
    const adt::AdtWorldInfo& world() const { return world_; }
    bool wmoOnly() const { return world_.wmoOnly; }
    glm::vec3 origin() const { return origin_; }
    glm::vec3 startPos() const { return startPos_; }   // suggested camera focus (tile-local frame)
    float startRadius() const { return startRadius_; }

    // Per-frame. `camPos` is the camera position in the tile-local frame (world - origin). Requests
    // missing tiles in `radius`, evicts far ones, and creates up to a few tiles' GPU resources.
    void Update(const glm::vec3& camPos, int radius, const adt::AdtLoadOptions& opt);

    // Build this frame's render lists: terrain handles, GPU-instanced object groups (frustum-culled,
    // grouped by model), and non-instanced instances (liquid + the nearest emitter objects' effects).
    void BuildFrame(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& eye, float dtMs,
                    std::vector<TerrainHandle>& outTerrains, std::vector<InstancedGroup>& outInstanced,
                    std::vector<SceneInstanceGpu>& outScene);

    int loadedTiles() const { return (int)tiles_.size(); }
    int pendingTiles() const { return (int)inFlight_.size(); }
    int objectCount() const { return (int)objects_.size(); }
    int modelCount() const { return (int)models_.size(); }
    int terrainTextureCount() { std::lock_guard<std::mutex> lk(knownMutex_); return (int)terrainTexKnown_.size(); }

    // Doodads projecting smaller than this fraction of viewport height are culled (0 = draw all).
    void setMinScreenFrac(float f) { minScreenFrac_ = f; }
    float minScreenFrac() const { return minScreenFrac_; }

    // Distant WDL terrain (mountain silhouettes for tiles not streamed as full ADTs).
    void setShowWdl(bool b) { showWdl_ = b; }
    bool showWdl() const { return showWdl_; }
    int lowTileCount() const { return (int)lowTiles_.size(); }

private:
    // ---- worker payloads (worker -> main) ----
    struct ModelPayload
    {
        std::string path;
        bool        isWmo = false;
        bool        skipped = false;   // path already GPU-loaded; main reuses the handle
        ModelUpload upload;            // decoded geometry+textures (empty when skipped)
        std::shared_ptr<m2::M2Model> m2;   // kept for the animator (M2 only)
        glm::vec3   boundsCenter{0.0f};
        float       boundsRadius = 1.0f;
        bool        hasEmitters = false;
    };
    struct PlacementRef
    {
        uint64_t  uid = 0;
        std::string path;
        bool       isWmo = false;
        glm::mat4  transform{1.0f};
        glm::vec3  origin{0.0f};
    };
    struct TilePayload
    {
        uint32_t key = 0;
        uint64_t generation = 0;
        bool     ok = false;
        TerrainUpload terrain;
        ModelUpload   liquid;
        std::vector<adt::AdtSubmesh> liquidSubmeshes;   // frame-anim info, parallel to liquid submeshes
        std::vector<PlacementRef>    placements;
        std::vector<ModelPayload>    models;            // unique new models referenced by this tile
    };
    struct Request { uint32_t key; uint64_t generation; glm::vec3 origin; adt::AdtLoadOptions opt; };

    // ---- main-thread registries ----
    struct ModelEntry
    {
        ModelHandle handle = 0;
        std::shared_ptr<m2::M2Model> m2;
        m2::M2Animator animator;
        bool hasEmitters = false;
        std::vector<glm::mat4>   localBones;    // animated palette this frame (M2-local)
        std::vector<SubmeshAnim> subAnims;      // per-batch UV/color this frame
        bool animatedThisFrame = false;
        glm::vec3 boundsCenter{0.0f};
        float     boundsRadius = 1.0f;
        bool      isWmo = false;
        int       refCount = 0;
    };
    struct WorldObject
    {
        std::string modelPath;
        glm::mat4 transform{1.0f};
        glm::vec3 origin{0.0f};
        glm::vec3 cullCenter{0.0f};
        float     cullRadius = 1.0f;
        std::unique_ptr<m2::M2EffectSystem> effects;
        std::vector<glm::mat4> palette;
        std::unordered_set<uint32_t> refTiles;
    };
    struct LoadedTile
    {
        TerrainHandle terrain = 0;
        ModelHandle   liquid = 0;
        float centerZ = 0.0f;   // tile terrain Z midpoint (local frame), for the frustum cull sphere
        float radiusZ = 0.0f;   // half the tile's terrain Z extent
        std::vector<adt::AdtSubmesh> liquidSubmeshes;
        std::vector<SubmeshAnim>     liquidAnims;
        std::vector<uint64_t>        objectUids;
    };

    void WorkerLoop();
    void ProcessRequest(const Request& r, TilePayload& out);
    void ConsumePayload(TilePayload&& p);
    void EvictTile(uint32_t key);
    void ClearAll();                     // main-thread teardown of all GPU + registries
    void LoadGlobalWmo();                // WMO-only maps
    void LoadWdlTiles();                 // build the low-detail distant-terrain meshes (map open)

    static uint32_t Key(int x, int y) { return (uint32_t)(y * 64 + x); }
    static int KeyX(uint32_t k) { return (int)(k % 64); }
    static int KeyY(uint32_t k) { return (int)(k / 64); }
    bool TileExists(int x, int y) const;

    ClientData* cd_ = nullptr;
    DbcStore*   dbc_ = nullptr;
    IRenderer*  renderer_ = nullptr;
    adt::LiquidTypeTable liquidTypes_;

    adt::AdtWorldInfo world_;
    std::string mapDir_;
    glm::vec3 origin_{0.0f};
    glm::vec3 startPos_{0.0f};
    float     startRadius_ = 400.0f;
    std::vector<char> tileExists_;   // 64*64

    // worker pool + queues
    std::vector<std::thread> workers_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> generation_{1};
    std::mutex reqMutex_;
    std::condition_variable reqCv_;
    std::deque<Request> requests_;
    std::mutex resMutex_;
    std::deque<TilePayload> results_;
    std::mutex knownMutex_;
    std::unordered_set<std::string> knownPaths_;      // model paths the main thread has GPU-created
    std::unordered_set<std::string> terrainTexKnown_; // ground-texture paths already in the renderer cache

    // main-thread state
    std::unordered_map<uint32_t, LoadedTile>  tiles_;
    std::unordered_set<uint32_t>              inFlight_;

    // WDL low-detail terrain: one coarse mesh per existing tile, built once at OpenMap. Drawn for
    // tiles NOT currently loaded as full ADTs, so distant terrain shows without streaming.
    struct LowTile { ModelHandle handle = 0; float centerZ = 0.0f; float radiusZ = 0.0f; };
    std::unordered_map<uint32_t, LowTile> lowTiles_;
    std::vector<glm::mat4> lowPalette_{glm::mat4(1.0f)};   // identity palette for the mesh path
    bool showWdl_ = true;
    std::unordered_map<std::string, ModelEntry> models_;
    std::unordered_map<uint64_t, WorldObject>   objects_;
    float liquidTime_ = 0.0f;
    float animTime_ = 0.0f;
    float minScreenFrac_ = 0.0015f;   // ~0.5px at 700px; cull only the genuinely-invisible doodad tail

    // Per-frame scratch for instanced groups (inner vectors' data() stay valid across outer
    // growth via move, so InstancedGroup can point into them for the frame).
    std::vector<std::vector<glm::mat4>> instXforms_;
};
} // namespace we
