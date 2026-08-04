// AdtLoader — see AdtLoader.h. Phase 1: terrain heightmesh. Phase 2: per-chunk texture
// layers (MTEX/MCLY/MCAL) blended by a packed alpha map.

#include "adt/AdtLoader.h"

#include <algorithm>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

#include "clientdata/ClientData.h"
#include "util/ByteReader.h"

namespace we::adt
{
namespace
{
bool Fail(std::string* error, const char* msg)
{
    if (error)
        *error = msg;
    return false;
}

// MCVT stores 145 heights as interleaved rows: 9 outer, 8 inner, 9 outer, ... (17 per pair).
inline int OuterIdx(int row, int col) { return row * 17 + col; }        // row,col in 0..8
inline int InnerIdx(int row, int col) { return row * 17 + 9 + col; }    // row,col in 0..7

// Low-res 16-bit hole map: 4x4 blocks, one bit per 2x2 cell block.
inline bool CellHole(uint16_t holes, int cellRow, int cellCol)
{
    const int bit = (cellRow / 2) * 4 + (cellCol / 2);
    return (holes >> bit) & 1;
}

// Split a NUL-separated name blob (MTEX/MMDX) into strings.
std::vector<std::string> SplitBlob(const std::vector<uint8_t>& blob)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < blob.size())
    {
        size_t start = i;
        while (i < blob.size() && blob[i] != 0) ++i;
        if (i > start)
            out.emplace_back(reinterpret_cast<const char*>(blob.data() + start), i - start);
        ++i;   // skip NUL
    }
    return out;
}

// Decode one MCLY layer's MCAL alpha data into a 64x64 8-bit map. `mcal` spans this chunk's
// MCAL data (past the sub-chunk header). Handles the three 3.3.5a encodings + the 63x63 edge
// fixup (unless do_not_fix_alpha_map). See ADT/v18 MCAL.
void DecodeAlpha(const ByteReader& mcal, uint32_t offset, uint32_t nextOffset, bool compressed,
                 bool doNotFix, uint8_t out[4096])
{
    std::memset(out, 0, 4096);
    if (compressed)
    {
        size_t in = offset, o = 0;
        while (o < 4096 && in < mcal.size)
        {
            uint8_t ctrl = 0;
            mcal.Get(in, ctrl);
            ++in;
            const bool fill = (ctrl & 0x80) != 0;
            const int n = ctrl & 0x7F;
            for (int k = 0; k < n && o < 4096; ++k)
            {
                uint8_t val = 0;
                if (in < mcal.size) mcal.Get(in, val);
                out[o++] = val;
                if (!fill) ++in;
            }
            if (fill) ++in;
        }
    }
    else
    {
        const uint32_t span = (nextOffset > offset) ? (nextOffset - offset)
                                                    : (uint32_t)(mcal.size > offset ? mcal.size - offset : 0);
        if (span >= 4096)
        {
            for (int i = 0; i < 4096; ++i) { uint8_t v = 0; mcal.Get(offset + i, v); out[i] = v; }
        }
        else
        {
            // 2048 bytes, 4-bit: low nibble then high nibble, scaled to 8-bit (n*17).
            for (int i = 0; i < 2048; ++i)
            {
                uint8_t b = 0;
                mcal.Get(offset + i, b);
                out[i * 2 + 0] = (uint8_t)((b & 0x0F) * 17);
                out[i * 2 + 1] = (uint8_t)(((b >> 4) & 0x0F) * 17);
            }
        }
    }
    if (!doNotFix)
    {
        for (int y = 0; y < 64; ++y) out[y * 64 + 63] = out[y * 64 + 62];
        for (int x = 0; x < 64; ++x) out[63 * 64 + x] = out[62 * 64 + x];
    }
}

// Replace "%d" in a LiquidType texture pattern with a 1-based frame number.
std::string LiquidFramePath(const std::string& pattern, int frame)
{
    size_t pos = pattern.find("%d");
    if (pos == std::string::npos)
        return pattern;
    return pattern.substr(0, pos) + std::to_string(frame) + pattern.substr(pos + 2);
}

