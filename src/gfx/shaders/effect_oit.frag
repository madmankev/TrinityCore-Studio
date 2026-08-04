#version 450

// Weighted-Blended OIT variant of effect.frag for mode-2 (alpha-over) particles/ribbons. Emits the
// same accum/revealage accumulators as model_oit.frag so alpha-blended effects are order-independent.

layout(binding = 1) uniform sampler2D tex;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec4  outAccum;
layout(location = 1) out float outReveal;

void main()
{
    vec4 c = texture(tex, inUV) * inColor;
    float a = c.a;
    float w = a * clamp(3e3 * pow(1.0 - gl_FragCoord.z, 3.0), 1e-2, 3e3);
    outAccum  = vec4(c.rgb * a, a) * w;
    outReveal = a;
}
