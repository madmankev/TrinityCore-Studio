// WmoUploadBuild — see WmoUploadBuild.h.

#include "wmo/WmoUploadBuild.h"

#include "clientdata/BlpDecoder.h"
#include "clientdata/ClientData.h"

namespace we::wmo
{
ModelUpload BuildUpload(ClientData& cd, const WmoModel& model)
{
    ModelUpload up;

    up.vertices.reserve(model.vertices.size());
    for (const WmoVertex& v : model.vertices)
    {
        ModelVertexGpu g = {};
        g.pos[0] = v.pos[0]; g.pos[1] = v.pos[1]; g.pos[2] = v.pos[2];
        g.normal[0] = v.normal[0]; g.normal[1] = v.normal[1]; g.normal[2] = v.normal[2];
        g.uv[0] = v.uv[0]; g.uv[1] = v.uv[1];
        // No skeleton: zero bone weights make the vertex shader pass position through.
        for (int i = 0; i < 4; ++i) { g.boneIndices[i] = 0; g.boneWeights[i] = 0.0f; }
        g.color[0] = v.color[0]; g.color[1] = v.color[1]; g.color[2] = v.color[2]; g.color[3] = v.color[3];
        up.vertices.push_back(g);
    }

    up.indices = model.indices;
    up.boneCount = 0;   // static geometry; CreateModel promotes this to a 1-entry dummy palette

    up.submeshes.reserve(model.submeshes.size());
    for (const WmoSubmesh& s : model.submeshes)
    {
        ModelSubmeshGpu m = {};
        m.indexStart = s.indexStart;
        m.indexCount = s.indexCount;
        m.textureIndex = s.textureIndex;
        m.blendMode = s.blendMode;
        m.materialFlags = s.materialFlags;
        m.priorityPlane = s.priorityPlane;
        m.center[0] = s.center[0]; m.center[1] = s.center[1]; m.center[2] = s.center[2];
        up.submeshes.push_back(m);
    }

    // One texture entry per slot (parallel to model.texturePaths, referenced by submesh
    // textureIndex). Empty/undecodable slots stay blank -> renderer's white fallback.
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
    return up;
}
} // namespace we::wmo
