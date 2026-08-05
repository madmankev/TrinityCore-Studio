#pragma once

// AdtTypes — WoW 3.3.5a (WotLK, build 12340) ADT map-tile format: on-disk chunk structs
// (tightly packed; read straight from file bytes, so the sizes are load-bearing and
// static_assert'd) plus the decoded in-memory AdtTile the viewer renders.
//
// An ADT is one file per 533.33-yard map tile (World/Maps/<Dir>/<Dir>_<X>_<Y>.adt), chunked
// like a WMO: top-level chunks (MVER/MHDR/MCIN/MTEX/MMDX/MMID/MWMO/MWID/MDDF/MODF/MH2O)
// followed by 256 MCNK map-chunks. Each MCNK has a 128-byte header and its own nested
// sub-chunks (MCVT heights, MCNR normals, MCLY texture layers, MCAL alpha maps, MCCV vertex
// color, MCLQ legacy liquid). See docs/ or wowdev.wiki ADT/v18 for the format.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace we::adt
{
// Tile geometry constants (yards).
constexpr float kTileSize  = 533.33333f;          // one ADT tile
constexpr float kChunkSize = kTileSize / 16.0f;   // one MCNK  (33.3333)
constexpr float kUnitSize  = kChunkSize / 8.0f;   // outer-vertex spacing (4.16666)
constexpr float kMapOrigin = 32.0f * kTileSize;   // 17066.66 — world coord of tile (0,0)'s NW

// ---------------------------------------------------------------------------
// On-disk structures (tightly packed; read directly from the file bytes)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

// MHDR — root header: offsets (relative to &MHDR.data) of the other root chunks (64 bytes).
struct AdtHeader
{
    uint32_t flags;
    uint32_t mcin;
    uint32_t mtex;
    uint32_t mmdx;
    uint32_t mmid;
    uint32_t mwmo;
    uint32_t mwid;
    uint32_t mddf;
    uint32_t modf;
    uint32_t mfbo;
    uint32_t mh2o;
    uint32_t mtxf;
    uint32_t unused[4];
};
static_assert(sizeof(AdtHeader) == 64, "AdtHeader must be 64 bytes");

// MCIN — 256 entries (16 bytes each) indexing the MCNK chunks; `offset` is an ABSOLUTE file offset
// to each MCNK's magic. The loader ignores MCIN (it walks MCNKs by iteration), but the client
// requires it correct, so AdtWriter rebuilds it on insert. size/flags/asyncId are preserved.
struct AdtMcinEntry
{
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
    uint32_t asyncId;
};
static_assert(sizeof(AdtMcinEntry) == 16, "AdtMcinEntry must be 16 bytes");

// MCNK — map-chunk header (128 bytes). The sub-chunks (MCVT/MCNR/MCLY/...) follow it.
// ofs* fields are relative to the MCNK chunk's *magic*, not its data. WotLK (< 5.3) layout:
// ofsHeight/ofsNormal live at 0x14/0x18 (no high_res_holes).
struct AdtMcnkHeader
{
    uint32_t flags;             // 0x00: bit2 lq_river, bit3 lq_ocean, bit4 lq_magma, bit5 lq_slime, bit6 has_mccv
    uint32_t indexX;            // 0x04
    uint32_t indexY;            // 0x08
    uint32_t nLayers;           // 0x0C  (<=4)
    uint32_t nDoodadRefs;       // 0x10
    uint32_t ofsHeight;         // 0x14  MCVT
    uint32_t ofsNormal;         // 0x18  MCNR
    uint32_t ofsLayer;          // 0x1C  MCLY
    uint32_t ofsRefs;           // 0x20  MCRF
    uint32_t ofsAlpha;          // 0x24  MCAL
    uint32_t sizeAlpha;         // 0x28
    uint32_t ofsShadow;         // 0x2C  MCSH
    uint32_t sizeShadow;        // 0x30
    uint32_t areaid;            // 0x34
    uint32_t nMapObjRefs;       // 0x38
    uint16_t holesLowRes;       // 0x3C
    uint16_t unknownButUsed;    // 0x3E
    uint8_t  predTex[16];       // 0x40  uint2[8][8]
    uint8_t  noEffectDoodad[8]; // 0x50  uint1[8][8]
    uint32_t ofsSndEmitters;    // 0x58
    uint32_t nSndEmitters;      // 0x5C
    uint32_t ofsLiquid;         // 0x60  MCLQ
    uint32_t sizeLiquid;        // 0x64  (8 when unused; only read if >8)
    float    position[3];       // 0x68  world position of the chunk's NW corner
    uint32_t ofsMCCV;           // 0x74
    uint32_t ofsMCLV;           // 0x78
    uint32_t unused;            // 0x7C
};
static_assert(sizeof(AdtMcnkHeader) == 128, "AdtMcnkHeader must be 128 bytes");

// MCLY — one texture layer (16 bytes).
struct AdtLayer
{
    uint32_t textureId;         // index into MTEX
    uint32_t flags;             // bit8 use_alpha_map(0x100), bit9 alpha_compressed(0x200), overbright 0x40...
    uint32_t offsetInMCAL;      // byte offset into this chunk's MCAL
    int32_t  effectId;
};
static_assert(sizeof(AdtLayer) == 16, "AdtLayer must be 16 bytes");

// MDDF — an M2 doodad placement (36 bytes).
struct AdtDoodadDef
{
    uint32_t nameId;            // index into MMID (-> MMDX offset)
    uint32_t uniqueId;
    float    position[3];       // placement coords (see AdtLoader for the world remap)
    float    rotation[3];       // Euler degrees
    uint16_t scale;             // 1024 == 1.0
    uint16_t flags;
};
static_assert(sizeof(AdtDoodadDef) == 36, "AdtDoodadDef must be 36 bytes");

// MODF — a WMO placement (64 bytes).
struct AdtMapObjDef
{
    uint32_t nameId;            // index into MWID (-> MWMO offset)
    uint32_t uniqueId;
    float    position[3];
    float    rotation[3];       // Euler degrees
    float    extentsMin[3];
    float    extentsMax[3];
    uint16_t flags;
    uint16_t doodadSet;
    uint16_t nameSet;
    uint16_t scale;             // 1024 == 1.0 (WotLK: usually 1024)
};
static_assert(sizeof(AdtMapObjDef) == 64, "AdtMapObjDef must be 64 bytes");

// MH2O — per-chunk liquid header (12 bytes); one entry per MCNK (256), then instances + data.
struct AdtMh2oHeader
{
    uint32_t offsetInstances;   // -> AdtMh2oInstance[layerCount] (relative to MH2O data start)
    uint32_t layerCount;        // 0 if no liquid here
    uint32_t offsetAttributes;
};
static_assert(sizeof(AdtMh2oHeader) == 12, "AdtMh2oHeader must be 12 bytes");

// MH2O — one liquid layer instance (24 bytes).
struct AdtMh2oInstance
{
    uint16_t liquidType;        // LiquidType.dbc id (direct)
    uint16_t lvf;               // LiquidVertexFormat: 0 height+depth, 1 height+uv, 2 depth-only, 3 all
    float    minHeight;
    float    maxHeight;
    uint8_t  xOffset, yOffset;  // 0..7 within the chunk's 8x8 tile grid
    uint8_t  width, height;     // 1..8
    uint32_t offsetExistsBitmap;
    uint32_t offsetVertexData;
};
static_assert(sizeof(AdtMh2oInstance) == 24, "AdtMh2oInstance must be 24 bytes");

// MCLY layer flags we act on.
enum AdtLayerFlags : uint32_t
{
    kAdtLayerUseAlphaMap    = 0x100,
    kAdtLayerAlphaCompressed = 0x200,
};

// MCNK header flag bits we act on.
enum AdtMcnkFlags : uint32_t
{
    kAdtMcnkLqRiver = 0x004,
    kAdtMcnkLqOcean = 0x008,
    kAdtMcnkLqMagma = 0x010,
    kAdtMcnkLqSlime = 0x020,
    kAdtMcnkHasMccv = 0x040,
    kAdtMcnkHighResHoles = 0x10000,
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Decoded, in-memory tile (renderer-facing)
// ---------------------------------------------------------------------------

// A terrain vertex. Mirrors ModelVertexGpu's leading fields so upload is a straight copy.
struct AdtVertex
{
    float   pos[3];
    float   normal[3];
    float   uv[2];                              // chunk-local 0..1 (alpha map); tiling derived in shader
    uint8_t color[4] = {255, 255, 255, 255};    // baked MCCV (RGBA); neutral white when absent
    uint8_t chunkId = 0;                         // compacted MCNK submesh index; selects per-chunk params
};

// One draw batch. Phase 1 is one submesh per chunk (whole terrain). From Phase 2 each chunk
// carries its layer texture indices + alpha-map index for the terrain pipeline.
struct AdtSubmesh
{
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    int      layerTex[4] = {-1, -1, -1, -1};   // into AdtTile::texturePaths (Phase 2)
    int      layerCount = 0;
    int      alphaMap = -1;                     // into AdtTile::alphaMaps (Phase 2)
    // Liquid (Phase 4): rendered via the mesh path, animated by frame.
    bool     isLiquid = false;
    int      liquidFrameBase = -1;
    int      liquidFrameCount = 1;
    uint16_t blendMode = 0;
    uint16_t materialFlags = 0;
    float    center[3] = {0, 0, 0};
};

// A placed doodad / building: an M2 or WMO reference plus its world transform (baked 4x4).
struct AdtPlacement
{
    std::string path;           // normalized .m2 or .wmo (into the tile frame)
    bool        isWmo = false;
    int         doodadSet = -1; // WMO doodad set (MODF)
    uint32_t    uniqueId = 0;   // MDDF/MODF uniqueId — a spanning object repeats across tiles with
                                // the same id; the streamer loads/renders it once, keyed by this.
    float       transform[16];  // world placement (already offset into the tile frame)
    float       origin[3];      // world position (framing / transparent sort)
};

// A 64x64 packed alpha map for one chunk (R=layer1, G=layer2, B=layer3 weights).
struct AdtAlphaMap
{
    std::vector<uint8_t> rgba;  // 64*64*4
};

struct AdtTile
{
    std::string name;
    int tileX = 0, tileY = 0;

    // Terrain geometry.
    std::vector<AdtVertex>  vertices;
    std::vector<uint32_t>   indices;
    std::vector<AdtSubmesh> submeshes;

    // Textures (Phase 2: unique MTEX ground textures + liquid frames) and per-chunk alpha maps.
    std::vector<std::string> texturePaths;
    std::vector<AdtAlphaMap> alphaMaps;

    // Placements (Phase 3) and everything is expressed in the tile-local frame: every world
    // coordinate has had `worldOffset` subtracted so the tile sits near the origin.
    std::vector<AdtPlacement> placements;
    glm::vec3 worldOffset{0.0f};

    // Liquid (MH2O / MCLQ) — a separate mesh (own vertices/indices/submeshes/textures) rendered
    // through the mesh pipeline (alpha-blended, frame-animated), not the terrain pipeline.
    std::vector<AdtVertex>   liquidVertices;
    std::vector<uint32_t>    liquidIndices;
    std::vector<AdtSubmesh>  liquidSubmeshes;
    std::vector<std::string> liquidTexturePaths;

    // Counts (for --adt-test).
    int chunkCount = 0;
    int renderedChunks = 0;
    int textureCount = 0;
    int doodadDefCount = 0;
    int wmoDefCount = 0;
    int liquidTileCount = 0;
    bool hasMh2o = false;
    int mclqChunks = 0;   // MCNKs carrying legacy MCLQ liquid data (sizeLiquid > 8)
    int chunksNoLayer = 0;   // MCNKs with no MCLY texture layers (would render untextured)

    glm::vec3 boundsCenter{0.0f};
    float     boundsRadius = 1.0f;

    bool valid() const { return !vertices.empty() && !indices.empty(); }
};

// Decoded WDT (World\Maps\<dir>\<dir>.wdt): whether the map is a single global WMO (dungeons/raids)
// or terrain, plus which tiles exist. Drives the streamer's map open.
struct AdtWorldInfo
{
    bool wmoOnly = false;                      // MPHD flag 0x0001 (wdt_uses_global_map_obj)
    uint32_t mphdFlags = 0;
    std::string globalWmo;                     // MWMO path (wmo-only)
    AdtMapObjDef globalPlacement{};            // MODF (wmo-only), 1 entry
    bool hasGlobalPlacement = false;
    std::vector<std::pair<int, int>> tiles;    // existing (x,y) from MAIN (terrain maps)
};
} // namespace we::adt
