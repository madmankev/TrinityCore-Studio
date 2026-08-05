#version 450

// Effect fragment shader: modulate the emitter texture by the per-particle color/alpha.
// The blend equation (additive / alpha / mod / ...) is baked into the pipeline.

layout(binding = 1) uniform sampler2D tex;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(tex, inUV) * inColor;
}
