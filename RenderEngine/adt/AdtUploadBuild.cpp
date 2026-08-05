// AdtUploadBuild — see AdtUploadBuild.h.

#include "adt/AdtUploadBuild.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "clientdata/BlpDecoder.h"
#include "clientdata/ClientData.h"

namespace we::adt
{
ModelUpload BuildUpload(ClientData& cd, const AdtTile& tile)
{
    ModelUpload up;

    up.vertices.reserve(tile.vertices.size());
    for (const AdtVertex& v : tile.vertices)
    {
        ModelVertexGpu g = {};
        g.pos[0] = v.pos[0]; g.pos[1] = v.pos[1]; g.pos[2] = v.pos[2];
        g.normal[0] = v.normal[0]; g.normal[1] = v.normal[1]; g.normal[2] = v.normal[2];
        g.uv[0] = v.uv[0]; g.uv[1] = v.uv[1];
        for (int i = 0; i < 4; ++i) { g.boneIndices[i] = 0; g.boneWeights[i] = 0.0f; }
        g.color[0] = v.color[0]; g.color[1] = v.color[1]; g.color[2] = v.color[2]; g.color[3] = v.color[3];
        up.vertices.push_back(g);
    }

    up.indices = tile.indices;
    up.boneCount = 0;

    up.submeshes.reserve(tile.submeshes.size());
    for (const AdtSubmesh& s : tile.submeshes)
    {
        ModelSubmeshGpu m = {};
        m.indexStart = s.indexStart;
        m.indexCount = s.indexCount;
        m.textureIndex = s.layerTex[0];   // Phase 1: -1 (white); Phase 2 uses the terrain path
        m.blendMode = s.blendMode;
        m.materialFlags = s.materialFlags;
        m.center[0] = s.center[0]; m.center[1] = s.center[1]; m.center[2] = s.center[2];
        up.submeshes.push_back(m);
    }

    up.textures.resize(tile.texturePaths.size());
    for (size_t i = 0; i < tile.texturePaths.size(); ++i)
    {
        if (tile.texturePaths[i].empty())
            continue;
        BlpImage img = DecodeBlp(cd.ReadFile(tile.texturePaths[i]));
        if (img.valid())
        {
            up.textures[i].rgba = std::move(img.rgba);
            up.textures[i].w = img.width;
            up.textures[i].h = img.height;
        }
    }
    return up;
}

TerrainUpload BuildTerrainUpload(ClientData& cd, const AdtTile& tile,
                                 const std::unordered_set<std::string>* cached)
{
    TerrainUpload up;

    up.vertices.reserve(tile.vertices.size());
    for (const AdtVertex& v : tile.vertices)
    {
        ModelVertexGpu g = {};
        g.pos[0] = v.pos[0]; g.pos[1] = v.pos[1]; g.pos[2] = v.pos[2];
        g.normal[0] = v.normal[0]; g.normal[1] = v.normal[1]; g.normal[2] = v.normal[2];
        g.uv[0] = v.uv[0]; g.uv[1] = v.uv[1];
        for (int i = 0; i < 4; ++i) { g.boneIndices[i] = 0; g.boneWeights[i] = 0.0f; }
        g.boneIndices[0] = v.chunkId;   // terrain.vert reads this as the per-chunk param index
        g.color[0] = v.color[0]; g.color[1] = v.color[1]; g.color[2] = v.color[2]; g.color[3] = v.color[3];
        up.vertices.push_back(g);
    }
    up.indices = tile.indices;

    up.submeshes.reserve(tile.submeshes.size());
    for (const AdtSubmesh& s : tile.submeshes)
    {
        TerrainSubmeshGpu m = {};
        m.indexStart = s.indexStart;
        m.indexCount = s.indexCount;
        for (int i = 0; i < 4; ++i) m.layerTex[i] = s.layerTex[i];
        m.layerCount = s.layerCount;
        m.alphaMap = s.alphaMap;
        up.submeshes.push_back(m);
    }

    // Ground textures: carry every path; decode only the ones not already in the renderer's cache
    // (the streamer's worker passes the cached set so shared textures are decoded once, world-wide).
    up.texturePaths = tile.texturePaths;
    up.textures.resize(tile.texturePaths.size());
    for (size_t i = 0; i < tile.texturePaths.size(); ++i)
    {
        if (tile.texturePaths[i].empty())
            continue;
        if (cached && cached->count(tile.texturePaths[i]))
            continue;   // already resident -> path only, no decode
        BlpImage img = DecodeBlp(cd.ReadFile(tile.texturePaths[i]));
        if (img.valid())
        {
            up.textures[i].rgba = std::move(img.rgba);
            up.textures[i].w = img.width;
            up.textures[i].h = img.height;
        }
    }

    // Per-chunk packed alpha maps (64x64 RGBA).
    up.alphaMaps.resize(tile.alphaMaps.size());
    for (size_t i = 0; i < tile.alphaMaps.size(); ++i)
    {
        up.alphaMaps[i].rgba = tile.alphaMaps[i].rgba;
        up.alphaMaps[i].w = 64;
        up.alphaMaps[i].h = 64;
    }
    return up;
}

ModelUpload BuildLiquidUpload(ClientData& cd, const AdtTile& tile)
{
    ModelUpload up;
    if (tile.liquidVertices.empty() || tile.liquidSubmeshes.empty())
        return up;

    up.vertices.reserve(tile.liquidVertices.size());
    for (const AdtVertex& v : tile.liquidVertices)
    {
        ModelVertexGpu g = {};
        g.pos[0] = v.pos[0]; g.pos[1] = v.pos[1]; g.pos[2] = v.pos[2];
        g.normal[0] = v.normal[0]; g.normal[1] = v.normal[1]; g.normal[2] = v.normal[2];
        g.uv[0] = v.uv[0]; g.uv[1] = v.uv[1];
        for (int i = 0; i < 4; ++i) { g.boneIndices[i] = 0; g.boneWeights[i] = 0.0f; }
        g.color[0] = v.color[0]; g.color[1] = v.color[1]; g.color[2] = v.color[2]; g.color[3] = v.color[3];
        up.vertices.push_back(g);
    }
    up.indices = tile.liquidIndices;
    up.boneCount = 0;

    up.submeshes.reserve(tile.liquidSubmeshes.size());
    for (const AdtSubmesh& s : tile.liquidSubmeshes)
    {
        ModelSubmeshGpu m = {};
        m.indexStart = s.indexStart;
        m.indexCount = s.indexCount;
        m.textureIndex = s.layerTex[0];   // liquid frame base
        m.blendMode = s.blendMode;
        m.materialFlags = s.materialFlags;
        m.priorityPlane = 1;
        m.center[0] = s.center[0]; m.center[1] = s.center[1]; m.center[2] = s.center[2];
        up.submeshes.push_back(m);
    }

    up.textures.resize(tile.liquidTexturePaths.size());
    for (size_t i = 0; i < tile.liquidTexturePaths.size(); ++i)
    {
        if (tile.liquidTexturePaths[i].empty())
            continue;
        BlpImage img = DecodeBlp(cd.ReadFile(tile.liquidTexturePaths[i]));
        if (img.valid())
        {
            up.textures[i].rgba = std::move(img.rgba);
            up.textures[i].w = img.width;
            up.textures[i].h = img.height;
        }
    }
    return up;
}

ModelUpload BuildWdlUpload(const WdlTile& tile, const glm::vec3& origin)
{
    ModelUpload up;
    if (tile.heights.size() != 545)
        return up;

    // Tile NW corner (grid row 0, col 0) in world coords; row/col increase toward -X/-Y (ADT order).
    const float nwx = kMapOrigin - tile.y * kTileSize;
    const float nwy = kMapOrigin - tile.x * kTileSize;
    auto outerIdx = [](int r, int c) { return r * 17 + c; };
    auto innerIdx = [](int r, int c) { return 289 + r * 16 + c; };

    up.vertices.resize(545);
    auto place = [&](int idx, float wx, float wy, int16_t h) {
        ModelVertexGpu& g = up.vertices[idx];
        g.pos[0] = wx - origin.x;
        g.pos[1] = wy - origin.y;
        g.pos[2] = (float)h;   // absolute height (Z is not origin-shifted, matching ADT terrain)
        g.normal[0] = 0; g.normal[1] = 0; g.normal[2] = 1;
        for (int k = 0; k < 4; ++k) { g.boneIndices[k] = 0; g.boneWeights[k] = 0.0f; }  // raw (no skin)
        // Height-tinted earthy ramp: low green -> tan -> grey -> snow.
        float t = std::clamp(((float)h + 200.0f) / 1400.0f, 0.0f, 1.0f);
        float r, gr, b;
        if (t < 0.4f)      { float u = t / 0.4f;         r = 0.24f + u * 0.26f; gr = 0.36f + u * 0.10f; b = 0.18f + u * 0.04f; }
        else if (t < 0.7f) { float u = (t - 0.4f) / 0.3f; r = 0.50f + u * 0.05f; gr = 0.46f - u * 0.02f; b = 0.22f + u * 0.13f; }
        else               { float u = (t - 0.7f) / 0.3f; r = 0.55f + u * 0.40f; gr = 0.44f + u * 0.51f; b = 0.35f + u * 0.60f; }
        g.color[0] = (uint8_t)std::clamp(r * 255.0f, 0.0f, 255.0f);
        g.color[1] = (uint8_t)std::clamp(gr * 255.0f, 0.0f, 255.0f);
        g.color[2] = (uint8_t)std::clamp(b * 255.0f, 0.0f, 255.0f);
        g.color[3] = 255;
    };
    for (int r = 0; r <= 16; ++r)
        for (int c = 0; c <= 16; ++c)
            place(outerIdx(r, c), nwx - r * kChunkSize, nwy - c * kChunkSize, tile.heights[outerIdx(r, c)]);
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c)
            place(innerIdx(r, c), nwx - (r + 0.5f) * kChunkSize, nwy - (c + 0.5f) * kChunkSize,
                  tile.heights[innerIdx(r, c)]);

