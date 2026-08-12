// AdtStreamer — see AdtStreamer.h.

#include "adt/AdtStreamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>

#include <glm/gtc/matrix_transform.hpp>

#include "clientdata/ClientData.h"
#include "model/M2Loader.h"
#include "model/ModelUploadBuild.h"
#include "wmo/WmoDoodadPlacement.h"
#include "wmo/WmoLoader.h"
#include "wmo/WmoTypes.h"
#include "wmo/WmoUploadBuild.h"
#include "adt/AdtUploadBuild.h"
#include "viewer/Frustum.h"          // SphereInFrustum
#include "viewer/Picking.h"          // RaySphere

namespace we
{
namespace
{
std::string LowerStr(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
// A tile's center in WoW world coords (matches adt::Load's self-centering worldOffset).
glm::vec2 TileCenterWorld(int x, int y)
{
    return {(31.5f - y) * adt::kTileSize, (31.5f - x) * adt::kTileSize};
}
constexpr int kConsumePerFrame = 2;   // tiles' GPU resources created per frame (budget)
constexpr int kEnqueuePerFrame = 6;   // new tile load requests posted per frame
} // namespace

void AdtStreamer::Init(ClientData* cd, DbcStore* dbc, IRenderer* renderer, int workerCount)
{
    cd_ = cd;
    dbc_ = dbc;
    renderer_ = renderer;
    if (dbc_ && cd_)
    {
        liquidTypes_ = dbc_->LoadLiquidTypes(*cd_);
        dresser_ = std::make_unique<ModelDresser>(*cd_, *dbc_);
    }
    tileExists_.assign(64 * 64, 0);
    stop_ = false;
    for (int i = 0; i < std::max(1, workerCount); ++i)
        workers_.emplace_back([this] { WorkerLoop(); });
}

void AdtStreamer::Shutdown()
{
    if (!workers_.empty())
    {
        { std::lock_guard<std::mutex> lk(reqMutex_); stop_ = true; }
        reqCv_.notify_all();
        for (std::thread& t : workers_) if (t.joinable()) t.join();
        workers_.clear();
    }
    ClearAll();
    // Drop the renderer so a second Shutdown() (the destructor's, which runs AFTER App frees the
    // renderer) doesn't call through a dangling pointer in ClearAll. Shutdown is terminal — the
    // module calls it once via OnShutdown while the renderer is alive, then ~AdtStreamer calls it
    // again during teardown. ClearAll gates all renderer_ use on non-null.
    renderer_ = nullptr;
}

bool AdtStreamer::TileExists(int x, int y) const
{
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return false;
    return tileExists_[y * 64 + x] != 0;
}

bool AdtStreamer::IsTileLoaded(int x, int y) const
{
    if (x < 0 || x >= 64 || y < 0 || y >= 64)
        return false;
    return tiles_.find(Key(x, y)) != tiles_.end();
}

bool AdtStreamer::IsTilePending(int x, int y) const
{
    if (x < 0 || x >= 64 || y < 0 || y >= 64)
        return false;
    return inFlight_.count(Key(x, y)) != 0;
}

void AdtStreamer::ClearAll()
{
    if (renderer_)
    {
        for (auto& kv : tiles_)
        {
            if (kv.second.terrain) renderer_->DestroyTerrain(kv.second.terrain);
            if (kv.second.liquid) renderer_->DestroyModel(kv.second.liquid);
        }
        for (auto& kv : models_)
            if (kv.second.handle) renderer_->DestroyModel(kv.second.handle);
        for (auto& kv : lowTiles_)
            if (kv.second.handle) renderer_->DestroyModel(kv.second.handle);
        renderer_->ClearTerrainTextureCache();   // no terrain live now -> free shared ground textures
    }
    tiles_.clear();
    inFlight_.clear();
    models_.clear();
    lowTiles_.clear();
    objects_.clear();
    embeddedWmoDoodadCount_ = 0;
    { std::lock_guard<std::mutex> lk(knownMutex_); knownPaths_.clear(); terrainTexKnown_.clear(); }
}

bool AdtStreamer::OpenMap(const std::string& mapDir, const adt::AdtLoadOptions& opt)
{
    // WMO-only maps do not issue tile requests, so preserve the prop toggle here for their
    // synchronous global-WMO load. Terrain requests carry the same option independently.
    includeWmoDoodads_ = opt.wmoDoodads;
    // Invalidate outstanding work, then tear down GPU state on the main thread.
    generation_.fetch_add(1);
    { std::lock_guard<std::mutex> lk(reqMutex_); requests_.clear(); }
    { std::lock_guard<std::mutex> lk(resMutex_); results_.clear(); }
    ClearAll();

    mapDir_ = mapDir;
    if (!cd_ || !adt::LoadWorldInfo(*cd_, mapDir, world_))
        return false;

    tileExists_.assign(64 * 64, 0);
    for (const auto& t : world_.tiles)
        if (t.first >= 0 && t.first < 64 && t.second >= 0 && t.second < 64)
            tileExists_[t.second * 64 + t.first] = 1;

    // Shared origin = center-of-existing-tiles' tile center (keeps coords small; float precision fine).
    if (world_.wmoOnly)
    {
        origin_ = glm::vec3(0.0f);
        LoadGlobalWmo();
        return true;
    }
    if (world_.tiles.empty())
        return true;
    long sx = 0, sy = 0;
    for (const auto& t : world_.tiles) { sx += t.first; sy += t.second; }
    const int rx = (int)(sx / (long)world_.tiles.size());
    const int ry = (int)(sy / (long)world_.tiles.size());
    glm::vec2 c = TileCenterWorld(rx, ry);
    origin_ = glm::vec3(c.x, c.y, 0.0f);

    // Frame the camera: sync-load the reference tile once for its real height (map-open only).
    adt::AdtTile ref;
    std::string err;
    if (adt::Load(*cd_, adt::TilePath(mapDir, rx, ry), ref, {}, &liquidTypes_, &err, &origin_))
    {
        startPos_ = ref.boundsCenter;
        startRadius_ = adt::kTileSize * 2.5f;
    }
    else
    {
        startPos_ = glm::vec3(0.0f, 0.0f, 150.0f);
        startRadius_ = adt::kTileSize * 2.5f;
    }
    LoadWdlTiles();
    return true;
}

// Build the low-detail distant-terrain meshes for the whole map (one coarse mesh per existing WDL
// tile). Reuses the mesh pipeline (hillshaded + height-tinted); drawn for tiles not streamed as ADTs.
void AdtStreamer::LoadWdlTiles()
{
    if (!renderer_ || world_.wmoOnly)
        return;
    adt::WdlData wdl;
    if (!adt::LoadWdl(*cd_, mapDir_, wdl))
        return;
    for (const adt::WdlTile& wt : wdl.tiles)
    {
        ModelUpload up = adt::BuildWdlUpload(wt, origin_);
        if (up.vertices.empty()) continue;
        LowTile lt;
        lt.handle = renderer_->CreateModel(up);
        if (!lt.handle) continue;
        int16_t lo = wt.heights[0], hi = wt.heights[0];
        for (int16_t h : wt.heights) { lo = std::min(lo, h); hi = std::max(hi, h); }
        lt.centerZ = 0.5f * (lo + hi);
        lt.radiusZ = 0.5f * (hi - lo);
        lowTiles_[Key(wt.x, wt.y)] = lt;
    }
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------
void AdtStreamer::WorkerLoop()
{
    for (;;)
    {
        Request r;
        {
            std::unique_lock<std::mutex> lk(reqMutex_);
            reqCv_.wait(lk, [this] { return stop_ || !requests_.empty(); });
            if (stop_) return;
            r = requests_.front();
            requests_.pop_front();
        }
        if (r.generation != generation_.load())
            continue;   // stale (map changed)
        TilePayload p;
        ProcessRequest(r, p);
        {
            std::lock_guard<std::mutex> lk(resMutex_);
            results_.push_back(std::move(p));
        }
    }
}

void AdtStreamer::DressModel(const std::string& path, m2::M2Model& model)
{
    if (!dresser_)
        return;
    // Lock-free: only take the dresser mutex if a runtime slot (non-zero type, empty path) needs
    // filling. `model` is this worker's private copy, so reading it unlocked is safe. Most static
    // doodads are type-0 only and skip the lock entirely.
    bool needs = false;
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
    {
        uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
        if (t != 0 && model.texturePaths[i].empty()) { needs = true; break; }
    }
    if (!needs)
        return;
    std::lock_guard<std::mutex> lk(dresserMutex_);
    dresser_->ResolveDefaultTextures(path, model);
}

void AdtStreamer::ProcessRequest(const Request& r, TilePayload& out)
{
    out.key = r.key;
    out.generation = r.generation;
    out.ok = false;

    adt::AdtTile tile;
    std::string err;
    if (!adt::Load(*cd_, adt::TilePath(mapDir_, KeyX(r.key), KeyY(r.key)), tile, r.opt, &liquidTypes_,
                   &err, &r.origin))
        return;   // out.ok stays false -> main marks the tile loaded-empty

    out.ok = true;
    std::unordered_set<std::string> texKnown;   // snapshot of already-cached ground textures
    { std::lock_guard<std::mutex> lk(knownMutex_); texKnown = terrainTexKnown_; }
    out.terrain = adt::BuildTerrainUpload(*cd_, tile, &texKnown);
    if (r.opt.liquid)
    {
        out.liquid = adt::BuildLiquidUpload(*cd_, tile);
        out.liquidSubmeshes = tile.liquidSubmeshes;
    }

    // Placement refs (all objects on this tile) + unique new models (skip already-loaded paths).
    // WMO shells and their internal MODD M2s share the same path registry so a prop that also
    // appears as an ADT MDDF only decodes/uploads once.
    std::unordered_set<std::string> tileModelPaths;
    auto queueM2 = [&](const std::string& path) {
        const std::string key = LowerStr(path);
        if (!tileModelPaths.insert(key).second)
            return;
        {
            std::lock_guard<std::mutex> lk(knownMutex_);
            if (knownPaths_.count(key))
                return;   // main already has it
        }
        ModelPayload mp;
        mp.path = path;
        auto m2 = std::make_shared<m2::M2Model>();
        if (!m2::Load(*cd_, path, *m2, nullptr))
            return;
        DressModel(path, *m2);   // resolve runtime skins so texture-variation doodads aren't white
        mp.upload = m2::BuildUpload(*cd_, *m2);
        mp.hasEmitters = !m2->particleEmitters.empty() || !m2->ribbonEmitters.empty();
        mp.boundsCenter = m2->boundsCenter;
        mp.boundsRadius = m2->boundsRadius;
        mp.m2 = std::move(m2);
        out.models.push_back(std::move(mp));
    };
    auto queueWmoShell = [&](const std::string& path) {
        const std::string key = LowerStr(path);
        if (!tileModelPaths.insert(key).second)
            return;
        {
            std::lock_guard<std::mutex> lk(knownMutex_);
            if (knownPaths_.count(key))
                return;   // main already has it
        }
        ModelPayload mp;
        mp.path = path;
        mp.isWmo = true;
        wmo::WmoModel wm;
        wmo::WmoLoadOptions wopt;
        // Internal M2s are emitted below as independent child refs. Keeping them out of the
        // shell upload means each unique M2 remains animated/cullable/instancable by its own path.
        wopt.doodads = false;
        wopt.liquid = r.opt.liquid;
        if (!wmo::Load(*cd_, path, wm, wopt, &liquidTypes_, nullptr))
            return;
        mp.upload = wmo::BuildUpload(*cd_, wm);
        for (ModelVertexGpu& v : mp.upload.vertices)
        {
            v.boneIndices[0] = 0;
            v.boneWeights[0] = 1.0f;
        }
        mp.upload.boneCount = 1;   // shell rides bone 0 = the placement matrix
        mp.boundsCenter = wm.boundsCenter;
        mp.boundsRadius = wm.boundsRadius;
        out.models.push_back(std::move(mp));
    };

    // Repeated MODF records commonly reference the same root WMO and doodad set. Resolve a set
    // once per worker payload, then compose each placement later on the main thread against its
    // current root transform. A different MODF doodadSet gets its own correct resolution.
    std::unordered_map<std::string, std::vector<wmo::WmoDoodadInstance>> doodadSetCache;
    for (const adt::AdtPlacement& pl : tile.placements)
    {
        PlacementRef pr;
        pr.uid = pl.uniqueId;
        pr.path = pl.path;
        pr.isWmo = pl.isWmo;
        std::memcpy(&pr.transform[0][0], pl.transform, sizeof(pl.transform));
        pr.origin = glm::vec3(pl.origin[0], pl.origin[1], pl.origin[2]);
        out.placements.push_back(std::move(pr));

        if (!pl.isWmo)
        {
            queueM2(pl.path);
            continue;
        }

        queueWmoShell(pl.path);
        if (!r.opt.wmoDoodads)
            continue;

        const std::string setKey = LowerStr(pl.path) + "#" + std::to_string(pl.doodadSet);
        auto cacheIt = doodadSetCache.find(setKey);
        if (cacheIt == doodadSetCache.end())
        {
            std::vector<wmo::WmoDoodadInstance> instances;
            // Read only MODS/MODN/MODD from the WMO root. Re-parsing all WMO group geometry for
            // every placed MODF would make a prop-rich city hitch even though the shell already
            // has its own one-per-path worker upload above.
            wmo::LoadDoodadInstances(*cd_, pl.path, pl.doodadSet, instances, nullptr);
            cacheIt = doodadSetCache.emplace(setKey, std::move(instances)).first;
        }

        const std::vector<wmo::WmoDoodadInstance>& instances = cacheIt->second;
        for (size_t index = 0; index < instances.size(); ++index)
        {
            const wmo::WmoDoodadInstance& inst = instances[index];
            if (inst.m2Path.empty())
                continue;
            EmbeddedDoodadRef ref;
            ref.parentUid = pl.uniqueId;
            ref.instanceIndex = static_cast<uint32_t>(index);
            ref.path = inst.m2Path;
            std::memcpy(&ref.localTransform[0][0], inst.transform, sizeof(inst.transform));
            ref.localOrigin = glm::vec3(inst.origin[0], inst.origin[1], inst.origin[2]);
            ref.tint = wmo::DoodadTint(inst.color);
            out.embeddedDoodads.push_back(std::move(ref));
            queueM2(inst.m2Path);
        }
    }
}

// ---------------------------------------------------------------------------
// Main thread
// ---------------------------------------------------------------------------
void AdtStreamer::Update(const glm::vec3& camPos, int radius, const adt::AdtLoadOptions& opt)
{
    if (world_.wmoOnly || !renderer_)
        return;

    // 1) Consume up to a budget of finished payloads (create GPU resources).
    for (int n = 0; n < kConsumePerFrame; ++n)
    {
        TilePayload p;
        {
            std::lock_guard<std::mutex> lk(resMutex_);
            if (results_.empty()) break;
            p = std::move(results_.front());
            results_.pop_front();
        }
        ConsumePayload(std::move(p));
    }

    // 2) Camera tile.
    const glm::vec2 camWorld(camPos.x + origin_.x, camPos.y + origin_.y);
    const int cx = (int)std::lround(31.5f - camWorld.y / adt::kTileSize);
    const int cy = (int)std::lround(31.5f - camWorld.x / adt::kTileSize);

    // 3) Evict tiles beyond radius+1 (hysteresis).
    const int evictR = radius + 1;
    std::vector<uint32_t> toEvict;
    for (const auto& kv : tiles_)
    {
        const int tx = KeyX(kv.first), ty = KeyY(kv.first);
        if (std::max(std::abs(tx - cx), std::abs(ty - cy)) > evictR)
            toEvict.push_back(kv.first);
    }
    for (uint32_t k : toEvict) EvictTile(k);

    // 4) Enqueue missing tiles within radius, nearest-first (bounded per frame).
    struct Cand { int x, y, d2; };
    std::vector<Cand> cands;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
        {
            const int tx = cx + dx, ty = cy + dy;
            if (!TileExists(tx, ty)) continue;
            const uint32_t k = Key(tx, ty);
            if (tiles_.count(k) || inFlight_.count(k)) continue;
            cands.push_back({tx, ty, dx * dx + dy * dy});
        }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
    const uint64_t gen = generation_.load();
    int posted = 0;
    {
        std::lock_guard<std::mutex> lk(reqMutex_);
        for (const Cand& c : cands)
        {
            if (posted >= kEnqueuePerFrame) break;
            const uint32_t k = Key(c.x, c.y);
            inFlight_.insert(k);
            requests_.push_back({k, gen, origin_, opt});
            ++posted;
        }
    }
    if (posted) reqCv_.notify_all();
}

void AdtStreamer::ConsumePayload(TilePayload&& p)
{
    inFlight_.erase(p.key);
    if (p.generation != generation_.load())
        return;   // map changed while loading
    if (!p.ok)
    {
        tiles_[p.key] = LoadedTile{};   // remember as loaded-empty so we don't re-request
        return;
    }

    LoadedTile lt;
    // Real Z bounds of this tile's terrain (local frame) so the frustum-cull sphere sits at the
    // tile's actual height, not a fixed guess — otherwise tiles pop as the camera flies over relief.
    // Also retain a light CPU heightmesh (positions + indices) for ray-vs-ground picking.
    if (!p.terrain.vertices.empty())
    {
        float minZ = p.terrain.vertices[0].pos[2], maxZ = minZ;
        lt.terrainVerts.reserve(p.terrain.vertices.size());
        for (const ModelVertexGpu& v : p.terrain.vertices)
        {
            minZ = std::min(minZ, v.pos[2]);
            maxZ = std::max(maxZ, v.pos[2]);
            lt.terrainVerts.emplace_back(v.pos[0], v.pos[1], v.pos[2]);
        }
        lt.centerZ = 0.5f * (minZ + maxZ);
        lt.radiusZ = 0.5f * (maxZ - minZ);
        lt.terrainIndices = p.terrain.indices;
    }
    lt.terrain = renderer_->CreateTerrain(p.terrain);
    // Publish this tile's ground textures as cached so other workers stop decoding them.
    {
        std::lock_guard<std::mutex> lk(knownMutex_);
        for (const std::string& tp : p.terrain.texturePaths)
            if (!tp.empty()) terrainTexKnown_.insert(tp);
    }
    if (!p.liquid.vertices.empty())
        lt.liquid = renderer_->CreateModel(p.liquid);
    lt.liquidSubmeshes = std::move(p.liquidSubmeshes);

    // Create any genuinely-new models (dedup by path on the main thread).
    for (ModelPayload& mp : p.models)
    {
        const std::string key = LowerStr(mp.path);
        if (models_.count(key)) continue;
        if (mp.upload.vertices.empty()) continue;   // skipped/failed
        ModelEntry e;
        e.isWmo = mp.isWmo;
        e.handle = renderer_->CreateModel(mp.upload);
        if (!e.handle) continue;
        e.boundsCenter = mp.boundsCenter;
        e.boundsRadius = mp.boundsRadius;
        // Model-local AABB over the uploaded vertices, for tight ray-vs-OBB picking.
        glm::vec3 bmin(1e30f), bmax(-1e30f);
        for (const ModelVertexGpu& v : mp.upload.vertices)
        {
            bmin = glm::min(bmin, glm::vec3(v.pos[0], v.pos[1], v.pos[2]));
            bmax = glm::max(bmax, glm::vec3(v.pos[0], v.pos[1], v.pos[2]));
        }
        e.boundsMin = bmin;
        e.boundsMax = bmax;
        e.hasEmitters = mp.hasEmitters;
        if (!mp.isWmo && mp.m2)
        {
            e.m2 = std::move(mp.m2);
            e.animator.SetModel(e.m2.get(), cd_, mp.path);
            e.animator.Evaluate(0, 0.0f, glm::mat4(1.0f), e.localBones);   // bind pose
        }
        else
        {
            e.localBones = {glm::mat4(1.0f)};   // WMO: 1-bone identity palette (rides the instance matrix)
        }
        models_[key] = std::move(e);
        { std::lock_guard<std::mutex> lk(knownMutex_); knownPaths_.insert(key); }
    }

    // Register objects, deduped by uniqueId; refcount by tile.
    for (PlacementRef& pr : p.placements)
    {
        const std::string key = LowerStr(pr.path);
        auto oit = objects_.find(pr.uid);
        if (oit != objects_.end())
        {
            oit->second.refTiles.insert(p.key);   // spanning object already loaded — just add a ref
            lt.objectUids.push_back(pr.uid);
            continue;
        }
        auto mit = models_.find(key);
        if (mit == models_.end()) continue;   // model unavailable (rare evict race) — skip
        WorldObject obj;
        obj.modelPath = key;
        obj.transform = pr.transform;
        obj.origin = pr.origin;
        UpdateObjectBounds(obj);
        if (mit->second.hasEmitters && mit->second.m2)
        {
            obj.effects = std::make_unique<m2::M2EffectSystem>();
            obj.effects->SetModel(mit->second.m2.get(), &mit->second.animator);
        }
        obj.refTiles.insert(p.key);
        ++mit->second.refCount;
        objects_[pr.uid] = std::move(obj);
        lt.objectUids.push_back(pr.uid);
    }

    // The parent roots are registered first so every MODD ref can bind to the actual current
    // transform (including a root changed in this session before a spanning tile completed).
    for (const EmbeddedDoodadRef& ref : p.embeddedDoodads)
        AttachEmbeddedDoodad(ref, p.key, &lt);

    tiles_[p.key] = std::move(lt);
}

void AdtStreamer::EvictTile(uint32_t key)
{
    auto it = tiles_.find(key);
    if (it == tiles_.end()) return;
    LoadedTile& lt = it->second;
    if (lt.terrain) renderer_->DestroyTerrain(lt.terrain);
    if (lt.liquid) renderer_->DestroyModel(lt.liquid);
    for (uint64_t uid : lt.objectUids)
    {
        auto oit = objects_.find(uid);
        if (oit == objects_.end()) continue;
        oit->second.refTiles.erase(key);
        if (!oit->second.refTiles.empty()) continue;   // still referenced by another loaded tile
        auto mit = models_.find(oit->second.modelPath);
        if (mit != models_.end() && --mit->second.refCount <= 0)
        {
            if (mit->second.handle) renderer_->DestroyModel(mit->second.handle);
            { std::lock_guard<std::mutex> lk(knownMutex_); knownPaths_.erase(oit->second.modelPath); }
            models_.erase(mit);
        }
        if (oit->second.embeddedWmoDoodad)
        {
            auto parent = objects_.find(oit->second.parentUid);
            if (parent != objects_.end())
            {
                auto& children = parent->second.embeddedChildren;
                children.erase(std::remove(children.begin(), children.end(), uid), children.end());
            }
            embeddedWmoDoodadCount_ = std::max(0, embeddedWmoDoodadCount_ - 1);
        }
        objects_.erase(oit);
    }
    tiles_.erase(it);
}


bool AdtStreamer::EnsureM2ModelLoaded(const std::string& path, ModelEntry*& out)
{
    out = nullptr;
    if (!cd_ || !renderer_ || path.empty())
        return false;

    const std::string key = LowerStr(path);
    auto existing = models_.find(key);
    if (existing != models_.end())
    {
        if (existing->second.isWmo)
            return false;   // a malformed client path collision; never treat a WMO shell as an M2 prop
        out = &existing->second;
        return true;
    }

    auto m2 = std::make_shared<m2::M2Model>();
    if (!m2::Load(*cd_, path, *m2, nullptr))
        return false;
    DressModel(path, *m2);
    ModelUpload upload = m2::BuildUpload(*cd_, *m2);
    if (upload.vertices.empty())
        return false;

    ModelEntry entry;
    entry.isWmo = false;
    entry.m2 = std::move(m2);
    entry.boundsCenter = entry.m2->boundsCenter;
    entry.boundsRadius = entry.m2->boundsRadius;
    entry.hasEmitters = !entry.m2->particleEmitters.empty() || !entry.m2->ribbonEmitters.empty();
    entry.boundsMin = glm::vec3(1e30f);
    entry.boundsMax = glm::vec3(-1e30f);
    for (const ModelVertexGpu& vertex : upload.vertices)
    {
        const glm::vec3 p(vertex.pos[0], vertex.pos[1], vertex.pos[2]);
        entry.boundsMin = glm::min(entry.boundsMin, p);
        entry.boundsMax = glm::max(entry.boundsMax, p);
    }
    entry.handle = renderer_->CreateModel(upload);
    if (!entry.handle)
        return false;
    entry.animator.SetModel(entry.m2.get(), cd_, path);
    entry.animator.Evaluate(0, 0.0f, glm::mat4(1.0f), entry.localBones);

    auto inserted = models_.emplace(key, std::move(entry));
    out = &inserted.first->second;
    { std::lock_guard<std::mutex> lk(knownMutex_); knownPaths_.insert(key); }
    return true;
}

void AdtStreamer::UpdateObjectBounds(WorldObject& object)
{
    const auto model = models_.find(object.modelPath);
    if (model == models_.end())
        return;
    object.cullCenter = glm::vec3(object.transform * glm::vec4(model->second.boundsCenter, 1.0f));
    object.cullRadius = model->second.boundsRadius * glm::length(glm::vec3(object.transform[0])) + 0.01f;
}

void AdtStreamer::UpdateEmbeddedChildren(WorldObject& parent)
{
    for (uint64_t childUid : parent.embeddedChildren)
    {
        auto child = objects_.find(childUid);
        if (child == objects_.end() || !child->second.embeddedWmoDoodad)
            continue;
        WorldObject& prop = child->second;
        prop.transform = wmo::ComposeDoodadTransform(parent.transform, prop.localToParent);
        prop.origin = glm::vec3(prop.transform[3]);
        UpdateObjectBounds(prop);
    }
}

bool AdtStreamer::AttachEmbeddedDoodad(const EmbeddedDoodadRef& ref, uint32_t tileKey,
                                       LoadedTile* ownerTile)
{
    auto parent = objects_.find(ref.parentUid);
    if (parent == objects_.end())
        return false;   // root WMO could not be uploaded; never orphan a prop in the scene

    const uint64_t childUid = wmo::MakeEmbeddedDoodadUid(ref.parentUid, ref.instanceIndex);
    auto existing = objects_.find(childUid);
    if (existing != objects_.end())
    {
        // A spanning MODF repeats identically in neighbouring ADTs. It adds a tile reference to
        // the same child rather than creating duplicate furniture/torches at the seam.
        if (!existing->second.embeddedWmoDoodad || existing->second.parentUid != ref.parentUid)
            return false;
        existing->second.refTiles.insert(tileKey);
        if (ownerTile && std::find(ownerTile->objectUids.begin(), ownerTile->objectUids.end(), childUid) ==
                             ownerTile->objectUids.end())
            ownerTile->objectUids.push_back(childUid);
        return true;
    }

    ModelEntry* model = nullptr;
    if (!EnsureM2ModelLoaded(ref.path, model) || !model)
        return false;

    WorldObject prop;
    prop.modelPath = LowerStr(ref.path);
    prop.localToParent = ref.localTransform;
    prop.transform = wmo::ComposeDoodadTransform(parent->second.transform, prop.localToParent);
    prop.origin = glm::vec3(parent->second.transform * glm::vec4(ref.localOrigin, 1.0f));
    prop.instanceTint = ref.tint;
    prop.embeddedWmoDoodad = true;
    prop.parentUid = ref.parentUid;
    prop.refTiles.insert(tileKey);
    UpdateObjectBounds(prop);
    if (model->hasEmitters && model->m2)
    {
        prop.effects = std::make_unique<m2::M2EffectSystem>();
        prop.effects->SetModel(model->m2.get(), &model->animator);
    }

    ++model->refCount;
    objects_.emplace(childUid, std::move(prop));
    // `objects_` can rehash above, so reacquire the parent iterator before updating its hierarchy.
    parent = objects_.find(ref.parentUid);
    if (parent != objects_.end())
    {
        auto& children = parent->second.embeddedChildren;
        if (std::find(children.begin(), children.end(), childUid) == children.end())
            children.push_back(childUid);
    }
    ++embeddedWmoDoodadCount_;
    if (ownerTile)
        ownerTile->objectUids.push_back(childUid);
    return true;
}

void AdtStreamer::ReleaseObject(uint64_t uid, bool detachFromLoadedTiles)
{
    auto object = objects_.find(uid);
    if (object == objects_.end())
        return;

    if (detachFromLoadedTiles)
    {
        for (uint32_t tileKey : object->second.refTiles)
        {
            auto tile = tiles_.find(tileKey);
            if (tile == tiles_.end())
                continue;
            auto& objectUids = tile->second.objectUids;
            objectUids.erase(std::remove(objectUids.begin(), objectUids.end(), uid), objectUids.end());
        }
    }

    if (object->second.embeddedWmoDoodad)
    {
        auto parent = objects_.find(object->second.parentUid);
        if (parent != objects_.end())
        {
            auto& children = parent->second.embeddedChildren;
            children.erase(std::remove(children.begin(), children.end(), uid), children.end());
        }
        embeddedWmoDoodadCount_ = std::max(0, embeddedWmoDoodadCount_ - 1);
    }

    auto model = models_.find(object->second.modelPath);
    if (model != models_.end() && --model->second.refCount <= 0)
    {
        if (model->second.handle)
            renderer_->DestroyModel(model->second.handle);
        { std::lock_guard<std::mutex> lk(knownMutex_); knownPaths_.erase(object->second.modelPath); }
        models_.erase(model);
    }
    objects_.erase(object);
}

void AdtStreamer::LoadGlobalWmo()
{
    if (!renderer_ || !cd_ || world_.globalWmo.empty())
        return;

    wmo::WmoModel wm;
    wmo::WmoLoadOptions wopt;
    wopt.doodads = includeWmoDoodads_;
    wopt.doodadSet = world_.hasGlobalPlacement ? static_cast<int>(world_.globalPlacement.doodadSet) : -1;
    std::string err;
    if (!wmo::Load(*cd_, world_.globalWmo, wm, wopt, &liquidTypes_, &err))
        return;

    ModelUpload up = wmo::BuildUpload(*cd_, wm);
    for (ModelVertexGpu& v : up.vertices)
    {
        v.boneIndices[0] = 0;
        v.boneWeights[0] = 1.0f;
    }
    up.boneCount = 1;
    ModelEntry entry;
    entry.isWmo = true;
    entry.handle = renderer_->CreateModel(up);
    if (!entry.handle)
        return;
    entry.boundsCenter = wm.boundsCenter;
    entry.boundsRadius = wm.boundsRadius;
    entry.boundsMin = glm::vec3(1e30f);
    entry.boundsMax = glm::vec3(-1e30f);
    for (const ModelVertexGpu& vertex : up.vertices)
    {
        const glm::vec3 p(vertex.pos[0], vertex.pos[1], vertex.pos[2]);
        entry.boundsMin = glm::min(entry.boundsMin, p);
        entry.boundsMax = glm::max(entry.boundsMax, p);
    }
    entry.localBones = {glm::mat4(1.0f)};
    entry.refCount = 1;
    models_["__global__"] = std::move(entry);

    // Placement matrix from the WDT MODF (same convention as ADT MODF), around origin 0.
    WorldObject obj;
    obj.modelPath = "__global__";
    const auto& g = world_.globalPlacement;
    glm::mat4 M(1.0f);
    // World position (Y-up placement -> our Z-up frame), matching AdtLoader::PlacementMatrix.
    glm::vec3 world(adt::kMapOrigin - g.position[2], adt::kMapOrigin - g.position[0], g.position[1]);
    M = glm::translate(M, world);
    M = glm::rotate(M, glm::radians(g.rotation[1] + 180.0f), glm::vec3(0, 0, 1));
    M = glm::rotate(M, glm::radians(g.rotation[0]), glm::vec3(0, 1, 0));
    M = glm::rotate(M, glm::radians(g.rotation[2]), glm::vec3(1, 0, 0));
    obj.transform = M;
    obj.origin = world;
    obj.refTiles.insert(0);   // WMO-only map lifetime reference; no ADT tile owns it
    UpdateObjectBounds(obj);
    objects_.emplace(1, std::move(obj));

    // Global WMO interiors use the same MODD child model path as terrain-map placements. The
    // children are attached after the root so click/transform/outline behavior remains rooted at
    // the single WDT MODF object.
    if (includeWmoDoodads_)
    {
        for (size_t index = 0; index < wm.doodadInstances.size(); ++index)
        {
            const wmo::WmoDoodadInstance& inst = wm.doodadInstances[index];
            if (inst.m2Path.empty())
                continue;
            EmbeddedDoodadRef ref;
            ref.parentUid = 1;
            ref.instanceIndex = static_cast<uint32_t>(index);
            ref.path = inst.m2Path;
            std::memcpy(&ref.localTransform[0][0], inst.transform, sizeof(inst.transform));
            ref.localOrigin = glm::vec3(inst.origin[0], inst.origin[1], inst.origin[2]);
            ref.tint = wmo::DoodadTint(inst.color);
            AttachEmbeddedDoodad(ref, 0, nullptr);
        }
    }

    const auto root = objects_.find(1);
    if (root != objects_.end())
    {
        startPos_ = root->second.cullCenter;
        startRadius_ = glm::max(wm.boundsRadius, 20.0f);
    }
}

// ---------------------------------------------------------------------------
uint64_t AdtStreamer::PickObject(const glm::vec3& ro, const glm::vec3& rd, float& outDist) const
{
    uint64_t best = 0;
    float bestT = 1e30f;
    for (const auto& kv : objects_)
    {
        const WorldObject& o = kv.second;
        auto mit = models_.find(o.modelPath);
        if (mit == models_.end())
            continue;
        // Tight ray-vs-oriented-box against the model's local AABB placed by its transform. A
        // bounding sphere here is far too coarse on a dense map (big doodad/WMO spheres steal clicks).
        const float t = RayObb(ro, rd, o.transform, mit->second.boundsMin, mit->second.boundsMax);
        if (t < 0.0f || t >= bestT)
            continue;

        // Internal MODD props are real geometry for accurate picking, but the authoring target is
        // always their root MODF. That preserves ADT save semantics and makes a click on a torch or
        // chair select/move/highlight the whole placed WMO instead of inventing an MDDF record.
        uint64_t selectableUid = kv.first;
        if (o.embeddedWmoDoodad)
        {
            const auto parent = objects_.find(o.parentUid);
            if (parent == objects_.end())
                continue;
            selectableUid = o.parentUid;
        }
        bestT = t;
        best = selectableUid;
    }
    outDist = best ? bestT : -1.0f;
    return best;
}

bool AdtStreamer::GroundHit(const glm::vec3& ro, const glm::vec3& rd, glm::vec3& outLocal,
                            float& outDist, int& outTileX, int& outTileY) const
{
    const float tileR = adt::kTileSize * 0.72f;   // ~half tile diagonal (matches BuildFrame)
    float bestT = 1e30f;
    bool hit = false;
    for (const auto& kv : tiles_)
    {
        const LoadedTile& lt = kv.second;
        if (lt.terrainIndices.empty())
            continue;
        const int tx = KeyX(kv.first), ty = KeyY(kv.first);
        const glm::vec2 cwv = TileCenterWorld(tx, ty);
        const glm::vec3 tc(cwv.x - origin_.x, cwv.y - origin_.y, lt.centerZ);
        if (RaySphere(ro, rd, tc, tileR + lt.radiusZ + 100.0f) < 0.0f)
            continue;   // broad-phase reject
        const std::vector<glm::vec3>& v = lt.terrainVerts;
        const std::vector<uint32_t>& idx = lt.terrainIndices;
        for (size_t i = 0; i + 2 < idx.size(); i += 3)
        {
            const uint32_t i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];
            if (i0 >= v.size() || i1 >= v.size() || i2 >= v.size())
                continue;
            const float t = RayTriangle(ro, rd, v[i0], v[i1], v[i2]);
            if (t >= 0.0f && t < bestT)
            {
                bestT = t;
                outTileX = tx;
                outTileY = ty;
                hit = true;
            }
        }
    }
    if (!hit)
        return false;
    outDist = bestT;
    outLocal = ro + rd * bestT;
    return true;
}

bool AdtStreamer::ObjectTransform(uint64_t uid, glm::mat4& out) const
{
    auto it = objects_.find(uid);
    if (it == objects_.end() || it->second.embeddedWmoDoodad)
        return false;
    out = it->second.transform;
    return true;
}

bool AdtStreamer::SetObjectTransform(uint64_t uid, const glm::mat4& m)
{
    auto it = objects_.find(uid);
    if (it == objects_.end() || it->second.embeddedWmoDoodad)
        return false;
    WorldObject& o = it->second;
    o.transform = m;
    o.origin = glm::vec3(m[3]);
    // Recompute the cull sphere from the model bounds (same math as ConsumePayload), so the moved
    // object isn't culled at its old position. Embedded MODD props inherit the exact same root
    // transform immediately, before an artist sees the next World Editor frame.
    UpdateObjectBounds(o);
    UpdateEmbeddedChildren(o);
    return true;
}

std::string AdtStreamer::ObjectModelPath(uint64_t uid) const
{
    auto it = objects_.find(uid);
    return (it == objects_.end() || it->second.embeddedWmoDoodad) ? std::string() : it->second.modelPath;
}

bool AdtStreamer::AddObject(const std::string& path, bool isWmo, const glm::mat4& localTransform,
                            int tileX, int tileY, uint64_t uniqueId)
{
    if (!cd_ || !renderer_ || path.empty() || wmo::IsEmbeddedDoodadUid(uniqueId) || objects_.count(uniqueId))
        return false;
    const std::string key = LowerStr(path);

    // AdtWriter initializes a newly added MODF with doodadSet 0. Mirror that persisted value in
    // the live preview (rather than choosing a non-repeatable automatic local set); existing client
    // MODFs likewise use their exact on-disk selector in the streamed worker path.
    wmo::WmoModel placedWmo;
    bool havePlacedWmo = false;
    auto loadPlacedWmo = [&]() {
        if (havePlacedWmo)
            return true;
        wmo::WmoLoadOptions wopt;
        wopt.doodads = includeWmoDoodads_;
        wopt.doodadSet = 0;   // matches the default field written by AdtWriter::AddPlacement
        wopt.liquid = false;
        if (!wmo::Load(*cd_, path, placedWmo, wopt, nullptr, nullptr))
            return false;
        havePlacedWmo = true;
        return true;
    };

    auto mit = models_.find(key);
    if (mit == models_.end())
    {
        if (!isWmo)
        {
            ModelEntry* loaded = nullptr;
            if (!EnsureM2ModelLoaded(path, loaded) || !loaded)
                return false;
            mit = models_.find(key);
        }
        else
        {
            if (!loadPlacedWmo())
                return false;
            ModelUpload upload = wmo::BuildUpload(*cd_, placedWmo);
            for (ModelVertexGpu& vertex : upload.vertices)
            {
                vertex.boneIndices[0] = 0;
                vertex.boneWeights[0] = 1.0f;
            }
            upload.boneCount = 1;
            if (upload.vertices.empty())
                return false;

            ModelEntry entry;
            entry.isWmo = true;
            entry.boundsCenter = placedWmo.boundsCenter;
            entry.boundsRadius = placedWmo.boundsRadius;
            entry.boundsMin = glm::vec3(1e30f);
            entry.boundsMax = glm::vec3(-1e30f);
            for (const ModelVertexGpu& vertex : upload.vertices)
            {
                const glm::vec3 p(vertex.pos[0], vertex.pos[1], vertex.pos[2]);
                entry.boundsMin = glm::min(entry.boundsMin, p);
                entry.boundsMax = glm::max(entry.boundsMax, p);
            }
            entry.localBones = {glm::mat4(1.0f)};
            entry.handle = renderer_->CreateModel(upload);
            if (!entry.handle)
                return false;
            { std::lock_guard<std::mutex> lk(knownMutex_); knownPaths_.insert(key); }
            mit = models_.emplace(key, std::move(entry)).first;
        }
    }
    if (mit == models_.end() || mit->second.isWmo != isWmo)
        return false;

    // Register the root world object; attach it to the tile so eviction cleans it up.
    WorldObject obj;
    obj.modelPath = key;
    obj.transform = localTransform;
    obj.origin = glm::vec3(localTransform[3]);
    obj.refTiles.insert(Key(tileX, tileY));
    UpdateObjectBounds(obj);
    if (mit->second.hasEmitters && mit->second.m2)
    {
        obj.effects = std::make_unique<m2::M2EffectSystem>();
        obj.effects->SetModel(mit->second.m2.get(), &mit->second.animator);
    }
    const uint32_t tileKey = Key(tileX, tileY);
    ++mit->second.refCount;
    objects_.emplace(uniqueId, std::move(obj));
    auto tile = tiles_.find(tileKey);
    if (tile != tiles_.end())
        tile->second.objectUids.push_back(uniqueId);

    if (isWmo && includeWmoDoodads_ && (havePlacedWmo || loadPlacedWmo()))
    {
        for (size_t index = 0; index < placedWmo.doodadInstances.size(); ++index)
        {
            const wmo::WmoDoodadInstance& inst = placedWmo.doodadInstances[index];
            if (inst.m2Path.empty())
                continue;
            EmbeddedDoodadRef ref;
            ref.parentUid = uniqueId;
            ref.instanceIndex = static_cast<uint32_t>(index);
            ref.path = inst.m2Path;
            std::memcpy(&ref.localTransform[0][0], inst.transform, sizeof(inst.transform));
            ref.localOrigin = glm::vec3(inst.origin[0], inst.origin[1], inst.origin[2]);
            ref.tint = wmo::DoodadTint(inst.color);
            AttachEmbeddedDoodad(ref, tileKey, tile != tiles_.end() ? &tile->second : nullptr);
        }
    }
    return true;
}

bool AdtStreamer::RemoveObject(uint64_t uid)
{
    auto object = objects_.find(uid);
    if (object == objects_.end() || object->second.embeddedWmoDoodad)
        return false;

    // Delete an editable WMO root and its transient props as one scene operation. Releasing each
    // child removes it from every loaded spanning tile and drops its independently ref-counted M2.
    const std::vector<uint64_t> children = object->second.embeddedChildren;
    for (uint64_t childUid : children)
        ReleaseObject(childUid, true);
    ReleaseObject(uid, true);
    return true;
}

bool AdtStreamer::ObjectSaveInfo(uint64_t uid, glm::mat4& transform, bool& isWmo,
                                 std::vector<std::pair<int, int>>& tiles) const
{
    auto it = objects_.find(uid);
    if (it == objects_.end() || it->second.embeddedWmoDoodad)
        return false;
    transform = it->second.transform;
    auto mit = models_.find(it->second.modelPath);
    isWmo = (mit != models_.end()) && mit->second.isWmo;
    tiles.clear();
    for (uint32_t key : it->second.refTiles)
        tiles.emplace_back(KeyX(key), KeyY(key));
    return true;
}

void AdtStreamer::BuildFrame(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& eye,
                             float dtMs, std::vector<TerrainHandle>& outTerrains,
                             std::vector<InstancedGroup>& outInstanced,
                             std::vector<SceneInstanceGpu>& outScene)
{
    outTerrains.clear();
    outInstanced.clear();
    outScene.clear();
    instXforms_.clear();
    animTime_ += dtMs;
    liquidTime_ += dtMs * 0.001f;
    const glm::mat4 vp = proj * view;
    const Frustum frustum(vp);   // extract + normalize the 6 planes ONCE, not per object
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);
    // Screen-size cull: an object's projected half-height (fraction of the viewport) is
    // ~ cullRadius * |proj[1][1]| / distance. |.| because proj[1][1] is negated for Vulkan's
    // flipped Y. Drop objects below minScreenFrac_ (sub-pixel doodads).
    const float projY = std::fabs(proj[1][1]);

