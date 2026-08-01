// M2Loader — see M2Loader.h.

#include "model/M2Loader.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

#include "clientdata/BlpDecoder.h"
#include "clientdata/ClientData.h"

namespace we::m2
{
namespace
{
// Bounds-checked view over a byte buffer.
struct Reader
{
    const uint8_t* data = nullptr;
    size_t size = 0;

    bool In(uint32_t offset, size_t bytes) const
    {
        return offset <= size && bytes <= size - offset;
    }

    // Copy a POD at `offset`. Returns false if out of range.
    template <class T>
    bool Get(uint32_t offset, T& out) const
    {
        if (!In(offset, sizeof(T)))
            return false;
        std::memcpy(&out, data + offset, sizeof(T));
        return true;
    }

    // Read `arr.count` elements of type T at `arr.offset` into `out`.
    template <class T>
    bool GetArray(const M2Array& arr, std::vector<T>& out) const
    {
        const size_t total = static_cast<size_t>(arr.count) * sizeof(T);
        if (!In(arr.offset, total))
            return false;
        out.resize(arr.count);
        if (arr.count)
            std::memcpy(out.data(), data + arr.offset, total);
        return true;
    }

    std::string GetString(const M2Array& arr) const
    {
        if (!In(arr.offset, arr.count) || arr.count == 0)
            return {};
        const char* p = reinterpret_cast<const char*>(data + arr.offset);
        size_t len = arr.count;
        while (len > 0 && p[len - 1] == '\0')  // trim trailing NUL(s)
            --len;
        return std::string(p, len);
    }
};

std::string Fail(std::string* error, const char* msg)
{
    if (error)
        *error = msg;
    return {};
}

// "Creature\Rat\Rat.m2" -> "Creature\Rat\Rat00.skin" (strip extension, append 00.skin).
std::string SkinPath(const std::string& m2Path)
{
    size_t dot = m2Path.find_last_of('.');
    std::string base = (dot == std::string::npos) ? m2Path : m2Path.substr(0, dot);
    return base + "00.skin";
}

std::string ToLower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Internal geometry-model filenames are historically ".mdx"; the 3.3.5a client stores ".m2".
std::string NormalizeModelPath(std::string p)
{
    if (p.size() >= 4 && ToLower(p.substr(p.size() - 4)) == ".mdx")
        p = p.substr(0, p.size() - 4) + ".m2";
    return p;
}

// Depth guard so a geometry-model particle's own emitters don't recurse forever.
thread_local int g_loadDepth = 0;
} // namespace

// Forward decl: geometry-particle resolution runs at the end of a top-level Load.
namespace { void ResolveGeoParticles(ClientData& cd, M2Model& out); }

bool Load(ClientData& cd, const std::string& m2Path, M2Model& out, std::string* error)
{
    out = M2Model{};

    ++g_loadDepth;
    struct DepthGuard { ~DepthGuard() { --g_loadDepth; } } depthGuard;

    out.m2Bytes = cd.ReadFile(m2Path);
    if (out.m2Bytes.empty())
    {
        Fail(error, "m2 file missing or empty");
        return false;
    }
    Reader r{out.m2Bytes.data(), out.m2Bytes.size()};

    M2Header h{};
    if (!r.Get(0, h))
    {
        Fail(error, "m2 too small for header");
        return false;
    }
    if (std::memcmp(h.id, "MD20", 4) != 0)
    {
        Fail(error, "not an MD20 (m2) file");
        return false;
    }

    // Vertices.
    if (!r.GetArray(h.vertices, out.vertices) || out.vertices.empty())
    {
        Fail(error, "no vertices");
        return false;
    }

    // Textures (resolve type==0 filenames).
    std::vector<M2TextureDef> texDefs;
    r.GetArray(h.textures, texDefs);
    out.texturePaths.reserve(texDefs.size());
    out.textureTypes.reserve(texDefs.size());
    for (const M2TextureDef& t : texDefs)
    {
        out.textureTypes.push_back(t.type);
        out.texturePaths.push_back(t.type == 0 ? r.GetString(t.filename) : std::string{});
    }

    // Texture lookup + materials (render flags).
    std::vector<uint16_t> texLookup;
    r.GetArray(h.texLookup, texLookup);
    std::vector<M2Material> materials;
    r.GetArray(h.renderFlags, materials);

    // Skeleton + animation metadata (decoded lazily later; kept raw here).
    r.GetArray(h.bones, out.bones);
    r.GetArray(h.boneLookupTable, out.boneLookup);
    r.GetArray(h.animations, out.sequences);
    r.GetArray(h.globalSequences, out.globalSequenceDurations);

    // Effects + mesh-animation data (their sub-tracks are read lazily from m2Bytes).
    r.GetArray(h.particleEmitters, out.particleEmitters);
    r.GetArray(h.ribbonEmitters, out.ribbonEmitters);
    r.GetArray(h.textureAnimations, out.textureTransforms);
    r.GetArray(h.colors, out.colors);
    r.GetArray(h.transparency, out.transparencies);
    r.GetArray(h.texAnimLookup, out.texAnimLookup);
    r.GetArray(h.transLookup, out.transLookup);

    // --- .skin ---
    std::vector<uint8_t> skinBytes = cd.ReadFile(SkinPath(m2Path));
    if (skinBytes.empty())
    {
        Fail(error, "companion .skin missing");
        return false;
    }
    Reader s{skinBytes.data(), skinBytes.size()};
    // External WotLK .skin files are prefixed with a 'SKIN' magic; the header proper
    // follows it. Offsets inside are absolute from the file start either way.
    const uint32_t skinHeaderOffset =
        (skinBytes.size() >= 4 && std::memcmp(skinBytes.data(), "SKIN", 4) == 0) ? 4u : 0u;
    SkinHeader sh{};
    if (!s.Get(skinHeaderOffset, sh))
    {
        Fail(error, ".skin too small for header");
        return false;
    }

    std::vector<uint16_t> skinVertexLookup;  // -> global .m2 vertex indices
    std::vector<uint16_t> skinTriangles;     // -> indices into skinVertexLookup
    std::vector<M2SkinSection> submeshes;
    std::vector<M2Batch> batches;
    if (!s.GetArray(sh.indices, skinVertexLookup) || !s.GetArray(sh.triangles, skinTriangles) ||
        !s.GetArray(sh.submeshes, submeshes) || !s.GetArray(sh.batches, batches))
    {
        Fail(error, ".skin arrays out of range");
        return false;
    }

    // Resolve every triangle index to a global .m2 vertex index. The submeshes'
    // indexStart/indexCount then apply directly to out.indices (same order).
    const uint32_t vertCount = static_cast<uint32_t>(out.vertices.size());
    out.indices.reserve(skinTriangles.size());
    for (uint16_t tri : skinTriangles)
    {
        uint32_t global = (tri < skinVertexLookup.size()) ? skinVertexLookup[tri] : 0;
        if (global >= vertCount)
            global = 0;  // clamp defensively; malformed skins shouldn't crash the viewer
        out.indices.push_back(global);
    }

    // One RenderBatch per M2Batch (a submesh drawn with a texture + material).
    out.batches.reserve(batches.size());
    for (const M2Batch& b : batches)
    {
        if (b.skinSectionIndex >= submeshes.size())
            continue;
        const M2SkinSection& sub = submeshes[b.skinSectionIndex];

        RenderBatch rb;
        rb.indexStart = sub.indexStart + (static_cast<uint32_t>(sub.level) << 16);
        rb.indexCount = sub.indexCount;
        rb.submeshId = sub.skinSectionId;
        rb.priorityPlane = b.priorityPlane;
        rb.center[0] = sub.sortCenterPosition[0];
        rb.center[1] = sub.sortCenterPosition[1];
        rb.center[2] = sub.sortCenterPosition[2];
        if (rb.indexStart + rb.indexCount > out.indices.size())
            continue;  // range escapes the buffer — skip rather than read OOB

        if (b.textureComboIndex < texLookup.size())
        {
            uint16_t ti = texLookup[b.textureComboIndex];
            if (ti < out.texturePaths.size())
                rb.textureIndex = static_cast<int>(ti);
        }
        if (b.materialIndex < materials.size())
        {
            rb.blendMode = materials[b.materialIndex].blendMode;
            rb.materialFlags = materials[b.materialIndex].flags;
        }
        // Animation lookups: color is a direct index; texture-transform and weight go
        // through their lookup tables. 0xFFFF means "none".
        if (b.colorIndex != 0xFFFF && b.colorIndex < out.colors.size())
            rb.colorIndex = static_cast<int>(b.colorIndex);
        if (b.textureTransformComboIndex < out.texAnimLookup.size())
        {
            uint16_t ti = out.texAnimLookup[b.textureTransformComboIndex];
            if (ti < out.textureTransforms.size())
                rb.textureTransformIndex = static_cast<int>(ti);
        }
        if (b.textureWeightComboIndex < out.transLookup.size())
        {
            uint16_t wi = out.transLookup[b.textureWeightComboIndex];
            if (wi < out.transparencies.size())
                rb.textureWeightIndex = static_cast<int>(wi);
        }
        out.batches.push_back(rb);
    }
    if (out.batches.empty())
    {
        Fail(error, "no drawable batches");
        return false;
    }

    // Bounding sphere from the vertices (for camera framing).
    glm::vec3 lo(out.vertices[0].pos[0], out.vertices[0].pos[1], out.vertices[0].pos[2]);
    glm::vec3 hi = lo;
    for (const M2Vertex& v : out.vertices)
    {
        glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    out.boundsCenter = (lo + hi) * 0.5f;
    out.boundsRadius = glm::max(glm::length(hi - out.boundsCenter), 0.01f);

    // Resolve geometry-model particle meshes only at the top level (a referenced model's
    // own particle emitters are ignored to avoid unbounded recursion).
    if (g_loadDepth == 1)
        ResolveGeoParticles(cd, out);

    out.name = m2Path;
    return true;
}

namespace
{
void ResolveGeoParticles(ClientData& cd, M2Model& out)
{
    out.particleGeoModel.assign(out.particleEmitters.size(), -1);
    if (out.particleEmitters.empty())
        return;

    Reader r{out.m2Bytes.data(), out.m2Bytes.size()};
    std::unordered_map<std::string, int> byPath;   // normalized lower path -> geoModel index (or -1)
    int textureBase = static_cast<int>(out.texturePaths.size());

    for (size_t ei = 0; ei < out.particleEmitters.size(); ++ei)
    {
        std::string fn = r.GetString(out.particleEmitters[ei].geometryModelFilename);
        if (fn.empty())
            continue;
        fn = NormalizeModelPath(fn);
        std::string key = ToLower(fn);

        if (auto it = byPath.find(key); it != byPath.end())
        {
            out.particleGeoModel[ei] = it->second;   // already loaded (or known-missing = -1)
            continue;
        }

        M2Model gm;
        if (!Load(cd, fn, gm, nullptr))
        {
            byPath[key] = -1;   // remember the miss so we don't retry it
            continue;
        }

        GeoParticleModel g;
        g.verts.reserve(gm.vertices.size());
        for (const M2Vertex& v : gm.vertices)
        {
            GeoParticleModel::Vert vv;
            vv.pos[0] = v.pos[0]; vv.pos[1] = v.pos[1]; vv.pos[2] = v.pos[2];
            vv.uv[0] = v.uv[0]; vv.uv[1] = v.uv[1];
            g.verts.push_back(vv);
        }
        g.indices = gm.indices;
        g.subs.reserve(gm.batches.size());
        for (const RenderBatch& b : gm.batches)
            g.subs.push_back({b.indexStart, b.indexCount, b.textureIndex, b.blendMode});

        g.textures.resize(gm.texturePaths.size());
        for (size_t i = 0; i < gm.texturePaths.size(); ++i)
        {
            if (gm.texturePaths[i].empty())
                continue;
            BlpImage img = DecodeBlp(cd.ReadFile(gm.texturePaths[i]));
            if (img.valid())
            {
                g.textures[i].rgba = std::move(img.rgba);
                g.textures[i].w = img.width;
                g.textures[i].h = img.height;
            }
        }
        g.textureBase = textureBase;
        textureBase += static_cast<int>(g.textures.size());

        int idx = static_cast<int>(out.geoParticleModels.size());
        out.geoParticleModels.push_back(std::move(g));
        byPath[key] = idx;
        out.particleGeoModel[ei] = idx;
    }
}
} // namespace
} // namespace we::m2