// Parse the MH2O chunk into liquid geometry. `chunkBases[k]` is the world NW-corner of MCNK k
// (same 256 row-major order as the MH2O header). Everything is emitted in the tile-local frame.
void BuildLiquidMH2O(ClientData& cd, const ByteReader& mh, const std::vector<glm::vec3>& chunkBases,
                     const glm::vec3& worldOffset, const LiquidTypeTable* liquidTypes, AdtTile& out)
{
    std::unordered_map<std::string, std::pair<int, int>> frameCache;   // pattern -> {base, count}

    for (size_t k = 0; k < chunkBases.size() && k < 256; ++k)
    {
        AdtMh2oHeader hdr{};
        if (!mh.Get(k * sizeof(AdtMh2oHeader), hdr) || hdr.layerCount == 0)
            continue;
        const glm::vec3 base = chunkBases[k];

        for (uint32_t L = 0; L < hdr.layerCount; ++L)
        {
            AdtMh2oInstance inst{};
            if (!mh.Get(hdr.offsetInstances + L * sizeof(AdtMh2oInstance), inst))
                continue;
            const int W = inst.width, Hh = inst.height;
            if (W <= 0 || Hh <= 0 || W > 8 || Hh > 8)
                continue;
            const int vw = W + 1, vh = Hh + 1;

            // Heightmap: lvf 0/1/3 store (vw*vh) floats first; lvf 2 / no data -> flat minHeight.
            const bool hasHeights = inst.offsetVertexData != 0 && inst.lvf != 2;
            auto heightAt = [&](int i, int j) -> float {
                if (!hasHeights) return inst.minHeight;
                float hv = inst.minHeight;
                mh.Get(inst.offsetVertexData + (size_t)(j * vw + i) * 4, hv);
                return hv;
            };
            // Exists bitmap: 1 bit per tile; offset 0 -> all present.
            auto tileExists = [&](int i, int j) -> bool {
                if (inst.offsetExistsBitmap == 0) return true;
                const size_t bit = (size_t)j * W + i;
                uint8_t byte = 0;
                mh.Get(inst.offsetExistsBitmap + bit / 8, byte);
                return (byte >> (bit % 8)) & 1;
            };

            // Resolve type -> category + animated texture frames.
            const DbcStore::LiquidTypeInfo* info = nullptr;
            if (liquidTypes)
            {
                auto it = liquidTypes->find(inst.liquidType);
                if (it != liquidTypes->end()) info = &it->second;
            }
            const uint32_t category = info ? info->category : 0u;

            int frameBase = -1, frameCount = 1;
            if (info && !info->texture.empty())
            {
                auto cached = frameCache.find(info->texture);
                if (cached != frameCache.end()) { frameBase = cached->second.first; frameCount = cached->second.second; }
                else
                {
                    frameBase = (int)out.liquidTexturePaths.size();
                    if (info->texture.find("%d") == std::string::npos)
                    {
                        out.liquidTexturePaths.push_back(info->texture);
                        frameCount = 1;
                    }
                    else
                    {
                        int f = 1;
                        for (; f <= 60; ++f)
                        {
                            std::string path = LiquidFramePath(info->texture, f);
                            if (!cd.HasFile(path)) break;
                            out.liquidTexturePaths.push_back(std::move(path));
                        }
                        frameCount = f - 1;
                        if (frameCount <= 0) { out.liquidTexturePaths.push_back(LiquidFramePath(info->texture, 1)); frameCount = 1; }
                    }
                    frameCache[info->texture] = {frameBase, frameCount};
                }
            }

            uint8_t tr, tg, tb, ta;
            if (category == 2)      { tr = 255; tg = 255; tb = 255; ta = 240; }   // magma
            else if (category == 3) { tr = 255; tg = 255; tb = 255; ta = 150; }   // slime
            else                    { tr = 150; tg = 195; tb = 235; ta = 130; }   // water/ocean
            if (frameBase < 0 && category == 2) { tr = 255; tg = 90; tb = 25; }

            const uint32_t vertexBase = (uint32_t)out.liquidVertices.size();
            for (int j = 0; j < vh; ++j)
                for (int i = 0; i < vw; ++i)
                {
                    AdtVertex v;
                    v.pos[0] = base.x - (inst.yOffset + j) * kUnitSize - worldOffset.x;
                    v.pos[1] = base.y - (inst.xOffset + i) * kUnitSize - worldOffset.y;
                    v.pos[2] = heightAt(i, j);
                    v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = 1;
                    v.uv[0] = i * 0.5f; v.uv[1] = j * 0.5f;
                    v.color[0] = tr; v.color[1] = tg; v.color[2] = tb; v.color[3] = ta;
                    out.liquidVertices.push_back(v);
                }

            const uint32_t indexBase = (uint32_t)out.liquidIndices.size();
            int tiles = 0;
            for (int j = 0; j < Hh; ++j)
                for (int i = 0; i < W; ++i)
                {
                    if (!tileExists(i, j)) continue;
                    const uint32_t v00 = vertexBase + j * vw + i;
                    const uint32_t v10 = v00 + 1, v01 = v00 + vw, v11 = v01 + 1;
                    out.liquidIndices.push_back(v00); out.liquidIndices.push_back(v10); out.liquidIndices.push_back(v11);
                    out.liquidIndices.push_back(v00); out.liquidIndices.push_back(v11); out.liquidIndices.push_back(v01);
                    ++tiles;
                }
            if (tiles == 0) { out.liquidVertices.resize(vertexBase); continue; }

            AdtSubmesh s;
            s.indexStart = indexBase;
            s.indexCount = (uint32_t)out.liquidIndices.size() - indexBase;
            s.layerTex[0] = frameBase;
            s.blendMode = 2;                        // alpha
            s.materialFlags = 0x01 | 0x04 | 0x08;   // unlit + two-sided + liquid (ignore texture alpha)
            s.isLiquid = true;
            s.liquidFrameBase = frameBase;
            s.liquidFrameCount = frameCount;
            s.center[0] = base.x - (inst.yOffset + Hh * 0.5f) * kUnitSize - worldOffset.x;
            s.center[1] = base.y - (inst.xOffset + W * 0.5f) * kUnitSize - worldOffset.y;
            s.center[2] = heightAt(W / 2, Hh / 2);
            out.liquidSubmeshes.push_back(s);
            out.liquidTileCount += tiles;
        }
    }
}

