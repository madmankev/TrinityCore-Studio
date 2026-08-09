#version 450

// M2 model vertex shader with GPU skinning. Each vertex is influenced by up to 4 bones;
// the bone matrix palette is a per-model storage buffer updated each frame by the
// animator. At rest the palette is all-identity, so this reproduces the bind pose.

const int MAX_WORLD_LIGHTS = 16;
struct WorldLight {
    vec4 positionRange;
    vec4 colorIntensity;
    vec4 directionInnerCos;
    vec4 outerType;
};

layout(binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
    vec4 sunDirectionIntensity;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 fogColor;
    vec4 fogParams;
    WorldLight lights[MAX_WORLD_LIGHTS];
} scene;

layout(std430, binding = 2) readonly buffer Bones {
    mat4 bone[];
} bones;

layout(push_constant) uniform Material {
    mat4 texMatrix;   // animated UV transform
    vec4 color;       // (fragment)
    int  blendMode;  // (fragment)
    int  flags;      // (fragment)
    int  boneBase;   // offset into the shared bone palette (scene instancing)
    int  pad0;       // aligns highlight to the shared C++ MeshPush layout
    vec4 highlight;  // (fragment) additive selection/hover tint
} mat;

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in uvec4 inBoneIndices;
layout(location = 4) in vec4  inBoneWeights;
layout(location = 5) in vec4  inColor;   // baked per-vertex color (WMO MOCV); white for M2

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColor;
layout(location = 3) out vec3 outWorldPos;

void main()
{
    vec3 pos = inPos;
    vec3 nrm = inNormal;

    float wsum = inBoneWeights.x + inBoneWeights.y + inBoneWeights.z + inBoneWeights.w;
    if (wsum > 0.0)
    {
        vec3 p = vec3(0.0);
        vec3 n = vec3(0.0);
        for (int i = 0; i < 4; ++i)
        {
            float w = inBoneWeights[i];
            if (w > 0.0)
            {
                mat4 m = bones.bone[mat.boneBase + int(inBoneIndices[i])];
                p += w * vec3(m * vec4(inPos, 1.0));
                n += w * (mat3(m) * inNormal);
            }
        }
        pos = p;
        nrm = n;
    }

    gl_Position = scene.proj * scene.view * vec4(pos, 1.0);
    outUV = (mat.texMatrix * vec4(inUV, 0.0, 1.0)).xy;   // animated UV scroll/rotate/scale
    outNormal = nrm;
    outColor = inColor;
    outWorldPos = pos;
}
