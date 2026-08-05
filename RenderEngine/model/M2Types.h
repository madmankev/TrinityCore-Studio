#pragma once

// M2Types — the WoW 3.3.5a (build 12340, M2 version 264) model format, on-disk structs
// plus the decoded in-memory model the viewer uses.
//
// On-disk layout is authoritative from TrinityCore's vmap4_extractor (ModelHeader) and
// wowdev.wiki (M2 / M2/.skin). All arrays in the file are M2Array{count, offset} where
// `offset` is relative to the start of the file the array lives in (.m2 or .skin).
// Geometry is split: vertices + bones + animation live in the .m2; the triangle/submesh/
// batch lists live in external Model0{0..3}.skin files (offsets relative to the .skin).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace we::m2
{
// ---------------------------------------------------------------------------
// On-disk structures (tightly packed; read directly from the file bytes)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

struct M2Array
{
    uint32_t count;
    uint32_t offset;
};

// The .m2 header (MD20). Field order/layout matches TrinityCore ModelHeader exactly.
// Note WotLK has `nViews` as a lone count (skins are external — no ofsViews).
struct M2Header
{
    char     id[4];              // "MD20"
    uint8_t  version[4];         // 264 for 3.3.5a
    M2Array  name;
    uint32_t globalFlags;
    M2Array  globalSequences;    // uint32[] durations
    M2Array  animations;         // M2Sequence[]
    M2Array  animationLookup;    // uint16[]
    M2Array  bones;              // M2CompBone[]
    M2Array  keyBoneLookup;      // uint16[]
    M2Array  vertices;           // M2Vertex[]
    uint32_t numViews;           // count of external .skin profiles
    M2Array  colors;
    M2Array  textures;           // M2TextureDef[]
    M2Array  transparency;
    M2Array  textureAnimations;
    M2Array  texReplace;
    M2Array  renderFlags;        // M2Material[]
    M2Array  boneLookupTable;    // uint16[]
    M2Array  texLookup;          // uint16[]  (batch.textureComboIndex -> texture index)
    M2Array  texUnits;
    M2Array  transLookup;
    M2Array  texAnimLookup;
    float    bounds[14];         // bounding box (min/max), radius, collision box, etc.
    M2Array  boundingTriangles;
    M2Array  boundingVertices;
    M2Array  boundingNormals;
    M2Array  attachments;
    M2Array  attachLookup;
    M2Array  events;
    M2Array  lights;
    M2Array  cameras;
    M2Array  cameraLookup;
    M2Array  ribbonEmitters;
    M2Array  particleEmitters;
};

// 48 bytes. Vertices stay in the .m2 even though the index/triangle lists are external.
struct M2Vertex
{
    float   pos[3];
    uint8_t boneWeights[4];
    uint8_t boneIndices[4];
    float   normal[3];
    float   uv[2];
    float   uv2[2];
};

// A texture slot. type==0 => filename is stored in the .m2 at `filename.offset`.
// type>0 => runtime-supplied (e.g. creature skin from CreatureDisplayInfo variations).
struct M2TextureDef
{
    uint32_t type;
    uint32_t flags;
    M2Array  filename;   // char[] (includes NUL)
};

// Material / render flags (a.k.a. M2Material). blendMode: 0 opaque, 1 alpha-key, 2 alpha.
struct M2Material
{
    uint16_t flags;
    uint16_t blendMode;
};

// A per-bone animation channel. In WotLK the timestamps/values are arrays-of-arrays
// (one sub-array per animation); sub-array data is inline in the .m2 or in a .anim file.
struct M2Track
{
    uint16_t interpolationType;   // 0 none, 1 linear, 2 hermite, 3 bezier
    uint16_t globalSequence;      // 0xFFFF if none
    M2Array  timestamps;          // M2Array<M2Array<uint32>>
    M2Array  values;              // M2Array<M2Array<T>>
};

// 88 bytes. Rotation values are packed M2CompQuat (int16[4]).
struct M2CompBone
{
    int32_t  keyBoneId;
    uint32_t flags;
    int16_t  parentBone;   // -1 => root
    uint16_t submeshId;
    uint16_t unknown[2];
    M2Track  translation;  // C3Vector values
    M2Track  rotation;     // M2CompQuat values
    M2Track  scale;        // C3Vector values
    float    pivot[3];
};

// A packed quaternion: each component (int16) maps to [-1, 1]. Order is x,y,z,w.
struct M2CompQuat
{
    int16_t x, y, z, w;
};

// 64 bytes. WotLK (v264) stores a single `duration` (pre-WotLK used start+end).
struct M2Sequence
{
    uint16_t animationId;     // AnimationData.dbc id
    uint16_t subAnimationId;  // variation
    uint32_t duration;        // ms
    float    moveSpeed;
    uint32_t flags;           // bit 0x20 => keyframes inline (else external .anim)
    int16_t  frequency;
    uint16_t padding;
    uint32_t replayMin;
    uint32_t replayMax;
    uint32_t blendTime;
    float    boundsMin[3];
    float    boundsMax[3];
    float    boundsRadius;
    int16_t  nextAnimation;
    uint16_t aliasNext;
};

// --- .skin (external, WotLK) ---
// The external file is prefixed with a 4-byte 'SKIN' magic (detected at load time);
// this struct is the header proper, matching the old embedded ofsViews block. All
// M2Array offsets are absolute from the start of the .skin file.
struct SkinHeader
{
    M2Array indices;     // uint16[]  -> lookup into the .m2 global vertex list
    M2Array triangles;   // uint16[]  -> indices into `indices` (3 per triangle)
    M2Array properties;  // uint8[4][] bone influences per local vertex
    M2Array submeshes;   // M2SkinSection[]
    M2Array batches;     // M2Batch[]
    uint32_t boneCountMax;
};

// 48 bytes (WotLK adds sortCenter/sortRadius vs. BC's 32).
struct M2SkinSection
{
    uint16_t skinSectionId;
    uint16_t level;          // (level<<16) added to vertex/index start for >64k
    uint16_t vertexStart;
    uint16_t vertexCount;
    uint16_t indexStart;
    uint16_t indexCount;
    uint16_t boneCount;
    uint16_t boneComboIndex;
    uint16_t boneInfluences;
    uint16_t centerBoneIndex;
    float    centerPosition[3];
    float    sortCenterPosition[3];
    float    sortRadius;
};

// 40 bytes on-disk. An attachment point: where items (weapon/helm/shoulders) hang on the model.
struct M2AttachmentDef
{
    uint32_t id;               // attachment type (1 HandRight, 2 HandLeft, 5/6 shoulders, 11 helm, ...)
    uint16_t bone;             // bone index this attachment rides
    uint16_t unknown;
    float    position[3];      // offset relative to the bone
    M2Track  animateAttached;  // ignored (bool track)
};

// Decoded attachment (renderer-facing): an item hung here is placed at bones[bone] * translate(pos).
struct M2Attachment
{
    uint32_t id = 0;
    uint16_t bone = 0;
    float    pos[3] = {0, 0, 0};
};
static_assert(sizeof(M2AttachmentDef) == 40, "M2AttachmentDef must be 40 bytes");

// 24 bytes. A texture unit: ties a submesh to a texture + material.
struct M2Batch
{
    uint8_t  flags;
    int8_t   priorityPlane;
    uint16_t shaderId;
    uint16_t skinSectionIndex;         // -> submeshes[]
    uint16_t geosetIndex;
    uint16_t colorIndex;               // 0xFFFF if none
    uint16_t materialIndex;            // -> renderFlags[] (M2Material)
    uint16_t materialLayer;
    uint16_t textureCount;
    uint16_t textureComboIndex;        // -> texLookup[]
    uint16_t textureCoordComboIndex;
    uint16_t textureWeightComboIndex;
    uint16_t textureTransformComboIndex;
};

// --- effects (particles / ribbons / animated texture + color) ---

// Fixed-timeline track used inside particle emitters (keyed by particle age, not by the
// model's animation). timestamps are uint16, keys are the value type.
struct M2FBlock
{
    M2Array timestamps;   // uint16[]
    M2Array keys;         // T[]
};

// 476 bytes (WotLK). Emission params are M2Tracks (animated by the current sequence);
// color/alpha/scale/cell tracks are FBlocks (keyed by each particle's age).
struct M2ParticleEmitter
{
    uint32_t particleId;
    uint32_t flags;
    float    position[3];
    uint16_t bone;
    uint16_t texture;
    M2Array  geometryModelFilename;
    M2Array  recursionModelFilename;
    uint8_t  blendingType;
    uint8_t  emitterType;              // 1 plane, 2 sphere, 3 spline
    uint16_t particleColorIndex;
    uint8_t  particleType;
    uint8_t  headOrTail;
    int16_t  textureTileRotation;
    uint16_t textureDimensionRows;
    uint16_t textureDimensionColumns;
    M2Track  emissionSpeed;
    M2Track  speedVariation;
    M2Track  verticalRange;
    M2Track  horizontalRange;
    M2Track  gravity;
    M2Track  lifespan;
    float    lifespanVary;
    M2Track  emissionRate;
    float    emissionRateVary;
    M2Track  emissionAreaLength;
    M2Track  emissionAreaWidth;
    M2Track  zSource;
    M2FBlock colorTrack;              // vec3 keys
    M2FBlock alphaTrack;             // fixed16 keys
    M2FBlock scaleTrack;             // vec2 keys
    float    scaleVary[2];
    M2FBlock headCellTrack;          // uint16 keys
    M2FBlock tailCellTrack;          // uint16 keys
    float    tailLength;
    float    twinkleSpeed;
    float    twinklePercent;
    float    twinkleScaleMin;
    float    twinkleScaleMax;
    float    burstMultiplier;
    float    drag;
    float    baseSpin;
    float    baseSpinVary;
    float    spin;
    float    spinVary;
    float    tumbleMin[3];
    float    tumbleMax[3];
    float    windVector[3];
    float    windTime;
    float    followSpeed1;
    float    followScale1;
    float    followSpeed2;
    float    followScale2;
    M2Array  splinePoints;           // vec3[]
    M2Track  enabledIn;              // uint8 keys
};

// 176 bytes (WotLK). A trail emitted along a bone's path.
struct M2RibbonEmitter
{
    int32_t  ribbonId;
    uint32_t boneIndex;
    float    position[3];
    M2Array  textureIndices;   // uint16[]
    M2Array  materialIndices;  // uint16[]
    M2Track  colorTrack;       // vec3
    M2Track  alphaTrack;       // fixed16
    M2Track  heightAboveTrack; // float
    M2Track  heightBelowTrack; // float
    float    edgesPerSecond;
    float    edgeLifetime;
    float    gravity;
    uint16_t textureRows;
    uint16_t textureCols;
    M2Track  texSlotTrack;     // uint16
    M2Track  visibilityTrack;  // uint8
    int16_t  priorityPlane;
    uint16_t padding;
};

// 60 bytes. Animated UV transform referenced by a batch (scroll/rotate/scale textures).
struct M2TextureTransform
{
    M2Track translation;   // vec3
    M2Track rotation;      // quat
    M2Track scaling;       // vec3
};

// 40 bytes. Animated vertex color + alpha for a batch.
struct M2Color
{
    M2Track color;   // vec3
    M2Track alpha;   // fixed16
};

#pragma pack(pop)

// On-disk sizes are load-bearing (we memcpy directly from file bytes). Assert the
// packed layouts against the documented 3.3.5a sizes so a packing regression fails to
// compile rather than silently misreading models.
static_assert(sizeof(M2Array) == 8, "M2Array must be 8 bytes");
static_assert(sizeof(M2Vertex) == 48, "M2Vertex must be 48 bytes");
static_assert(sizeof(M2TextureDef) == 16, "M2TextureDef must be 16 bytes");
static_assert(sizeof(M2Material) == 4, "M2Material must be 4 bytes");
static_assert(sizeof(M2Track) == 20, "M2Track must be 20 bytes");
static_assert(sizeof(M2CompBone) == 88, "M2CompBone must be 88 bytes");
static_assert(sizeof(M2CompQuat) == 8, "M2CompQuat must be 8 bytes");
static_assert(sizeof(M2Sequence) == 64, "M2Sequence (WotLK, single duration) must be 64 bytes");
static_assert(sizeof(SkinHeader) == 44, "SkinHeader (magic-less) must be 44 bytes");
static_assert(sizeof(M2SkinSection) == 48, "M2SkinSection must be 48 bytes");
static_assert(sizeof(M2Batch) == 24, "M2Batch must be 24 bytes");
static_assert(sizeof(M2Header) == 304, "M2Header must be 304 bytes");
static_assert(sizeof(M2FBlock) == 16, "M2FBlock must be 16 bytes");
static_assert(sizeof(M2ParticleEmitter) == 476, "M2ParticleEmitter (WotLK) must be 476 bytes");
static_assert(sizeof(M2RibbonEmitter) == 176, "M2RibbonEmitter (WotLK) must be 176 bytes");
static_assert(sizeof(M2TextureTransform) == 60, "M2TextureTransform must be 60 bytes");
static_assert(sizeof(M2Color) == 40, "M2Color must be 40 bytes");

// M2ParticleEmitter::flags bits (WotLK-relevant subset; Cata+ multitexture/compressed-gravity
// bits deliberately omitted — this viewer targets build 12340 / M2 v264).
enum M2ParticleFlags : uint32_t
{
    kParticleVelocityOrient   = 0x4,     // head/tail billboard aligns along the velocity vector
    kParticleWorldSpace       = 0x10,    // simulate in world space (else bone-local, the default)
    kParticleInheritBoneScale = 0x20,    // multiply particle size by the emitter bone's scale
    kParticleImplosion        = 0x80,    // kill particles moving away from the emitter center
    kParticleNegateSpinRandom = 0x200,   // 50% chance to spin the opposite direction
    kParticleFollow           = 0x4000,  // pull particle toward emitter once age > 2*dt
    kParticleSquirt           = 0x8000,  // only emit while emissionRate > 0 (burst on the edge)
};

// M2CompBone::flags billboard bits (mutually exclusive spherical vs. cylindrical).
enum M2BoneFlags : uint32_t
{
    kBoneSphericalBillboard   = 0x8,
    kBoneCylindricalBillboardX = 0x10,
    kBoneCylindricalBillboardY = 0x20,
    kBoneCylindricalBillboardZ = 0x40,
    kBoneTransformed          = 0x200,
    kBoneBillboardMask        = 0x78,   // any billboard bit
};

// ---------------------------------------------------------------------------
// Decoded, in-memory model (what the renderer/animator consume)
// ---------------------------------------------------------------------------

// One draw call: a contiguous range of the model's index buffer with a texture + material.
struct RenderBatch
{
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    int      textureIndex = -1;   // into M2Model::texturePaths (-1 => untextured/runtime)
    uint16_t blendMode = 0;       // M2Material::blendMode
    uint16_t materialFlags = 0;   // M2Material::flags (two-sided, unlit, ...)
    uint16_t shaderId = 0;        // M2Batch::shaderId (env-map / combiner selection)
    uint16_t materialLayer = 0;   // M2Batch::materialLayer (draw order within a submesh)
    uint16_t textureCount = 1;    // M2Batch::textureCount (2 => env-mapped / multi-tex unit)
    uint16_t submeshId = 0;
    int      priorityPlane = 0;   // M2Batch::priorityPlane (transparent draw ordering)
    float    center[3] = {0, 0, 0};   // submesh sort center (for back-to-front depth sort)

    // Animation lookups (for mesh texture/color animation). -1 => none/static.
    int      colorIndex = -1;             // -> M2Model::colors
    int      textureTransformIndex = -1;  // resolved via texAnimLookup
    int      textureWeightIndex = -1;     // resolved via transLookup -> M2Model::transparencies
};

// A geometry-model particle mesh (emitter.geometryModelFilename), resolved at load time
// into a static CPU mesh. M2EffectSystem bakes one transformed instance per live particle
// into the effect geometry; its textures are appended to the main model's texture list
// (BuildUpload) starting at textureBase, so a submesh's final texture index is
// textureBase + localTexture.
struct GeoParticleModel
{
    struct Vert { float pos[3]; float uv[2]; };
    struct Sub  { uint32_t indexStart = 0, indexCount = 0; int localTexture = -1; uint16_t blendMode = 0; };
    struct Tex  { std::vector<uint8_t> rgba; int w = 0, h = 0; };
    std::vector<Vert>     verts;
    std::vector<uint32_t> indices;
    std::vector<Sub>      subs;
    std::vector<Tex>      textures;
    int textureBase = 0;
};

struct M2Model;   // forward: a recursion child is a full model owned by its parent

// A recursion (child) particle model (emitter.recursionModelFilename). Each live particle of
// the parent emitter emits this child model's emitters as a trail. The child's textures are
// decoded and appended to the parent model's GPU texture list at `textureBase` (like
// GeoParticleModel), so a child draw's final texture index is textureBase + child.texture.
struct RecursionModel
{
    std::unique_ptr<M2Model>          model;        // full child model (emitters, bytes, tracks)
    std::vector<GeoParticleModel::Tex> textures;    // decoded child textures (parallel to child paths)
    int textureBase = 0;
};

struct M2Model
{
    std::string name;

    // Geometry. `indices` are already resolved to global M2 vertex indices (the .skin's
    // triangles mapped through its vertex-lookup), so a batch draws indices[start..start+count).
    std::vector<M2Vertex>    vertices;
    std::vector<uint32_t>    indices;
    std::vector<RenderBatch> batches;

    // Texture slots. Non-empty path => type==0 texture stored in the client (a .blp).
    // Empty path with a non-zero type => runtime skin (filled from a display id later).
    std::vector<std::string> texturePaths;
    std::vector<uint32_t>    textureTypes;

    // Skeleton + animation (raw; decoded by M2Animator in the animation phase). Kept as
    // the raw file bytes plus parsed bone/sequence arrays so tracks can be read lazily.
    std::vector<uint8_t>     m2Bytes;      // the whole .m2, for lazy track reads
    std::vector<M2CompBone>  bones;
    std::vector<int16_t>     boneLookup;   // boneLookupTable (batch/vertex bone remap)
    std::vector<M2Sequence>  sequences;
    std::vector<uint32_t>    globalSequenceDurations;
    std::vector<M2Attachment> attachments;  // where items hang (weapon/helm/shoulders)

    // Effects (raw structs; their M2Track/FBlock sub-arrays are read lazily from m2Bytes).
    std::vector<M2ParticleEmitter>  particleEmitters;
    std::vector<M2RibbonEmitter>    ribbonEmitters;
    std::vector<M2TextureTransform> textureTransforms;
    std::vector<M2Color>            colors;
    std::vector<M2Material>         materials;        // renderFlags[]; ribbon materialIndices resolve here
    std::vector<M2Track>            transparencies;   // each an M2Track<fixed16>
    std::vector<uint16_t>           texAnimLookup;    // batch.textureTransformComboIndex -> textureTransforms
    std::vector<uint16_t>           transLookup;      // batch.textureWeightComboIndex -> transparencies

    // Geometry-model particles: referenced meshes resolved at load. `particleGeoModel` is
    // parallel to particleEmitters (index into geoParticleModels, or -1 for none).
    std::vector<GeoParticleModel> geoParticleModels;
    std::vector<int>              particleGeoModel;

    // Recursion (child) particle models: `particleRecursionModel` is parallel to
    // particleEmitters (index into recursionModels, or -1 for none).
    std::vector<RecursionModel>   recursionModels;
    std::vector<int>              particleRecursionModel;

    // Bounding sphere (for framing the camera).
    glm::vec3 boundsCenter{0.0f};
    float     boundsRadius = 1.0f;

    bool valid() const { return !vertices.empty() && !indices.empty(); }
};
} // namespace we::m2