// Doodad/WMO name strings are ".mdx"/".mdl" in older data; the 3.3.5a client uses ".m2".
std::string NormalizeModel(std::string p)
{
    if (p.size() >= 4)
    {
        std::string ext = p.substr(p.size() - 4);
        for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
        if (ext == ".mdx" || ext == ".mdl")
            p = p.substr(0, p.size() - 4) + ".m2";
    }
    return p;
}

// Build a placement matrix mapping a model's local (Z-up) space into the tile-local terrain
// frame. MDDF/MODF positions are in the WoW placement convention; convert to the same world
// frame the terrain uses (which comes straight from MCNK.position), then subtract worldOffset.
// Rotation is Euler degrees: ry around up, rx around the E-W axis, rz around the N-S axis.
// Orientation is a calibration checkpoint (see the plan) — verified against a rendered tile.
glm::mat4 PlacementMatrix(const float pos[3], const float rot[3], float scale,
                          const glm::vec3& worldOffset, glm::vec3& originOut)
{
    // The terrain frame uses MCNK.position directly: pos0 = N-S world axis, pos1 = E-W world
    // axis. MDDF/MODF store (E-W, up, N-S), so map accordingly (both horizontals get 32*TS - v).
    glm::vec3 world(kMapOrigin - pos[2], kMapOrigin - pos[0], pos[1]);
    glm::vec3 local = world - glm::vec3(worldOffset.x, worldOffset.y, 0.0f);
    originOut = local;

    // Rotation is Euler degrees; in practice all shipped placements are yaw-only (0, yaw, 0).
    // Yaw is about the world up axis (our Z). Pitch/roll (rot[0]/rot[2]) are near-universally
    // zero in 3.3.5a data.
    glm::mat4 M(1.0f);
    M = glm::translate(M, local);
    M = glm::rotate(M, glm::radians(rot[1] + 180.0f), glm::vec3(0, 0, 1));   // yaw (up)
    M = glm::rotate(M, glm::radians(rot[0]), glm::vec3(0, 1, 0));   // pitch (E-W)
    M = glm::rotate(M, glm::radians(rot[2]), glm::vec3(1, 0, 0));   // roll (N-S)
    M = glm::scale(M, glm::vec3(scale));
    return M;
}

