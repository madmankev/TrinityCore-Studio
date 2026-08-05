#version 450

// Selection-outline vertex shader (inverted-hull outline). Skins the vertex exactly like model.vert
// (the bone palette folds the world transform for scene instances), then pushes it OUT along its
// normal in VIEW space, scaled by view depth so the outline keeps a roughly constant screen-space
// thickness. The outline pipeline culls FRONT faces + depth-tests against the fully-drawn scene, so
// only the rim that pokes past the real model (and isn't occluded by nearer geometry) is shaded.

layout(binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
} scene;

layout(std430, binding = 2) readonly buffer Bones {
    mat4 bone[];
} bones;

layout(push_constant) uniform Material {
    mat4 texMatrix;   // unused here (kept for a shared push-constant layout)
    vec4 color;       // unused
    int  blendMode;   // unused
    int  flags;       // unused
    int  boneBase;    // offset into the shared bone palette
    vec4 highlight;   // rgb = outline color (fragment); .a = outline width (view-space fraction)
} mat;

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in uvec4 inBoneIndices;
layout(location = 4) in vec4  inBoneWeights;
layout(location = 5) in vec4  inColor;

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

    vec4 viewPos = scene.view * vec4(pos, 1.0);
    vec3 nView = mat3(scene.view) * nrm;
    float nl = length(nView);
    nView = (nl > 1e-5) ? nView / nl : vec3(0.0);

    // View -z is forward; scale the offset by depth for ~constant screen-space thickness.
    float depth = max(-viewPos.z, 0.001);
    viewPos.xyz += nView * (mat.highlight.a * depth);

    gl_Position = scene.proj * viewPos;
}
