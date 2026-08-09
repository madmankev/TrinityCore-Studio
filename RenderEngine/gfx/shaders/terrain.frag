#version 450
#extension GL_EXT_nonuniform_qualifier : require

// ADT terrain fragment shader, batched to ONE draw per tile. Each chunk blends up to 4 tiled ground
// textures using a packed alpha map (R/G/B = layer 1/2/3 weights), sequentially over-compositing (the
// order WoW's old continents use), then modulates by the MCCV vertex color. World Editor lighting
// adds a game-style directional/ambient baseline plus authored point and spot lights.
//
// Ground textures live in a bindless, session-global array (deduped across tiles, indexed per chunk);
// alpha maps live in a per-tile 2D-array (one 64x64 layer per chunk). The per-chunk layer indices +
// alpha slice come from a per-tile SSBO indexed by the flat chunk id. Index 0 of the ground array is
// a reserved white texture, so a missing layer (-1 on the CPU, mapped to 0) contributes nothing.
//
// The alpha map is sampled CLAMP + a half-texel inset so per-chunk maps don't bleed across seams.

const int MAX_WORLD_LIGHTS = 16;
struct WorldLight {
    vec4 positionRange;
    vec4 colorIntensity;
    vec4 directionInnerCos;
    vec4 outerType;
};

layout(set = 0, binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
    vec4 sunDirectionIntensity;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 fogColor;
    vec4 fogParams;
    WorldLight lights[MAX_WORLD_LIGHTS];
} scene;

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
layout(location = 4) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

vec3 ground(int idx, vec2 uv) { return texture(groundTex[nonuniformEXT(idx)], uv).rgb; }

vec3 worldLighting(vec3 worldPos, vec3 normal)
{
    vec3 n = normalize(normal);
    vec3 result = scene.ambientColor.rgb * max(scene.ambientColor.a, 0.0);

    float sunLength = length(scene.sunDirectionIntensity.xyz);
    if (sunLength > 1e-5 && scene.sunDirectionIntensity.a > 0.0)
    {
        vec3 sunDir = scene.sunDirectionIntensity.xyz / sunLength;
        result += scene.sunColor.rgb * scene.sunDirectionIntensity.a * max(dot(n, sunDir), 0.0);
    }

    int count = clamp(int(scene.fogParams.w + 0.5), 0, MAX_WORLD_LIGHTS);
    for (int i = 0; i < count; ++i)
    {
        WorldLight light = scene.lights[i];
        float range = light.positionRange.w;
        float intensity = light.colorIntensity.w;
        if (range <= 0.001 || intensity <= 0.0)
            continue;
        vec3 toLight = light.positionRange.xyz - worldPos;
        float distanceToLight = length(toLight);
        if (distanceToLight >= range || distanceToLight <= 1e-5)
            continue;
        vec3 lightDir = toLight / distanceToLight;
        float attenuation = pow(clamp(1.0 - distanceToLight / range, 0.0, 1.0),
                                max(light.outerType.z, 0.05));
        float cone = 1.0;
        if (light.outerType.y > 0.5)
        {
            float directionLength = length(light.directionInnerCos.xyz);
            if (directionLength <= 1e-5)
                continue;
            float cosAngle = dot(light.directionInnerCos.xyz / directionLength, -lightDir);
            float outerCos = light.outerType.x;
            float innerCos = max(light.directionInnerCos.w, outerCos + 0.0001);
            cone = smoothstep(outerCos, innerCos, cosAngle);
        }
        float diffuse = 0.15 + 0.85 * max(dot(n, lightDir), 0.0);
        result += light.colorIntensity.rgb * intensity * attenuation * cone * diffuse;
    }
    return result;
}

vec3 applyFog(vec3 color, vec3 worldPos)
{
    if (scene.fogParams.z <= 0.5)
        return color;
    float begin = max(scene.fogParams.x, 0.0);
    float end = max(scene.fogParams.y, begin + 0.01);
    float viewDistance = length((scene.view * vec4(worldPos, 1.0)).xyz);
    return mix(color, scene.fogColor.rgb, smoothstep(begin, end, viewDistance));
}

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
    col *= worldLighting(inWorldPos, inNormal);
    col = applyFog(col, inWorldPos);

    outColor = vec4(col, 1.0);
}