// Build one MCNK's terrain into `out` (world coords). Emits one submesh (the chunk) with its
// texture layers + a packed alpha map. `texCount` is the tile's MTEX count (for bounds).
void BuildChunkTerrain(const ByteReader& rr, const Chunk& mcnk, int texCount, AdtTile& out)
{
    if (mcnk.size < 128)
        return;
    AdtMcnkHeader h{};
    if (!rr.Get(mcnk.offset, h))
        return;
    if (h.sizeLiquid > 8)
        ++out.mclqChunks;

    // A reader whose offset 0 is the MCNK *magic*, so the header ofs* fields resolve directly.
    if (mcnk.offset < 8)
        return;
    ByteReader mc(rr.data + mcnk.offset - 8, mcnk.size + 8);

    // Sub-chunk data by header offset (ofs points to the sub-chunk's 8-byte IFF header).
    auto subData = [&](uint32_t ofs) -> ByteReader {
        if (ofs == 0) return {};
        uint32_t sz = 0;
        if (!mc.Get(ofs + 4, sz)) return {};
        const size_t d = (size_t)ofs + 8;
        if (d > mc.size) return {};
        sz = std::min<uint32_t>(sz, (uint32_t)(mc.size - d));
        return ByteReader(mc.data + d, sz);
    };

    std::vector<float>    mcvt;
    std::vector<int8_t>   mcnr;
    std::vector<uint32_t> mccv;
    subData(h.ofsHeight).GetArrayAt<float>(0, 145, mcvt);
    { ByteReader nr = subData(h.ofsNormal); nr.GetArrayAt<int8_t>(0, std::min<size_t>(nr.size, 435), mcnr); }
    if (h.ofsMCCV) { ByteReader cv = subData(h.ofsMCCV); cv.GetArrayAt<uint32_t>(0, 145, mccv); }
    if (mcvt.size() < 145)
        return;

    // Texture layers (MCLY) + their alpha maps (MCAL). Layer 0 is the base (no alpha).
    std::vector<AdtLayer> layers;
    if (h.ofsLayer && h.nLayers)
        subData(h.ofsLayer).GetArrayAt<AdtLayer>(0, std::min<uint32_t>(h.nLayers, 4u), layers);
    // MCAL data spans from its header to the end of the MCNK (over-reads are bounds-checked;
    // per-layer sizes are exact, so trailing sub-chunks are never consumed).
    ByteReader mcal;
    if (h.ofsAlpha)
    {
        const size_t d = (size_t)h.ofsAlpha + 8;
        if (d < mc.size) mcal = ByteReader(mc.data + d, mc.size - d);
    }
    const bool doNotFix = (h.flags & 0x8000u) != 0;

    // Pack layers 1..3 alpha into RGB of a 64x64 map (base weight = 1 - sum, in the shader).
    AdtAlphaMap alpha;
    alpha.rgba.assign(64 * 64 * 4, 0);
    for (int i = 0; i < 64 * 64; ++i) alpha.rgba[i * 4 + 3] = 255;
    for (size_t L = 1; L < layers.size(); ++L)
    {
        if (!(layers[L].flags & kAdtLayerUseAlphaMap))
            continue;
        uint32_t nextOff = (uint32_t)mcal.size;
        for (size_t j = 1; j < layers.size(); ++j)
            if (j != L && (layers[j].flags & kAdtLayerUseAlphaMap) &&
                layers[j].offsetInMCAL > layers[L].offsetInMCAL)
                nextOff = std::min<uint32_t>(nextOff, layers[j].offsetInMCAL);
        uint8_t decoded[4096];
        DecodeAlpha(mcal, layers[L].offsetInMCAL, nextOff,
                    (layers[L].flags & kAdtLayerAlphaCompressed) != 0, doNotFix, decoded);
        const int ch = (int)L - 1;   // layer 1->R, 2->G, 3->B
        for (int i = 0; i < 64 * 64; ++i)
            alpha.rgba[i * 4 + ch] = decoded[i];
    }
    const int alphaIndex = (int)out.alphaMaps.size();
    out.alphaMaps.push_back(std::move(alpha));

    const glm::vec3 base(h.position[0], h.position[1], h.position[2]);
    const bool haveNr = mcnr.size() >= 145 * 3;
    const bool haveCv = mccv.size() >= 145;

    auto emit = [&](int idx, float wx, float wy, float u, float v)
    {
        AdtVertex vv;
        vv.pos[0] = wx;
        vv.pos[1] = wy;
        vv.pos[2] = base.z + mcvt[idx];
        if (haveNr)
        {
            vv.normal[0] = mcnr[idx * 3 + 0] / 127.0f;   // disk X,Z,Y -> world X(north),Y(west),Z(up)
            vv.normal[1] = mcnr[idx * 3 + 2] / 127.0f;
            vv.normal[2] = mcnr[idx * 3 + 1] / 127.0f;
        }
        else { vv.normal[0] = 0; vv.normal[1] = 0; vv.normal[2] = 1; }
        vv.uv[0] = u;
        vv.uv[1] = v;
        if (haveCv)
        {
            uint32_t p = mccv[idx];   // bytes B,G,R,A; 0x7F == 1.0
            int b = p & 0xFF, g = (p >> 8) & 0xFF, r = (p >> 16) & 0xFF;
            vv.color[0] = (uint8_t)std::min(255, r * 255 / 127);
            vv.color[1] = (uint8_t)std::min(255, g * 255 / 127);
            vv.color[2] = (uint8_t)std::min(255, b * 255 / 127);
            vv.color[3] = 255;
        }
        return vv;
    };

    const uint32_t vertexBase = static_cast<uint32_t>(out.vertices.size());
    // Chunk id = the compacted submesh index this chunk will receive (empty chunks are dropped and
    // never push a submesh, so their unused verts keep a stale id — harmless, no indices touch them).
    const uint8_t chunkId = static_cast<uint8_t>(out.submeshes.size());
    for (int row = 0; row <= 8; ++row)
    {
        for (int col = 0; col <= 8; ++col)
            out.vertices.push_back(emit(OuterIdx(row, col), base.x - row * kUnitSize,
                                        base.y - col * kUnitSize, col / 8.0f, row / 8.0f));
        if (row < 8)
            for (int col = 0; col < 8; ++col)
                out.vertices.push_back(emit(InnerIdx(row, col), base.x - (row + 0.5f) * kUnitSize,
                                            base.y - (col + 0.5f) * kUnitSize,
                                            (col + 0.5f) / 8.0f, (row + 0.5f) / 8.0f));
    }
    for (size_t i = vertexBase; i < out.vertices.size(); ++i)
        out.vertices[i].chunkId = chunkId;

    const uint32_t indexStart = static_cast<uint32_t>(out.indices.size());
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col)
        {
            if (CellHole(h.holesLowRes, row, col))
                continue;
            const uint32_t tl = vertexBase + OuterIdx(row, col);
            const uint32_t tr = vertexBase + OuterIdx(row, col + 1);
            const uint32_t bl = vertexBase + OuterIdx(row + 1, col);
            const uint32_t br = vertexBase + OuterIdx(row + 1, col + 1);
            const uint32_t ct = vertexBase + InnerIdx(row, col);
            const uint32_t tris[12] = {tl, tr, ct,  tr, br, ct,  br, bl, ct,  bl, tl, ct};
            for (uint32_t idx : tris)
                out.indices.push_back(idx);
        }

    AdtSubmesh sm;
    sm.indexStart = indexStart;
    sm.indexCount = static_cast<uint32_t>(out.indices.size()) - indexStart;
    if (sm.indexCount == 0)   // fully holed chunk: drop its (empty) submesh + alpha map slot
    {
        out.alphaMaps.pop_back();
        return;
    }
    sm.layerCount = (int)layers.size();
    if (layers.empty())
        ++out.chunksNoLayer;
    for (size_t L = 0; L < layers.size(); ++L)
        sm.layerTex[L] = (layers[L].textureId < (uint32_t)texCount) ? (int)layers[L].textureId : -1;
    sm.alphaMap = alphaIndex;
    out.submeshes.push_back(sm);
    ++out.renderedChunks;
}