    for (auto& kv : models_) kv.second.animatedThisFrame = false;

    // Terrains + liquids from loaded tiles (frustum-culled per tile — at radius 4 that's the
    // difference between drawing 81 tiles and only the handful actually in view).
    const float tileR = adt::kTileSize * 0.72f;   // ~half tile diagonal
    for (auto& kv : tiles_)
    {
        LoadedTile& lt = kv.second;
        const glm::vec2 cwv = TileCenterWorld(KeyX(kv.first), KeyY(kv.first));
        const glm::vec3 tc(cwv.x - origin_.x, cwv.y - origin_.y, lt.centerZ);
        // Sphere bounds the tile's real XY footprint + its actual Z relief (+slack for tall doodads
        // that overhang the terrain), so a tile is never culled while the camera is right over it.
        if (!SphereInFrustum(frustum, tc, tileR + lt.radiusZ + 100.0f))
            continue;
        if (lt.terrain) outTerrains.push_back(lt.terrain);
        if (lt.liquid && !lt.liquidSubmeshes.empty())
        {
            lt.liquidAnims.assign(lt.liquidSubmeshes.size(), SubmeshAnim{});
            const int frame = (int)(liquidTime_ * 18.0f);
            for (size_t i = 0; i < lt.liquidSubmeshes.size(); ++i)
            {
                const adt::AdtSubmesh& sm = lt.liquidSubmeshes[i];
                if (sm.isLiquid && sm.liquidFrameBase >= 0 && sm.liquidFrameCount > 0)
                    lt.liquidAnims[i].textureOverride = sm.liquidFrameBase + (frame % sm.liquidFrameCount);
            }
            SceneInstanceGpu si{};
            si.handle = lt.liquid;
            si.submeshAnims = lt.liquidAnims.data();
            si.submeshAnimCount = (int)lt.liquidAnims.size();
            outScene.push_back(si);
        }
    }

