// WmoLoader — see WmoLoader.h.

#include "wmo/WmoLoader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "clientdata/ClientData.h"
#include "util/ByteReader.h"

namespace we::wmo
{
namespace
{
bool Fail(std::string* error, const char* msg)
{
    if (error)
        *error = msg;
    return false;
}

// "World\wmo\Foo\Bar.wmo" + group i -> "World\wmo\Foo\Bar_000.wmo".
std::string GroupPath(const std::string& rootPath, int i)
{
    size_t dot = rootPath.find_last_of('.');
    std::string base = (dot == std::string::npos) ? rootPath : rootPath.substr(0, dot);
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "_%03d.wmo", i);
    return base + suffix;
}

// WMO EGxBlend -> our 0..6 mesh blend modes.
uint16_t MapWmoBlend(uint32_t b)
{
    switch (b)
    {
        case 0: return 0;   // opaque
        case 1: return 1;   // alpha key
        case 2: return 2;   // alpha
        case 3: return 4;   // additive
        case 4: return 5;   // mod
        case 5: return 6;   // mod2x
        default: return 0;
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

// Resolve MOGP.groupLiquid to a LiquidType.dbc id, per wowdev's algorithm. Legacy WMOs use
// groupLiquid 15 ("green lava") as a marker to take the real type from each tile's low-nibble
// `legacyLiquidType`; a group's basic type (water/ocean/magma/slime) maps to the WMO-liquid
// DBC ids 13/14/19/20 (water becomes ocean when the group's is_not_water_but_ocean flag set).
uint32_t ResolveLiquidTypeId(uint32_t groupLiquid, uint32_t mohdFlags, uint32_t groupFlags,
                             uint32_t tileLegacyType)
{
    const bool ocean = (groupFlags & 0x80000) != 0;   // SMOGroup::is_not_water_but_ocean
    auto toWmo = [&](uint32_t x) -> uint32_t {
        switch (x & 3)
        {
            case 0: return ocean ? 14u : 13u;   // WMO Water / WMO Ocean
            case 1: return 14u;                 // WMO Ocean
            case 2: return 19u;                 // WMO Magma
            default: return 20u;                // WMO Slime
        }
    };
    if (mohdFlags & 0x4)   // groupLiquid is a real DBC id (basic types still remap)
        return (groupLiquid < 21) ? toWmo(groupLiquid - 1) : groupLiquid;
    if (groupLiquid == 15)   // legacy "green lava": type comes from the tiles
        return toWmo(tileLegacyType);
    if (groupLiquid < 20)
        return toWmo(groupLiquid);
    return groupLiquid + 1;
}

// Build a translucent liquid surface from a group's MLIQ chunk: a height-mapped grid over
// the non-hidden tiles, textured + typed from LiquidType.dbc. Magma/slime vertices carry
// explicit UVs (SMOMVert s,t); water/ocean derive them. mliqOffset is relative to `sub`.
void BuildLiquid(const ByteReader& sub, size_t mliqOffset, uint32_t groupLiquid, uint32_t mohdFlags,
                 uint32_t groupFlags, const LiquidTypeTable* liquidTypes, ClientData& cd,
                 std::unordered_map<std::string, std::pair<int, int>>& frameCache, WmoModel& out)
{
    WmoLiquidHeader lh{};
    if (!sub.Get(mliqOffset, lh))
        return;
    const uint32_t xv = lh.xVerts, yv = lh.yVerts, xt = lh.xTiles, yt = lh.yTiles;
    if (xv == 0 || yv == 0 || xt == 0 || yt == 0)
        return;

    const size_t nLiqVerts = static_cast<size_t>(xv) * yv;
    const size_t vertsOffset = mliqOffset + sizeof(WmoLiquidHeader);
    std::vector<uint8_t> vraw;   // 8 bytes/vertex: {magma int16 s,t | water flow} then float height
    if (!sub.GetArrayAt<uint8_t>(vertsOffset, nLiqVerts * 8, vraw))
        return;
    std::vector<uint8_t> tileFlags;
    sub.GetArrayAt<uint8_t>(vertsOffset + nLiqVerts * 8, static_cast<size_t>(xt) * yt, tileFlags);

    const float unit = 4.1666666f;   // WMO liquid tile size (yards)
    auto heightAt = [&](uint32_t col, uint32_t row) {
        const size_t idx = static_cast<size_t>(row) * xv + col;
        float h = 0.0f;
        std::memcpy(&h, vraw.data() + idx * 8 + 4, 4);
        return h;
    };

    // Resolve the type (green-lava marker -> the tiles' legacy nibble) + animated texture.
    uint32_t tileLegacyType = 0;
    for (uint8_t f : tileFlags)
    {
        uint8_t lt = f & 0x0F;
        if (lt != 0x0F) { tileLegacyType = lt; break; }
    }
    const uint32_t dbcId = ResolveLiquidTypeId(groupLiquid, mohdFlags, groupFlags, tileLegacyType);
    const DbcStore::LiquidTypeInfo* info = nullptr;
    if (liquidTypes)
    {
        auto it = liquidTypes->find(dbcId);
        if (it != liquidTypes->end()) info = &it->second;
    }
    const uint32_t category = info ? info->category : 0u;   // default water
    const bool magmaLike = (category == 2 || category == 3);   // magma/slime -> SMOMVert UVs

    // Load all animation frames of the liquid texture (deduped per pattern). The DBC pattern
    // "...\lava.%d.blp" resolves to lava.1.blp .. lava.N.blp; probe until a frame is missing.
    int liquidTex = -1, frameBase = -1, frameCount = 1;
    if (info && !info->texture.empty())
    {
        auto cached = frameCache.find(info->texture);
        if (cached != frameCache.end())
        {
            frameBase = cached->second.first;
            frameCount = cached->second.second;
        }
        else
        {
            frameBase = static_cast<int>(out.texturePaths.size());
            if (info->texture.find("%d") == std::string::npos)
            {
                out.texturePaths.push_back(info->texture);
                frameCount = 1;
            }
            else
            {
                int f = 1;
                for (; f <= 60; ++f)
                {
                    std::string path = LiquidFramePath(info->texture, f);
                    if (!cd.HasFile(path))
                        break;
                    out.texturePaths.push_back(std::move(path));
                }
                frameCount = f - 1;
                if (frameCount <= 0) { out.texturePaths.push_back(LiquidFramePath(info->texture, 1)); frameCount = 1; }
            }
            frameCache[info->texture] = {frameBase, frameCount};
        }
        liquidTex = frameBase;
    }

    // Colour + opacity. Liquid opacity comes from the vertex alpha, NOT the texture's alpha
    // (which is a ripple mask) — the shader is told to ignore texture alpha for liquid.
    // Water tints the grey ripple texture blue; magma/slime textures are already coloured.
    uint8_t tr, tg, tb, ta;
    if (category == 2)      { tr = 255; tg = 255; tb = 255; ta = 240; }   // magma: opaque, glowing
    else if (category == 3) { tr = 255; tg = 255; tb = 255; ta = 150; }   // slime: translucent
    else                    { tr = 150; tg = 195; tb = 235; ta = 130; }   // water/ocean: blue, see-through
    if (liquidTex < 0 && category == 2) { tr = 255; tg = 90; tb = 25; }   // untextured magma fallback

    // Per-vertex UVs: magma/slime store int16 s,t (in 1/256 texel units); water/ocean have
    // no stored UV, so derive one from the grid at the same ~0.5 tiles/repeat density.
    auto uvAt = [&](uint32_t col, uint32_t row, float& u, float& v) {
        if (magmaLike)
        {
            const size_t idx = static_cast<size_t>(row) * xv + col;
            int16_t s = 0, t = 0;
            std::memcpy(&s, vraw.data() + idx * 8 + 0, 2);
            std::memcpy(&t, vraw.data() + idx * 8 + 2, 2);
            u = s / 256.0f; v = t / 256.0f;
        }
        else { u = col * 0.5f; v = row * 0.5f; }
    };

    const uint32_t vertexBase = static_cast<uint32_t>(out.vertices.size());
    for (uint32_t row = 0; row < yv; ++row)
        for (uint32_t col = 0; col < xv; ++col)
        {
            WmoVertex v;
            v.pos[0] = lh.baseCoords[0] + col * unit;
            v.pos[1] = lh.baseCoords[1] + row * unit;
            v.pos[2] = heightAt(col, row);
            v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = 1;
            uvAt(col, row, v.uv[0], v.uv[1]);
            v.color[0] = tr; v.color[1] = tg; v.color[2] = tb; v.color[3] = ta;
            out.vertices.push_back(v);
        }

    const uint32_t indexBase = static_cast<uint32_t>(out.indices.size());
    int tiles = 0;
    for (uint32_t row = 0; row < yt; ++row)
        for (uint32_t col = 0; col < xt; ++col)
        {
            const size_t fi = static_cast<size_t>(row) * xt + col;
            const uint8_t fl = fi < tileFlags.size() ? tileFlags[fi] : 0x08;
            if (fl & 0x08)   // tile has no liquid / hidden
                continue;
            const uint32_t v00 = vertexBase + row * xv + col;
            const uint32_t v10 = v00 + 1, v01 = v00 + xv, v11 = v01 + 1;
            out.indices.push_back(v00); out.indices.push_back(v10); out.indices.push_back(v11);
            out.indices.push_back(v00); out.indices.push_back(v11); out.indices.push_back(v01);
            ++tiles;
        }
    if (tiles == 0)
        return;

    WmoSubmesh s;
    s.indexStart = indexBase;
    s.indexCount = static_cast<uint32_t>(out.indices.size()) - indexBase;
    s.textureIndex = liquidTex;             // DBC liquid texture (frame 1), or -1 -> tinted sheet
    s.blendMode = 2;                        // alpha
    s.materialFlags = 0x01 | 0x04 | 0x08;   // unlit + two-sided + liquid (ignore texture alpha)
    s.priorityPlane = 1;                    // draw after the opaque shell
    s.isLiquid = true;
    s.liquidFrameBase = frameBase;
    s.liquidFrameCount = frameCount;
    s.center[0] = lh.baseCoords[0] + xt * unit * 0.5f;
    s.center[1] = lh.baseCoords[1] + yt * unit * 0.5f;
    s.center[2] = heightAt(xv / 2, yv / 2);
    out.submeshes.push_back(s);
    out.liquidTileCount += tiles;
}

// Parse one group file's MOGP geometry and append it (batches -> submeshes) to `out`.
void ParseGroup(const std::vector<uint8_t>& bytes, const std::vector<WmoMaterial>& materials,
                bool includeLiquid, uint32_t mohdFlags, const LiquidTypeTable* liquidTypes,
                ClientData& cd, std::unordered_map<std::string, std::pair<int, int>>& frameCache,
                WmoModel& out)
{
    ByteReader gr(bytes);

    // Find the MOGP chunk (group files are MVER then MOGP).
    ChunkIter it(gr);
    Chunk c, mogp;
    bool found = false;
    while (it.Next(c))
        if (c.Is("MOGP")) { mogp = c; found = true; break; }
    if (!found || mogp.size < sizeof(WmoGroupHeader))
        return;

    WmoGroupHeader gh{};
    gr.Get(mogp.offset, gh);

    // The geometry sub-chunks live inside the MOGP payload, after its 68-byte header.
    ByteReader sub(gr.data + mogp.offset + sizeof(WmoGroupHeader), mogp.size - sizeof(WmoGroupHeader));

    std::vector<float>    movt, monr, motv;   // positions(vec3), normals(vec3), uvs(vec2)
    std::vector<uint16_t> movi;               // indices
    std::vector<WmoBatch> moba;               // batches
    std::vector<uint32_t> mocv;               // vertex colors (BGRA)
    size_t mliqOffset = 0;                     // relative to `sub`
    bool   hasMliq = false;

    ChunkIter git(sub);
    while (git.Next(c))
    {
        if (c.Is("MOVT"))      sub.GetArrayAt<float>(c.offset, c.size / 4, movt);
        else if (c.Is("MONR")) sub.GetArrayAt<float>(c.offset, c.size / 4, monr);
        else if (c.Is("MOTV")) sub.GetArrayAt<float>(c.offset, c.size / 4, motv);
        else if (c.Is("MOVI")) sub.GetArrayAt<uint16_t>(c.offset, c.size / 2, movi);
        else if (c.Is("MOBA")) sub.GetArrayAt<WmoBatch>(c.offset, c.size / sizeof(WmoBatch), moba);
        else if (c.Is("MOCV")) sub.GetArrayAt<uint32_t>(c.offset, c.size / 4, mocv);
        else if (c.Is("MLIQ")) { mliqOffset = c.offset; hasMliq = true; }
        else if (c.Is("MOLT"))
        {
            std::vector<WmoLight> lts;
            sub.GetArrayAt<WmoLight>(c.offset, c.size / sizeof(WmoLight), lts);
            for (const WmoLight& L : lts)
            {
                WmoLightInfo li;
                li.pos[0] = L.position[0]; li.pos[1] = L.position[1]; li.pos[2] = L.position[2];
                li.rgb[0] = ((L.color >> 16) & 0xFF) / 255.0f;   // BGRA -> RGB
                li.rgb[1] = ((L.color >> 8) & 0xFF) / 255.0f;
                li.rgb[2] = (L.color & 0xFF) / 255.0f;
                li.intensity = L.intensity;
                li.radius = L.attenEnd > 0.5f ? L.attenEnd : 12.0f;
                out.lights.push_back(li);
            }
        }
    }

    if (includeLiquid && hasMliq)
        BuildLiquid(sub, mliqOffset, gh.groupLiquid, mohdFlags, gh.flags, liquidTypes, cd, frameCache, out);

    const size_t nVerts = movt.size() / 3;
    if (nVerts == 0 || movi.empty() || moba.empty())
        return;
    // MOCV is baked lighting only for INTERIOR groups. EXTERIOR groups (flag 0x8) are lit
    // by the outdoor sun and store a near-zero MOCV placeholder, so using it as unlit
    // vertex color turns them black — those get directional lighting with white instead.
    const bool exterior = (gh.flags & 0x8) != 0;
    const bool useMocv = mocv.size() >= nVerts && !exterior;

    const uint32_t vertexBase = static_cast<uint32_t>(out.vertices.size());
    out.vertices.reserve(out.vertices.size() + nVerts);
    for (size_t k = 0; k < nVerts; ++k)
    {
        WmoVertex v;
        v.pos[0] = movt[k * 3 + 0]; v.pos[1] = movt[k * 3 + 1]; v.pos[2] = movt[k * 3 + 2];
        if (monr.size() >= (k + 1) * 3)
        { v.normal[0] = monr[k * 3 + 0]; v.normal[1] = monr[k * 3 + 1]; v.normal[2] = monr[k * 3 + 2]; }
        else
        { v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = 1; }
        if (motv.size() >= (k + 1) * 2)
        { v.uv[0] = motv[k * 2 + 0]; v.uv[1] = motv[k * 2 + 1]; }
        if (useMocv)
        {
            uint32_t p = mocv[k];   // CImVector: bytes B,G,R,A
            v.color[0] = (p >> 16) & 0xFF;   // R
            v.color[1] = (p >> 8) & 0xFF;    // G
            v.color[2] = p & 0xFF;           // B
            v.color[3] = 255;
        }
        out.vertices.push_back(v);
    }

    const uint32_t indexBase = static_cast<uint32_t>(out.indices.size());
    out.indices.reserve(out.indices.size() + movi.size());
    for (uint16_t idx : movi)
        out.indices.push_back(vertexBase + idx);

    for (const WmoBatch& b : moba)
    {
        WmoSubmesh s;
        s.indexStart = indexBase + b.startIndex;
        s.indexCount = b.count;
        if (s.indexStart + s.indexCount > out.indices.size())
            continue;   // range escapes the buffer — skip rather than draw OOB
        if (b.materialId < materials.size())
        {
            const WmoMaterial& m = materials[b.materialId];
            s.textureIndex = static_cast<int>(b.materialId);
            s.blendMode = MapWmoBlend(m.blendMode);
            uint16_t flags = 0;
            if (m.flags & kWmoMatUnculled) flags |= 0x04;   // two-sided
            if (useMocv) flags |= 0x01;                     // baked lighting -> skip directional
            s.materialFlags = flags;
        }
        s.center[0] = 0.5f * (b.bbMin[0] + b.bbMax[0]);
        s.center[1] = 0.5f * (b.bbMin[1] + b.bbMax[1]);
        s.center[2] = 0.5f * (b.bbMin[2] + b.bbMax[2]);
        out.submeshes.push_back(s);
    }
}

std::string ToLower(std::string s)
{
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// Doodad names in MODN are historically ".MDX" or ".MDL"; the 3.3.5a client stores ".m2".
std::string NormalizeMdx(std::string p)
{
    if (p.size() >= 4)
    {
        std::string ext = ToLower(p.substr(p.size() - 4));
        if (ext == ".mdx" || ext == ".mdl")
            p = p.substr(0, p.size() - 4) + ".m2";
    }
    return p;
}

// Resolve the selected doodad set's placements into a list of instances (M2 path + world
// transform). The M2 geometry is NOT loaded here — the viewer loads each unique model once
// and renders these as live animated instances. Global set 0 is always included.
void ResolveDoodadInstances(const std::vector<uint8_t>& modn, const std::vector<WmoDoodadDef>& modd,
                            const std::vector<WmoDoodadSetInfo>& sets, const WmoLoadOptions& opt,
                            WmoModel& out)
{
    if (modd.empty() || sets.empty())
        return;

    int selected = opt.doodadSet;
    if (selected < 0)
    {
        selected = 0;
        for (size_t i = 1; i < sets.size(); ++i)
            if (sets[i].count > 0) { selected = static_cast<int>(i); break; }
    }

    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    auto addSet = [&](int s) {
        if (s >= 0 && s < static_cast<int>(sets.size()) && sets[s].count > 0)
            ranges.emplace_back(sets[s].first, sets[s].first + sets[s].count);
    };
    addSet(0);
    if (selected > 0) addSet(selected);
    if (ranges.empty())
        return;

    ByteReader modnReader(modn);
    for (const auto& range : ranges)
        for (uint32_t di = range.first; di < range.second && di < modd.size(); ++di)
        {
            const WmoDoodadDef& d = modd[di];
            std::string name = NormalizeMdx(modnReader.GetCString(d.nameOffsetAndFlags & 0x00FFFFFF));
            if (name.empty())
                continue;

            glm::quat q(d.rotation[3], d.rotation[0], d.rotation[1], d.rotation[2]);
            glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(d.position[0], d.position[1], d.position[2])) *
                          glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), glm::vec3(d.scale));

            WmoDoodadInstance inst;
            inst.m2Path = std::move(name);
            std::memcpy(inst.transform, &M[0][0], sizeof(inst.transform));
            inst.origin[0] = d.position[0]; inst.origin[1] = d.position[1]; inst.origin[2] = d.position[2];
            inst.color[0] = inst.color[1] = inst.color[2] = inst.color[3] = 255;
            if (d.color & 0x00FFFFFF)
            {
                inst.color[0] = (d.color >> 16) & 0xFF;   // R
                inst.color[1] = (d.color >> 8) & 0xFF;    // G
                inst.color[2] = d.color & 0xFF;           // B
            }
            out.doodadInstances.push_back(std::move(inst));
        }
}
} // namespace

