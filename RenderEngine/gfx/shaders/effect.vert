#version 450

// Effect (particle/ribbon) vertex shader. Geometry is pre-built in model space by the
// CPU simulation (billboard quads / ribbon strips) with a per-vertex color and uv.

layout(binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
} scene;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;

void main()
{
    gl_Position = scene.proj * scene.view * vec4(inPos, 1.0);
    outColor = inColor;
    outUV = inUV;
}