    // Distant WDL terrain: draw each low-detail tile that is NOT currently a loaded ADT (avoids
    // z-fighting / double-draw), frustum-culled. Rendered on the mesh path with an identity palette.
    if (showWdl_)
    {
        const float* pal = reinterpret_cast<const float*>(lowPalette_.data());
        for (auto& kv : lowTiles_)
        {
            if (tiles_.count(kv.first)) continue;   // a full ADT covers this tile
            const LowTile& lt = kv.second;
            if (!lt.handle) continue;
            const glm::vec2 cwv = TileCenterWorld(KeyX(kv.first), KeyY(kv.first));
            const glm::vec3 tc(cwv.x - origin_.x, cwv.y - origin_.y, lt.centerZ);
            if (!SphereInFrustum(frustum, tc, tileR + lt.radiusZ + 50.0f))
                continue;
            SceneInstanceGpu si{};
            si.handle = lt.handle;
            si.boneMatrices = pal;
            si.boneCount = 1;
            outScene.push_back(si);
        }
    }

    // Animate a unique model's shared palette + per-batch UV/color exactly once per frame. WMOs
    // keep the {identity} palette set at load; only M2s evaluate their animation.
    auto ensureAnimated = [&](ModelEntry& e)
    {
        if (e.isWmo || e.animatedThisFrame || !e.m2) return;
        float dur = (float)e.animator.Duration(0);
        float t = dur > 0 ? std::fmod(animTime_, dur) : 0.0f;
        e.animator.Evaluate(0, t, view, e.localBones);
        e.subAnims.resize(e.m2->batches.size());
        for (size_t i = 0; i < e.m2->batches.size(); ++i)
        {
            const m2::RenderBatch& b = e.m2->batches[i];
            glm::mat4 tmx = e.animator.TextureMatrix(b.textureTransformIndex, 0, t);
            std::memcpy(e.subAnims[i].texMatrix, &tmx[0][0], sizeof(e.subAnims[i].texMatrix));
            glm::vec4 col = e.animator.BatchColor(b.colorIndex, b.textureWeightIndex, 0, t);
            e.subAnims[i].color[0] = col.r; e.subAnims[i].color[1] = col.g;
            e.subAnims[i].color[2] = col.b; e.subAnims[i].color[3] = col.a;
        }
        e.animatedThisFrame = true;
    };