bool Load(ClientData& cd, const std::string& rootPath, WmoModel& out, const WmoLoadOptions& opt,
          const LiquidTypeTable* liquidTypes, std::string* error)
{
    out = WmoModel{};
    out.name = rootPath;

    std::vector<uint8_t> rootBytes = cd.ReadFile(rootPath);
    if (rootBytes.empty())
        return Fail(error, "wmo root file missing or empty");
    ByteReader rr(rootBytes);

    // Root chunks.
    WmoHeader hdr{};
    bool haveHdr = false;
    std::vector<uint8_t>      motx;        // texture-name blob
    std::vector<WmoMaterial>  materials;
    std::vector<WmoDoodadSet> doodadSets;
    std::vector<uint8_t>      modn;        // doodad-name blob
    std::vector<WmoDoodadDef> modd;        // doodad instances

    ChunkIter it(rr);
    Chunk c;
    while (it.Next(c))
    {
        if (c.Is("MOHD"))      haveHdr = rr.Get(c.offset, hdr);
        else if (c.Is("MOTX")) rr.GetArrayAt<uint8_t>(c.offset, c.size, motx);
        else if (c.Is("MOMT")) rr.GetArrayAt<WmoMaterial>(c.offset, c.size / sizeof(WmoMaterial), materials);
        else if (c.Is("MODS")) rr.GetArrayAt<WmoDoodadSet>(c.offset, c.size / sizeof(WmoDoodadSet), doodadSets);
        else if (c.Is("MODN")) rr.GetArrayAt<uint8_t>(c.offset, c.size, modn);
        else if (c.Is("MODD")) rr.GetArrayAt<WmoDoodadDef>(c.offset, c.size / sizeof(WmoDoodadDef), modd);
    }
    if (!haveHdr)
        return Fail(error, "wmo root missing MOHD");

    out.materialCount = static_cast<int>(materials.size());
    out.doodadDefCount = static_cast<int>(hdr.nDoodadDefs);

    // Resolve one texture path per material (submeshes reference by material index).
    ByteReader motxReader(motx);
    out.texturePaths.reserve(materials.size());
    for (const WmoMaterial& m : materials)
        out.texturePaths.push_back(motxReader.GetCString(m.texture1));

    // Doodad-set metadata (for the viewer's set picker; baking is a later phase).
    for (const WmoDoodadSet& d : doodadSets)
    {
        WmoDoodadSetInfo info;
        info.name.assign(d.name, strnlen(d.name, sizeof(d.name)));
        info.first = d.firstInstanceIndex;
        info.count = d.numDoodads;
        out.doodadSets.push_back(std::move(info));
    }

    // Group files.
    std::unordered_map<std::string, std::pair<int, int>> frameCache;   // liquid pattern -> {base, count}
    for (uint32_t g = 0; g < hdr.nGroups; ++g)
    {
        std::vector<uint8_t> gb = cd.ReadFile(GroupPath(rootPath, static_cast<int>(g)));
        if (gb.empty())
            continue;   // tolerate a missing group rather than failing the whole WMO
        ParseGroup(gb, materials, opt.liquid, hdr.flags, liquidTypes, cd, frameCache, out);
        ++out.groupCount;
    }

    if (out.vertices.empty() || out.indices.empty())
        return Fail(error, "wmo has no drawable geometry");

    // MOLT interior lights: a modest additive point-light pass over the shell vertices. MOCV
    // already bakes most interior lighting, so this is deliberately subtle — it mainly warms
    // brazier/torch areas and gives MOCV-less groups some local shape.
    if (!out.lights.empty())
    {
        for (WmoVertex& v : out.vertices)
        {
            glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
            glm::vec3 add(0.0f);
            for (const WmoLightInfo& L : out.lights)
            {
                float d = glm::length(p - glm::vec3(L.pos[0], L.pos[1], L.pos[2]));
                if (d >= L.radius) continue;
                float a = 1.0f - d / L.radius;
                add += glm::vec3(L.rgb[0], L.rgb[1], L.rgb[2]) * (a * a * std::min(L.intensity, 1.5f));
            }
            const float k = 90.0f;   // additive strength in 0..255 space
            v.color[0] = (uint8_t)std::min(255.0f, v.color[0] + add.x * k);
            v.color[1] = (uint8_t)std::min(255.0f, v.color[1] + add.y * k);
            v.color[2] = (uint8_t)std::min(255.0f, v.color[2] + add.z * k);
        }
    }

    // Doodads (embedded M2 props): resolve their placements; the viewer loads + animates them.
    if (opt.doodads)
        ResolveDoodadInstances(modn, modd, out.doodadSets, opt, out);

    // Bounds: prefer the root bounding box, else derive from vertices.
    glm::vec3 lo(hdr.boundingBoxMin[0], hdr.boundingBoxMin[1], hdr.boundingBoxMin[2]);
    glm::vec3 hi(hdr.boundingBoxMax[0], hdr.boundingBoxMax[1], hdr.boundingBoxMax[2]);
    if (!(hi.x > lo.x))   // degenerate/absent -> compute from geometry
    {
        lo = glm::vec3(out.vertices[0].pos[0], out.vertices[0].pos[1], out.vertices[0].pos[2]);
        hi = lo;
        for (const WmoVertex& v : out.vertices)
        {
            glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
    }
    out.boundsCenter = (lo + hi) * 0.5f;
    out.boundsRadius = glm::max(glm::length(hi - out.boundsCenter), 0.01f);

    return true;
}
} // namespace we::wmo