    // 16x16 cells, 4 triangles each fanning around the inner center vertex.
    up.indices.reserve(16 * 16 * 12);
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c)
        {
            uint32_t tl = outerIdx(r, c), tr = outerIdx(r, c + 1);
            uint32_t bl = outerIdx(r + 1, c), br = outerIdx(r + 1, c + 1);
            uint32_t ct = innerIdx(r, c);
            const uint32_t tris[12] = {tl, tr, ct, tr, br, ct, br, bl, ct, bl, tl, ct};
            for (uint32_t i : tris) up.indices.push_back(i);
        }

    // Smooth normals: accumulate face normals into the shared vertices.
    std::vector<glm::vec3> nacc(545, glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < up.indices.size(); i += 3)
    {
        const uint32_t a = up.indices[i], b2 = up.indices[i + 1], c2 = up.indices[i + 2];
        glm::vec3 pa(up.vertices[a].pos[0], up.vertices[a].pos[1], up.vertices[a].pos[2]);
        glm::vec3 pb(up.vertices[b2].pos[0], up.vertices[b2].pos[1], up.vertices[b2].pos[2]);
        glm::vec3 pc(up.vertices[c2].pos[0], up.vertices[c2].pos[1], up.vertices[c2].pos[2]);
        glm::vec3 fn = glm::cross(pb - pa, pc - pa);
        nacc[a] += fn; nacc[b2] += fn; nacc[c2] += fn;
    }
    for (int i = 0; i < 545; ++i)
    {
        glm::vec3 n = glm::length(nacc[i]) > 1e-6f ? glm::normalize(nacc[i]) : glm::vec3(0, 0, 1);
        up.vertices[i].normal[0] = n.x; up.vertices[i].normal[1] = n.y; up.vertices[i].normal[2] = n.z;
    }

    ModelSubmeshGpu sm = {};
    sm.indexStart = 0;
    sm.indexCount = (uint32_t)up.indices.size();
    sm.textureIndex = -1;   // -> white; the per-vertex color is the terrain tint
    sm.blendMode = 0;       // opaque, writes depth
    up.submeshes.push_back(sm);
    up.boneCount = 1;
    return up;
}
} // namespace we::adt