    // Objects (deduped): frustum-cull, then group visible objects by model. Repeated doodads/WMOs
    // draw via GPU hardware instancing — one shared palette + a per-instance transform each, one
    // draw per model-submesh regardless of instance count. Emitter objects can't instance their
    // (per-instance) particle geometry, so only the nearest few keep effects on the scene path.
    std::unordered_map<std::string, int> groupOf;     // modelPath -> index into instXforms_/groupEntry
    std::vector<ModelEntry*> groupEntry;              // parallel to instXforms_

    auto addInstance = [&](const std::string& path, ModelEntry& e, const glm::mat4& xform)
    {
        auto git = groupOf.find(path);
        int gi;
        if (git == groupOf.end())
        {
            gi = (int)instXforms_.size();
            groupOf.emplace(path, gi);
            instXforms_.emplace_back();
            groupEntry.push_back(&e);
        }
        else gi = git->second;
        instXforms_[gi].push_back(xform);
    };

    // The hovered / selected object can't ride the shared per-group push constant, so it is drawn on
    // the non-instanced scene path where it can carry an additive hover tint and/or a selection
    // outline. An internal MODD child inherits its parent WMO's authoring state, so selecting a
    // building outlines/highlights the actual furniture and fixtures as well as its shell.
    const bool haveHighlight = (highlightUid_ != 0 && highlightColor_.a > 0.0f);
    const bool haveOutline = (outlineUid_ != 0 && outlineColor_.a > 0.0f);
    auto belongsTo = [](const WorldObject& object, uint64_t uid, uint64_t rootUid) {
        return rootUid != 0 && (uid == rootUid ||
               (object.embeddedWmoDoodad && object.parentUid == rootUid));
    };
    auto isSpecial = [&](const WorldObject& object, uint64_t uid) {
        return (haveHighlight && belongsTo(object, uid, highlightUid_)) ||
               (haveOutline && belongsTo(object, uid, outlineUid_));
    };
    auto setSpecialFields = [&](SceneInstanceGpu& si, const WorldObject& object, uint64_t uid) {
        if (haveHighlight && belongsTo(object, uid, highlightUid_))
        {
            si.highlight[0] = highlightColor_.r; si.highlight[1] = highlightColor_.g;
            si.highlight[2] = highlightColor_.b; si.highlight[3] = highlightColor_.a;
        }
        if (haveOutline && belongsTo(object, uid, outlineUid_))
        {
            si.outline[0] = outlineColor_.r; si.outline[1] = outlineColor_.g;
            si.outline[2] = outlineColor_.b; si.outline[3] = outlineColor_.a;
        }
    };
    auto setObjectMaterial = [&](WorldObject& object, ModelEntry& entry, SceneInstanceGpu& si) {
        if (!wmo::HasVisibleDoodadTint(object.instanceTint))
        {
            si.submeshAnims = entry.subAnims.empty() ? nullptr : entry.subAnims.data();
            si.submeshAnimCount = (int)entry.subAnims.size();
            return;
        }
        // MODD's color is per instance, unlike an M2 batch color. Preserve the current UV/alpha
        // animation and multiply only this prop's material modulation on the scene path.
        object.tintedSubAnims = entry.subAnims;
        for (SubmeshAnim& anim : object.tintedSubAnims)
        {
            anim.color[0] *= object.instanceTint.r;
            anim.color[1] *= object.instanceTint.g;
            anim.color[2] *= object.instanceTint.b;
            anim.color[3] *= object.instanceTint.a;
        }
        si.submeshAnims = object.tintedSubAnims.empty() ? nullptr : object.tintedSubAnims.data();
        si.submeshAnimCount = (int)object.tintedSubAnims.size();
    };
    auto emitSceneObject = [&](WorldObject& object, ModelEntry& entry, uint64_t uid)
    {
        ensureAnimated(entry);
        object.palette.resize(entry.localBones.size());
        for (size_t j = 0; j < entry.localBones.size(); ++j)
            object.palette[j] = object.transform * entry.localBones[j];
        SceneInstanceGpu si{};
        si.handle = entry.handle;
        si.boneMatrices = object.palette.empty() ? nullptr : reinterpret_cast<const float*>(object.palette.data());
        si.boneCount = (int)object.palette.size();
        setObjectMaterial(object, entry, si);
        si.worldOrigin[0] = object.origin.x; si.worldOrigin[1] = object.origin.y; si.worldOrigin[2] = object.origin.z;
        setSpecialFields(si, object, uid);
        outScene.push_back(si);
    };

