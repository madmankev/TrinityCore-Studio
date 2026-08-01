#pragma once

// WmoTypes — WoW 3.3.5a (WMO version 17) World Map Object format: on-disk chunk structs
// (tightly packed; read straight from file bytes, so the sizes are load-bearing and
// static_assert'd) plus the decoded in-memory WmoModel the viewer renders.
//
// A WMO is a root file (Foo.wmo: MOHD/MOTX/MOMT/MODS/MODN/MODD/MOGI...) plus one group
// file per group (Foo_000.wmo, Foo_001.wmo, ...) that each hold a single MOGP chunk with
// the geometry sub-chunks nested inside (MOVT/MONR/MOTV/MOVI/MOBA/MOCV/MLIQ...).

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace we::wmo
{
// ---------------------------------------------------------------------------
// On-disk structures (tightly packed; read directly from the file bytes)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

// MOHD — root header (64 bytes).
struct WmoHeader
{
    uint32_t nMaterials;
    uint32_t nGroups;
    uint32_t nPortals;
    uint32_t nLights;
    uint32_t nDoodadNames;
    uint32_t nDoodadDefs;
    uint32_t nDoodadSets;
    uint32_t ambColor;          // CImVector (BGRA)
    uint32_t wmoId;             // WMOAreaTable id
    float    boundingBoxMin[3];
    float    boundingBoxMax[3];
    uint16_t flags;
    uint16_t numLod;            // WotLK: effectively padding
};
static_assert(sizeof(WmoHeader) == 64, "WmoHeader must be 64 bytes");

// MOMT — a material (64 bytes).
struct WmoMaterial
{
    uint32_t flags;             // F_UNLIT 0x01, F_UNFOGGED 0x02, F_UNCULLED 0x04, ...
    uint32_t shader;
    uint32_t blendMode;         // EGxBlend
    uint32_t texture1;          // byte offset into MOTX
    uint32_t emissiveColor;     // BGRA
    uint32_t sidnEmissiveColor;
    uint32_t texture2;          // byte offset into MOTX
    uint32_t diffColor;
    uint32_t groundType;
    uint32_t texture3;
    uint32_t color2;
    uint32_t flags2;
    uint32_t runtimeData[4];
};
static_assert(sizeof(WmoMaterial) == 64, "WmoMaterial must be 64 bytes");

// Material flag bits we act on.
enum WmoMaterialFlags : uint32_t
{
    kWmoMatUnlit    = 0x01,
    kWmoMatUnculled = 0x04,   // two-sided
};

// MOGI — group info (32 bytes).
struct WmoGroupInfo
{
    uint32_t flags;
    float    boundingBoxMin[3];
    float    boundingBoxMax[3];
    int32_t  nameOffset;        // into MOGN, -1 if none
};
static_assert(sizeof(WmoGroupInfo) == 32, "WmoGroupInfo must be 32 bytes");

// MODS — a doodad set (32 bytes).
struct WmoDoodadSet
{
    char     name[20];
    uint32_t firstInstanceIndex;
    uint32_t numDoodads;
    uint32_t unused;
};
static_assert(sizeof(WmoDoodadSet) == 32, "WmoDoodadSet must be 32 bytes");

// MODD — a doodad instance (40 bytes).
struct WmoDoodadDef
{
    uint32_t nameOffsetAndFlags;   // low 24 bits: byte offset into MODN; high 8: flags
    float    position[3];
    float    rotation[4];          // quaternion (x, y, z, w)
    float    scale;
    uint32_t color;                // BGRA
};
static_assert(sizeof(WmoDoodadDef) == 40, "WmoDoodadDef must be 40 bytes");

// MOGP — group header (68 bytes); the geometry sub-chunks follow it inside the MOGP chunk.
struct WmoGroupHeader
{
    uint32_t groupNameOffset;
    uint32_t descriptiveNameOffset;
    uint32_t flags;
    float    boundingBoxMin[3];
    float    boundingBoxMax[3];
    uint16_t portalStart;
    uint16_t portalCount;
    uint16_t transBatchCount;
    uint16_t intBatchCount;
    uint16_t extBatchCount;
    uint16_t padding;
    uint8_t  fogIds[4];
    uint32_t groupLiquid;
    uint32_t groupId;
    uint32_t flags2;
    uint32_t unused;
};
static_assert(sizeof(WmoGroupHeader) == 68, "WmoGroupHeader must be 68 bytes");

// Group flag bits we act on.
enum WmoGroupFlags : uint32_t
{
    kWmoGroupHasVertexColor = 0x00000004,   // has MOCV
    kWmoGroupHasLiquid      = 0x00001000,   // has MLIQ
};

// MOBA — a render batch (24 bytes).
struct WmoBatch
{
    int16_t  bbMin[3];
    int16_t  bbMax[3];
    uint32_t startIndex;    // first index into MOVI
    uint16_t count;         // index count
    uint16_t minIndex;
    uint16_t maxIndex;
    uint8_t  flags;
    uint8_t  materialId;    // into MOMT (0xFF = none)
};
static_assert(sizeof(WmoBatch) == 24, "WmoBatch must be 24 bytes");

// MLIQ — liquid header (30 bytes), followed by vertex + tile-flag arrays.
struct WmoLiquidHeader
{
    uint32_t xVerts;
    uint32_t yVerts;
    uint32_t xTiles;
    uint32_t yTiles;
    float    baseCoords[3];
    uint16_t materialId;
};
static_assert(sizeof(WmoLiquidHeader) == 30, "WmoLiquidHeader must be 30 bytes");

// MOLT — an interior light (48 bytes).
struct WmoLight
{
    uint8_t  type;         // 0 omni, 1 spot, 2 direct, 3 ambient
    uint8_t  useAtten;
    uint8_t  pad0, pad1;
    uint32_t color;        // CImVector (BGRA)
    float    position[3];
    float    intensity;
    float    attenStart;
    float    attenEnd;
    float    unk[4];
};
static_assert(sizeof(WmoLight) == 48, "WmoLight must be 48 bytes");

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Decoded, in-memory model (renderer-facing)
// ---------------------------------------------------------------------------

// A merged vertex across all groups (+ baked doodads + liquid). Mirrors the fields the GPU
// mesh path consumes, so WmoUploadBuild is a straight copy into ModelVertexGpu.
struct WmoVertex
{
    float   pos[3];
    float   normal[3];
    float   uv[2];
    uint8_t color[4] = {255, 255, 255, 255};   // baked MOCV (RGBA); white when absent
};

// One draw batch — mirrors ModelSubmeshGpu so upload is trivial.
struct WmoSubmesh
{
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    int      textureIndex = -1;   // into WmoModel::texturePaths
    uint16_t blendMode = 0;       // 0..6 mesh blend mode
    uint16_t materialFlags = 0;   // 0x01 unlit, 0x04 two-sided (matches ModelSubmeshGpu)
    int      priorityPlane = 0;
    float    center[3] = {0, 0, 0};
    bool     isLiquid = false;    // liquid surface -> frame-animate in the viewer
    int      liquidFrameBase = -1;   // first animation-frame texture index (into texturePaths)
    int      liquidFrameCount = 1;   // number of frames
};

// A decoded interior light (MOLT), world-space, for a modest additive vertex-lighting pass.
struct WmoLightInfo
{
    float pos[3];
    float rgb[3];
    float intensity;
    float radius;
};

// A doodad set, surfaced to the viewer's set picker.
struct WmoDoodadSetInfo
{
    std::string name;
    uint32_t    first = 0;
    uint32_t    count = 0;
};

// A placed doodad: a reference to an M2 plus its world transform. The viewer loads the M2
// once per unique path and renders these as live animated instances (RenderScene); a bind
// pose per instance suffices for a static shot.
struct WmoDoodadInstance
{
    std::string m2Path;            // normalized (.mdx -> .m2)
    float       transform[16];     // world placement: translate * rotate(quat) * scale
    float       origin[3];         // world position (transparent sort / framing)
    uint8_t     color[4];          // instance tint (BGRA -> RGBA); white if unset
};

struct WmoModel
{
    std::string name;

    // Merged geometry (shell groups; doodads and liquid appended in later phases).
    std::vector<WmoVertex>   vertices;
    std::vector<uint32_t>    indices;
    std::vector<WmoSubmesh>  submeshes;
    std::vector<std::string> texturePaths;   // unified (WMO materials + doodad + liquid)

    // Doodad metadata (for the set picker) + the resolved instances of the selected set.
    std::vector<WmoDoodadSetInfo>  doodadSets;
    std::vector<WmoDoodadInstance> doodadInstances;
    std::vector<WmoLightInfo>      lights;   // MOLT interior lights (world space)
    int doodadDefCount = 0;

    // Counts (for --wmo-test) and framing.
    int groupCount = 0;
    int materialCount = 0;
    int liquidTileCount = 0;

    glm::vec3 boundsCenter{0.0f};
    float     boundsRadius = 1.0f;

    bool valid() const { return !vertices.empty() && !indices.empty(); }
};
} // namespace we::wmo
