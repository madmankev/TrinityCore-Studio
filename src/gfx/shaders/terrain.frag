#version 450
#extension GL_EXT_nonuniform_qualifier : require

// ADT terrain fragment shader, batched to ONE draw per tile. Each chunk blends up to 4 tiled ground
// textures using a packed alpha map (R/G/B = layer 1/2/3 weights), sequentially over-compositing (the
// order WoW's old continents use), then modulates by the MCCV vertex color and a fixed sun.
//
// Ground textures live in a bindless, session-global array (deduped across tiles, indexed per chunk);
// alpha maps live in a per-tile 2D-array (one 64x64 layer per chunk). The per-chunk layer indices +
// alpha slice come from a per-tile SSBO indexed by the flat chunk id. Index 0 of the ground array is
// a reserved white texture, so a missing layer (-1 on the CPU, mapped to 0) contributes nothing.
//
// The alpha map is sampled CLAMP + a half-texel inset so per-chunk maps don't bleed across seams.

layout(set = 1, binding = 0) uniform sampler2D groundTex[4096];   // bindless, tiled (REPEAT)
layout(set = 2, binding = 0) uniform sampler2DArray alphaArray;   // per-tile, per-chunk 64x64, CLAMP

struct ChunkParams { ivec4 layer; int alphaSlice; int layerCount; int pad0; int pad1; };
layout(std430, set = 2, binding = 1) readonly buffer Params { ChunkParams p[]; } params;

layout(push_constant) uniform Terrain {
    float tileFactor;   // ground-texture repeats per chunk (UV 0..1 -> 0..tileFactor)
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) flat in uint inChunk;

layout(location = 0) out vec4 outColor;

vec3 ground(int idx, vec2 uv) { return texture(groundTex[nonuniformEXT(idx)], uv).rgb; }

void main()
{
    ChunkParams cp = params.p[inChunk];

    vec2 auv = inUV * 0.984375 + 0.0078125;
    vec3 a = texture(alphaArray, vec3(auv, float(cp.alphaSlice))).rgb;   // layer 1,2,3 weights

    vec2 tuv = inUV * pc.tileFactor;
    vec3 col = ground(cp.layer.x, tuv);                       // base
    col = mix(col, ground(cp.layer.y, tuv), a.r);             // layer 1 over base
    col = mix(col, ground(cp.layer.z, tuv), a.g);             // layer 2 over that
    col = mix(col, ground(cp.layer.w, tuv), a.b);             // layer 3 on top

    col *= inColor.rgb;   // MCCV

    float ndl = max(dot(normalize(inNormal), normalize(vec3(0.35, 0.4, 0.85))), 0.0);
    col *= (0.45 + 0.55 * ndl);

    outColor = vec4(col, 1.0);
}