    std::vector<std::tuple<float, uint64_t, WorldObject*>> emitters;   // deferred: nearest-N get effects
    for (auto& kv : objects_)
    {
        const uint64_t uid = kv.first;
        WorldObject& obj = kv.second;
        if (!SphereInFrustum(frustum, obj.cullCenter, obj.cullRadius, 8.0f))
            continue;
        // Screen-size cull: skip doodads projecting below ~kMinScreenFrac of viewport height. WMOs
        // have large cullRadius and always pass; emitters (effects) are excluded so torches stay lit.
        if (!obj.effects)
        {
            const float dist = glm::length(obj.cullCenter - eye);
            if (dist > 1e-3f && obj.cullRadius * projY / dist < minScreenFrac_)
                continue;
        }
        auto mit = models_.find(obj.modelPath);
        if (mit == models_.end()) continue;
        if (obj.effects)
        {
            glm::vec3 d = obj.cullCenter - eye;
            emitters.emplace_back(glm::dot(d, d), uid, &obj);
            continue;
        }
        if (isSpecial(obj, uid) || wmo::HasVisibleDoodadTint(obj.instanceTint))
        {
            emitSceneObject(obj, mit->second, uid);
            continue;
        }
        addInstance(obj.modelPath, mit->second, obj.transform);
    }

