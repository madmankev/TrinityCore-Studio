#version 450

// ADT terrain vertex shader. No skinning: terrain is static. Passes chunk-local UV (0..1,
// for the alpha map), the MCNR normal, and the MCCV vertex color to the fragment shader.
// Reuses the mesh vertex layout (bone attributes at locations 3/4 are present but unused).

layout(binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
} scene;

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 5) in vec4  inColor;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColor;

void main()
{
    gl_Position = scene.proj * scene.view * vec4(inPos, 1.0);
    outUV = inUV;
    outNormal = inNormal;
    outColor = inColor;
}
