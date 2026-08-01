// AdtUploadBuild — see AdtUploadBuild.h.

#include "adt/AdtUploadBuild.h"

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

TerrainUpload BuildTerrainUpload(ClientData& cd, const AdtTile& tile)
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

    // Unique ground textures (shared across chunks).
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
} // namespace we::adt