    // Nearest emitter objects render non-instanced (geometry + live particles); the rest fall back
    // to instanced geometry without effects.
    constexpr size_t kNearEffects = 24;
    std::sort(emitters.begin(), emitters.end(),
              [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
    for (size_t idx = 0; idx < emitters.size(); ++idx)
    {
        const uint64_t uid = std::get<1>(emitters[idx]);
        WorldObject& obj = *std::get<2>(emitters[idx]);
        ModelEntry& e = models_.find(obj.modelPath)->second;
        const bool special = isSpecial(obj, uid);
        const bool hasInstanceTint = wmo::HasVisibleDoodadTint(obj.instanceTint);
        if (idx >= kNearEffects && !special && !hasInstanceTint)
        {
            // Distant neutral props retain instancing; selected/tinted WMO children stay on the
            // scene path so their parent outline and MODD material color remain exact.
            addInstance(obj.modelPath, e, obj.transform);
            continue;
        }
        ensureAnimated(e);
        obj.palette.resize(e.localBones.size());
        for (size_t j = 0; j < e.localBones.size(); ++j)
            obj.palette[j] = obj.transform * e.localBones[j];

        SceneInstanceGpu si{};
        si.handle = e.handle;
        si.boneMatrices = obj.palette.empty() ? nullptr : reinterpret_cast<const float*>(obj.palette.data());
        si.boneCount = (int)obj.palette.size();
        setObjectMaterial(obj, e, si);
        float dur = (float)e.animator.Duration(0);
        float t = dur > 0 ? std::fmod(animTime_, dur) : 0.0f;
        obj.effects->Step(0, t, dtMs, obj.palette, camRight, camUp, eye);
        const m2::EffectGeometry& g = obj.effects->Geometry();
        si.effectVerts = g.verts.data(); si.effectVertCount = (int)g.verts.size();
        si.effectDraws = g.draws.data(); si.effectDrawCount = (int)g.draws.size();
        si.worldOrigin[0] = obj.origin.x; si.worldOrigin[1] = obj.origin.y; si.worldOrigin[2] = obj.origin.z;
        if (special)
            setSpecialFields(si, obj, uid);
        outScene.push_back(si);
    }

    // Emit one InstancedGroup per model (shared palette written once; transforms point into the
    // stable per-group scratch — inner vectors survive instXforms_ growth via move).
    outInstanced.reserve(instXforms_.size());
    for (size_t gi = 0; gi < instXforms_.size(); ++gi)
    {
        ModelEntry& e = *groupEntry[gi];
        ensureAnimated(e);
        InstancedGroup g{};
        g.handle = e.handle;
        g.sharedPalette = e.localBones.empty() ? nullptr : reinterpret_cast<const float*>(e.localBones.data());
        g.boneCount = (int)e.localBones.size();
        g.submeshAnims = e.subAnims.empty() ? nullptr : e.subAnims.data();
        g.submeshAnimCount = (int)e.subAnims.size();
        g.instanceTransforms = reinterpret_cast<const float*>(instXforms_[gi].data());
        g.instanceCount = (int)instXforms_[gi].size();
        outInstanced.push_back(g);
    }
}
} // namespace we
