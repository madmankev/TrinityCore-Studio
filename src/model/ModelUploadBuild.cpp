// ModelUploadBuild — see ModelUploadBuild.h.

#include "model/ModelUploadBuild.h"

#include <algorithm>

#include "clientdata/BlpDecoder.h"
#include "clientdata/ClientData.h"

namespace we::m2
{
ModelUpload BuildUpload(ClientData& cd, const M2Model& model)
{
    ModelUpload up;

    up.vertices.reserve(model.vertices.size());
    for (const M2Vertex& v : model.vertices)
    {
        ModelVertexGpu g = {};
        g.pos[0] = v.pos[0]; g.pos[1] = v.pos[1]; g.pos[2] = v.pos[2];
        g.normal[0] = v.normal[0]; g.normal[1] = v.normal[1]; g.normal[2] = v.normal[2];
        g.uv[0] = v.uv[0]; g.uv[1] = v.uv[1];
        for (int i = 0; i < 4; ++i)
        {
            g.boneIndices[i] = v.boneIndices[i];
            g.boneWeights[i] = v.boneWeights[i] / 255.0f;
        }
        g.color[0] = g.color[1] = g.color[2] = g.color[3] = 255;   // no vertex color for M2
        up.vertices.push_back(g);
    }

    up.indices = model.indices;
    up.boneCount = static_cast<uint32_t>(std::min<size_t>(model.bones.size(), 256));

    up.submeshes.reserve(model.batches.size());
    for (const RenderBatch& b : model.batches)
    {
        ModelSubmeshGpu s = {};
        s.indexStart = b.indexStart;
        s.indexCount = b.indexCount;
        s.textureIndex = b.textureIndex;
        s.blendMode = b.blendMode;
        s.materialFlags = b.materialFlags;
        s.priorityPlane = b.priorityPlane;
        s.center[0] = b.center[0];
        s.center[1] = b.center[1];
        s.center[2] = b.center[2];
        up.submeshes.push_back(s);
    }

    // One texture entry per M2 texture slot (parallel to model.texturePaths, which is
    // what RenderBatch::textureIndex references). Empty/undecodable slots stay blank so
    // the renderer falls back to its white texture (e.g. runtime creature skins).
    up.textures.resize(model.texturePaths.size());
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
    {
        if (model.texturePaths[i].empty())
            continue;
        BlpImage img = DecodeBlp(cd.ReadFile(model.texturePaths[i]));
        if (img.valid())
        {
            up.textures[i].rgba = std::move(img.rgba);
            up.textures[i].w = img.width;
            up.textures[i].h = img.height;
        }
    }

    // Append geometry-model particle textures after the main slots. The loader assigned
    // each referenced model a textureBase equal to the running texture count here, so a
    // baked submesh's final index (textureBase + localTexture) lands on the right entry.
    for (const GeoParticleModel& g : model.geoParticleModels)
    {
        for (const GeoParticleModel::Tex& t : g.textures)
        {
            ModelTextureGpu mt;
            mt.rgba = t.rgba;   // copy (the model retains its own for re-uploads/skin swaps)
            mt.w = t.w;
            mt.h = t.h;
            up.textures.push_back(std::move(mt));
        }
    }
    return up;
}
} // namespace we::m2
