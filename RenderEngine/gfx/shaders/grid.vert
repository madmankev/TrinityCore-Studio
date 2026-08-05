#version 450

// Ground reference grid: colored world-space line segments, transformed by the scene camera.

layout(binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
} scene;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    gl_Position = scene.proj * scene.view * vec4(inPos, 1.0);
    outColor = inColor;
}
