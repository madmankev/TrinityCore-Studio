#version 450

// ADT terrain vertex shader. No skinning: terrain is static. Passes chunk-local UV (0..1, for the
// alpha map), the MCNR normal, the MCCV vertex color, and the per-vertex CHUNK ID (packed into the
// unused bone-index slot) so the fragment shader can look up that chunk's layer/alpha parameters.

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

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in uvec4 inBone;   // .x = chunk id (0..255); other bone attrs unused
layout(location = 5) in vec4  inColor;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColor;
layout(location = 3) flat out uint outChunk;
layout(location = 4) out vec3 outWorldPos;

void main()
{
    gl_Position = scene.proj * scene.view * vec4(inPos, 1.0);
    outUV = inUV;
    outNormal = inNormal;
    outColor = inColor;
    outChunk = inBone.x;
    outWorldPos = inPos;
}