// Concatenate `src` into `dst`, remapping every buffer index (vertex/index bases, texture/alpha
// slots) so the two tiles share one merged AdtTile. Both must already be in the same world frame.
void MergeInto(AdtTile& dst, const AdtTile& src)
{
    // Terrain.
    const uint32_t vBase = (uint32_t)dst.vertices.size();
    const uint32_t iBase = (uint32_t)dst.indices.size();
    const int texBase = (int)dst.texturePaths.size();
    const int alphaBase = (int)dst.alphaMaps.size();
    dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
    for (uint32_t idx : src.indices) dst.indices.push_back(idx + vBase);
    dst.texturePaths.insert(dst.texturePaths.end(), src.texturePaths.begin(), src.texturePaths.end());
    dst.alphaMaps.insert(dst.alphaMaps.end(), src.alphaMaps.begin(), src.alphaMaps.end());
    for (AdtSubmesh s : src.submeshes)
    {
        s.indexStart += iBase;
        for (int k = 0; k < 4; ++k) if (s.layerTex[k] >= 0) s.layerTex[k] += texBase;
        if (s.alphaMap >= 0) s.alphaMap += alphaBase;
        dst.submeshes.push_back(s);
    }

    // Liquid.
    const uint32_t lvBase = (uint32_t)dst.liquidVertices.size();
    const uint32_t liBase = (uint32_t)dst.liquidIndices.size();
    const int liqTexBase = (int)dst.liquidTexturePaths.size();
    dst.liquidVertices.insert(dst.liquidVertices.end(), src.liquidVertices.begin(), src.liquidVertices.end());
    for (uint32_t idx : src.liquidIndices) dst.liquidIndices.push_back(idx + lvBase);
    dst.liquidTexturePaths.insert(dst.liquidTexturePaths.end(), src.liquidTexturePaths.begin(),
                                  src.liquidTexturePaths.end());
    for (AdtSubmesh s : src.liquidSubmeshes)
    {
        s.indexStart += liBase;
        if (s.layerTex[0] >= 0) s.layerTex[0] += liqTexBase;
        if (s.liquidFrameBase >= 0) s.liquidFrameBase += liqTexBase;
        dst.liquidSubmeshes.push_back(s);
    }

    // Placements (already in the shared frame).
    dst.placements.insert(dst.placements.end(), src.placements.begin(), src.placements.end());

    // Diagnostics.
    dst.chunkCount += src.chunkCount;
    dst.renderedChunks += src.renderedChunks;
    dst.doodadDefCount += src.doodadDefCount;
    dst.wmoDefCount += src.wmoDefCount;
    dst.liquidTileCount += src.liquidTileCount;
    dst.mclqChunks += src.mclqChunks;
    dst.chunksNoLayer += src.chunksNoLayer;
    dst.hasMh2o = dst.hasMh2o || src.hasMh2o;
    dst.textureCount = (int)dst.texturePaths.size();
}
} // namespace

