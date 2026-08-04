#version 450

// Instanced M2/WMO vertex shader for the streamed world. Same skinning as model.vert, but with a
// SHARED per-model bone palette (boneBase) and a PER-INSTANCE model matrix supplied as instance-rate
// vertex attributes (locations 6-9). So one draw renders N copies of a model — a forest of identical
// trees is one instanced draw instead of N, and the bone SSBO holds one palette per model, not per
// instance. Zero-weight WMO vertices pass through, so instanceMatrix * vertex = the placement.

layout(binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
} scene;

layout(std430, binding = 2) readonly buffer Bones {
    mat4 bone[];
} bones;

layout(push_constant) uniform Material {
    mat4 texMatrix;
    vec4 color;
    int  blendMode;
    int  unlit;
    int  boneBase;   // base of the model's SHARED palette
} mat;

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in uvec4 inBoneIndices;
layout(location = 4) in vec4  inBoneWeights;
layout(location = 5) in vec4  inColor;
layout(location = 6) in vec4  inInst0;   // per-instance model matrix, columns 0..3
layout(location = 7) in vec4  inInst1;
layout(location = 8) in vec4  inInst2;
layout(location = 9) in vec4  inInst3;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColor;

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

    mat4 inst = mat4(inInst0, inInst1, inInst2, inInst3);
    gl_Position = scene.proj * scene.view * inst * vec4(pos, 1.0);
    outUV = (mat.texMatrix * vec4(inUV, 0.0, 1.0)).xy;
    outNormal = mat3(inst) * nrm;
    outColor = inColor;
}
