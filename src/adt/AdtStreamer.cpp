// AdtStreamer — see AdtStreamer.h.

#include "adt/AdtStreamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

#include "clientdata/ClientData.h"
#include "model/M2Loader.h"
#include "model/ModelUploadBuild.h"
#include "wmo/WmoLoader.h"
#include "wmo/WmoTypes.h"
#include "wmo/WmoUploadBuild.h"
#include "adt/AdtUploadBuild.h"
#include "viewer/ViewportCamera.h"   // SphereInFrustum

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
        liquidTypes_ = dbc_->LoadLiquidTypes(*cd_);
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
}

bool AdtStreamer::TileExists(int x, int y) const
{
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return false;
    return tileExists_[y * 64 + x] != 0;
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
    { std::lock_guard<std::mutex> lk(knownMutex_); knownPaths_.clear(); terrainTexKnown_.clear(); }
}

bool AdtStreamer::OpenMap(const std::string& mapDir)
{
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
    std::unordered_set<std::string> tileModelPaths;
    for (const adt::AdtPlacement& pl : tile.placements)
    {
        PlacementRef pr;
        pr.uid = pl.uniqueId;
        pr.path = pl.path;
        pr.isWmo = pl.isWmo;
        std::memcpy(&pr.transform[0][0], pl.transform, sizeof(pl.transform));
        pr.origin = glm::vec3(pl.origin[0], pl.origin[1], pl.origin[2]);
        out.placements.push_back(std::move(pr));

        const std::string key = LowerStr(pl.path);
        if (!tileModelPaths.insert(key).second)
            continue;   // already handled within this tile
        {
            std::lock_guard<std::mutex> lk(knownMutex_);
            if (knownPaths_.count(key)) { continue; }   // main already has it
        }
        ModelPayload mp;
        mp.path = pl.path;
        mp.isWmo = pl.isWmo;
        if (pl.isWmo)
        {
            wmo::WmoModel wm;
            wmo::WmoLoadOptions wopt;
            wopt.doodads = false;   // placed-WMO internal doodads are out of scope for the world view
            wopt.liquid = r.opt.liquid;
            if (!wmo::Load(*cd_, pl.path, wm, wopt, &liquidTypes_, nullptr)) continue;
            mp.upload = wmo::BuildUpload(*cd_, wm);
            for (ModelVertexGpu& v : mp.upload.vertices) { v.boneIndices[0] = 0; v.boneWeights[0] = 1.0f; }
            mp.upload.boneCount = 1;   // shell rides bone 0 = the placement matrix
            mp.boundsCenter = wm.boundsCenter;
            mp.boundsRadius = wm.boundsRadius;
        }
        else
        {
            auto m2 = std::make_shared<m2::M2Model>();
            if (!m2::Load(*cd_, pl.path, *m2, nullptr)) continue;
            mp.upload = m2::BuildUpload(*cd_, *m2);
            mp.hasEmitters = !m2->particleEmitters.empty() || !m2->ribbonEmitters.empty();
            mp.boundsCenter = m2->boundsCenter;
            mp.boundsRadius = m2->boundsRadius;
            mp.m2 = std::move(m2);
        }
        out.models.push_back(std::move(mp));
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
    if (!p.terrain.vertices.empty())
    {
        float minZ = p.terrain.vertices[0].pos[2], maxZ = minZ;
        for (const ModelVertexGpu& v : p.terrain.vertices)
        {
            minZ = std::min(minZ, v.pos[2]);
            maxZ = std::max(maxZ, v.pos[2]);
        }
        lt.centerZ = 0.5f * (minZ + maxZ);
        lt.radiusZ = 0.5f * (maxZ - minZ);
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
        obj.cullCenter = glm::vec3(pr.transform * glm::vec4(mit->second.boundsCenter, 1.0f));
        obj.cullRadius = mit->second.boundsRadius * glm::length(glm::vec3(pr.transform[0])) + 0.01f;
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
        objects_.erase(oit);
    }
    tiles_.erase(it);
}

void AdtStreamer::LoadGlobalWmo()
{
    if (!renderer_ || world_.globalWmo.empty())
        return;
    wmo::WmoModel wm;
    std::string err;
    if (!wmo::Load(*cd_, world_.globalWmo, wm, {}, &liquidTypes_, &err))
        return;
    ModelUpload up = wmo::BuildUpload(*cd_, wm);
    for (ModelVertexGpu& v : up.vertices) { v.boneIndices[0] = 0; v.boneWeights[0] = 1.0f; }
    up.boneCount = 1;
    ModelEntry e;
    e.isWmo = true;
    e.handle = renderer_->CreateModel(up);
    if (!e.handle) return;
    e.boundsCenter = wm.boundsCenter;
    e.boundsRadius = wm.boundsRadius;
    e.refCount = 1;
    models_["__global__"] = std::move(e);

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
    obj.cullCenter = glm::vec3(M * glm::vec4(wm.boundsCenter, 1.0f));
    obj.cullRadius = wm.boundsRadius * 4.0f;
    obj.refTiles.insert(0);
    objects_[1] = std::move(obj);

    startPos_ = obj.cullCenter;
    startRadius_ = glm::max(wm.boundsRadius, 20.0f);
}

// ---------------------------------------------------------------------------
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

    std::vector<std::pair<float, WorldObject*>> emitters;   // deferred: nearest-N get effects
    for (auto& kv : objects_)
    {
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
            emitters.emplace_back(glm::dot(d, d), &obj);
            continue;
        }
        addInstance(obj.modelPath, mit->second, obj.transform);
    }

    // Nearest emitter objects render non-instanced (geometry + live particles); the rest fall back
    // to instanced geometry without effects.
    constexpr size_t kNearEffects = 24;
    std::sort(emitters.begin(), emitters.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t idx = 0; idx < emitters.size(); ++idx)
    {
        WorldObject& obj = *emitters[idx].second;
        ModelEntry& e = models_.find(obj.modelPath)->second;
        if (idx >= kNearEffects)
        {
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
        si.submeshAnims = e.subAnims.empty() ? nullptr : e.subAnims.data();
        si.submeshAnimCount = (int)e.subAnims.size();
        float dur = (float)e.animator.Duration(0);
        float t = dur > 0 ? std::fmod(animTime_, dur) : 0.0f;
        obj.effects->Step(0, t, dtMs, obj.palette, camRight, camUp, eye);
        const m2::EffectGeometry& g = obj.effects->Geometry();
        si.effectVerts = g.verts.data(); si.effectVertCount = (int)g.verts.size();
        si.effectDraws = g.draws.data(); si.effectDrawCount = (int)g.draws.size();
        si.worldOrigin[0] = obj.origin.x; si.worldOrigin[1] = obj.origin.y; si.worldOrigin[2] = obj.origin.z;
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