bool Load(ClientData& cd, const std::string& adtPath, AdtTile& out, const AdtLoadOptions& opt,
          const LiquidTypeTable* liquidTypes, std::string* error, const glm::vec3* forcedOffset)
{
    out = AdtTile{};
    out.name = adtPath;

    std::vector<uint8_t> bytes = cd.ReadFile(adtPath);
    if (bytes.empty())
        return Fail(error, "adt file missing or empty");
    ByteReader rr(bytes);

    std::vector<Chunk> mcnks;
    mcnks.reserve(256);
    std::vector<uint8_t>      mmdx, mwmo;   // M2 / WMO name blobs
    std::vector<uint32_t>     mmid, mwid;   // offsets into the blobs
    std::vector<AdtDoodadDef> mddf;         // M2 placements
    std::vector<AdtMapObjDef> modf;         // WMO placements
    std::vector<uint8_t>      mh2o;         // liquid chunk (parsed after worldOffset is known)
    ChunkIter it(rr);
    Chunk c;
    while (it.Next(c))
    {
        if (c.Is("MCNK")) mcnks.push_back(c);
        else if (c.Is("MTEX"))
        {
            std::vector<uint8_t> blob;
            rr.GetArrayAt<uint8_t>(c.offset, c.size, blob);
            out.texturePaths = SplitBlob(blob);
        }
        else if (c.Is("MMDX")) rr.GetArrayAt<uint8_t>(c.offset, c.size, mmdx);
        else if (c.Is("MMID")) rr.GetArrayAt<uint32_t>(c.offset, c.size / 4, mmid);
        else if (c.Is("MWMO")) rr.GetArrayAt<uint8_t>(c.offset, c.size, mwmo);
        else if (c.Is("MWID")) rr.GetArrayAt<uint32_t>(c.offset, c.size / 4, mwid);
        else if (c.Is("MDDF")) rr.GetArrayAt<AdtDoodadDef>(c.offset, c.size / sizeof(AdtDoodadDef), mddf);
        else if (c.Is("MODF")) rr.GetArrayAt<AdtMapObjDef>(c.offset, c.size / sizeof(AdtMapObjDef), modf);
        else if (c.Is("MH2O")) { rr.GetArrayAt<uint8_t>(c.offset, c.size, mh2o); out.hasMh2o = c.size > 0; }
    }
    out.textureCount = static_cast<int>(out.texturePaths.size());
    out.doodadDefCount = static_cast<int>(mddf.size());
    out.wmoDefCount = static_cast<int>(modf.size());

    out.chunkCount = static_cast<int>(mcnks.size());
    if (mcnks.empty())
        return Fail(error, "adt has no MCNK chunks");

    // Per-MCNK world NW-corner (file/row-major order == MH2O header order), for liquid placement.
    std::vector<glm::vec3> chunkBases;
    chunkBases.reserve(mcnks.size());
    for (const Chunk& mcnk : mcnks)
    {
        AdtMcnkHeader hh{};
        rr.Get(mcnk.offset, hh);
        chunkBases.emplace_back(hh.position[0], hh.position[1], hh.position[2]);
    }

    for (const Chunk& mcnk : mcnks)
        BuildChunkTerrain(rr, mcnk, out.textureCount, out);

    if (out.vertices.empty() || out.indices.empty())
        return Fail(error, "adt produced no terrain geometry");

    // Recenter into a tile-local frame (world coords are ~17000). Offset X/Y by the tile
    // center; keep Z. Placements reuse worldOffset.
    glm::vec3 lo(out.vertices[0].pos[0], out.vertices[0].pos[1], out.vertices[0].pos[2]);
    glm::vec3 hi = lo;
    for (const AdtVertex& v : out.vertices)
    {
        glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    // Self-center, unless a shared frame was supplied (stitched neighborhoods).
    out.worldOffset = forcedOffset ? *forcedOffset
                                   : glm::vec3((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, 0.0f);
    for (AdtVertex& v : out.vertices)
    {
        v.pos[0] -= out.worldOffset.x;
        v.pos[1] -= out.worldOffset.y;
    }

    // In the (possibly shared) frame, this tile's center is its bounds midpoint minus the offset.
    out.boundsCenter = glm::vec3((lo.x + hi.x) * 0.5f - out.worldOffset.x,
                                 (lo.y + hi.y) * 0.5f - out.worldOffset.y, (lo.z + hi.z) * 0.5f);
    out.boundsRadius = glm::max(glm::length(glm::vec3((hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f,
                                                      (hi.z - lo.z) * 0.5f)), 1.0f);

    // Placements (baked into the tile-local frame; the viewer/harness loads + instances them).
    ByteReader mmdxR(mmdx), mwmoR(mwmo);
    if (opt.doodads)
    {
        for (const AdtDoodadDef& d : mddf)
        {
            if (d.nameId >= mmid.size())
                continue;
            std::string path = NormalizeModel(mmdxR.GetCString(mmid[d.nameId]));
            if (path.empty())
                continue;
            AdtPlacement pl;
            pl.path = std::move(path);
            pl.isWmo = false;
            pl.uniqueId = d.uniqueId;
            glm::vec3 origin;
            glm::mat4 M = PlacementMatrix(d.position, d.rotation, d.scale / 1024.0f, out.worldOffset, origin);
            std::memcpy(pl.transform, &M[0][0], sizeof(pl.transform));
            pl.origin[0] = origin.x; pl.origin[1] = origin.y; pl.origin[2] = origin.z;
            out.placements.push_back(std::move(pl));
        }
    }
    if (opt.wmos)
    {
        for (const AdtMapObjDef& m : modf)
        {
            if (m.nameId >= mwid.size())
                continue;
            std::string path = mwmoR.GetCString(mwid[m.nameId]);
            if (path.empty())
                continue;
            AdtPlacement pl;
            pl.path = std::move(path);
            pl.isWmo = true;
            pl.doodadSet = m.doodadSet;
            pl.uniqueId = m.uniqueId;
            glm::vec3 origin;
            // MODF has no per-instance scale in 3.3.5a (the field is padding); scale is 1.0.
            glm::mat4 M = PlacementMatrix(m.position, m.rotation, 1.0f, out.worldOffset, origin);
            std::memcpy(pl.transform, &M[0][0], sizeof(pl.transform));
            pl.origin[0] = origin.x; pl.origin[1] = origin.y; pl.origin[2] = origin.z;
            out.placements.push_back(std::move(pl));
        }
    }

    // Liquid: MH2O (WotLK) is primary. MCLQ fallback (old-world tiles) is a later refinement.
    if (opt.liquid && !mh2o.empty())
        BuildLiquidMH2O(cd, ByteReader(mh2o), chunkBases, out.worldOffset, liquidTypes, out);

    return true;
}

std::string TilePath(const std::string& mapDir, int x, int y)
{
    return "World\\Maps\\" + mapDir + "\\" + mapDir + "_" + std::to_string(x) + "_" +
           std::to_string(y) + ".adt";
}

std::vector<std::pair<int, int>> ListTiles(ClientData& cd, const std::string& mapDir)
{
    std::vector<std::pair<int, int>> out;
    std::vector<uint8_t> bytes = cd.ReadFile("World\\Maps\\" + mapDir + "\\" + mapDir + ".wdt");
    if (bytes.empty())
        return out;
    ByteReader rr(bytes);
    ChunkIter it(rr);
    Chunk c;
    while (it.Next(c))
    {
        if (!c.Is("MAIN"))
            continue;
        // MAIN is a 64x64 grid of {uint32 flags, uint32 asyncId}; flag bit 0 = the tile exists.
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
            {
                uint32_t flags = 0;
                if (rr.Get(c.offset + (size_t)(y * 64 + x) * 8, flags) && (flags & 1))
                    out.emplace_back(x, y);
            }
        break;
    }
    return out;
}

bool LoadWorldInfo(ClientData& cd, const std::string& mapDir, AdtWorldInfo& out)
{
    out = AdtWorldInfo{};
    std::vector<uint8_t> bytes = cd.ReadFile("World\\Maps\\" + mapDir + "\\" + mapDir + ".wdt");
    if (bytes.empty())
        return false;
    ByteReader rr(bytes);
    ChunkIter it(rr);
    Chunk c;
    std::vector<uint8_t> mwmo;
    while (it.Next(c))
    {
        if (c.Is("MPHD"))
        {
            rr.Get(c.offset, out.mphdFlags);   // first uint32 = flags
            out.wmoOnly = (out.mphdFlags & 0x0001u) != 0;
        }
        else if (c.Is("MAIN"))
        {
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                {
                    uint32_t flags = 0;
                    if (rr.Get(c.offset + (size_t)(y * 64 + x) * 8, flags) && (flags & 1))
                        out.tiles.emplace_back(x, y);
                }
        }
        else if (c.Is("MWMO"))
        {
            rr.GetArrayAt<uint8_t>(c.offset, c.size, mwmo);
            ByteReader br(mwmo);
            out.globalWmo = br.GetCString(0);
        }
        else if (c.Is("MODF"))
        {
            out.hasGlobalPlacement = rr.Get(c.offset, out.globalPlacement);
        }
    }
    return true;
}

bool LoadWdl(ClientData& cd, const std::string& mapDir, WdlData& out)
{
    out = WdlData{};
    std::vector<uint8_t> bytes = cd.ReadFile("World\\Maps\\" + mapDir + "\\" + mapDir + ".wdl");
    if (bytes.empty())
        return false;
    ByteReader rr(bytes);
    ChunkIter it(rr);
    Chunk c;
    size_t maofData = 0;   // file offset of the MAOF data (4096 uint32 absolute offsets)
    while (it.Next(c))
        if (c.Is("MAOF")) { maofData = c.offset; break; }
    if (!maofData)
        return false;

    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            uint32_t mareOff = 0;
            if (!rr.Get(maofData + (size_t)(y * 64 + x) * 4, mareOff) || mareOff == 0)
                continue;
            // mareOff points at the MARE chunk header; the 545 int16 heights follow the 8-byte header.
            WdlTile t;
            t.x = x;
            t.y = y;
            if (rr.GetArrayAt<int16_t>(mareOff + 8, 545, t.heights) && t.heights.size() == 545)
                out.tiles.push_back(std::move(t));
        }
    return !out.tiles.empty();
}

bool LoadNeighborhood(ClientData& cd, const std::string& mapDir, int cx, int cy, int radius,
                      AdtTile& out, const AdtLoadOptions& opt, const LiquidTypeTable* liquidTypes,
                      std::string* error)
{
    // The center tile establishes the shared world frame all neighbors are expressed in.
    if (!Load(cd, TilePath(mapDir, cx, cy), out, opt, liquidTypes, error))
        return false;
    if (radius <= 0)
        return true;
    const glm::vec3 sharedOffset = out.worldOffset;

    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;
            AdtTile t;
            if (Load(cd, TilePath(mapDir, cx + dx, cy + dy), t, opt, liquidTypes, nullptr, &sharedOffset))
                MergeInto(out, t);
        }

    // Bounds over the whole merged neighborhood.
    glm::vec3 lo(out.vertices[0].pos[0], out.vertices[0].pos[1], out.vertices[0].pos[2]);
    glm::vec3 hi = lo;
    for (const AdtVertex& v : out.vertices)
    {
        glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    out.boundsCenter = (lo + hi) * 0.5f;
    out.boundsRadius = glm::max(glm::length((hi - lo) * 0.5f), 1.0f);
    out.worldOffset = sharedOffset;
    return true;
}
} // namespace we::adt
