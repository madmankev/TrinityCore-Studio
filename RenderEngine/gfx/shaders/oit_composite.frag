#version 450

// Weighted-Blended OIT resolve. Reads the two accumulation attachments (written by *_oit.frag in the
// previous subpass) as input attachments and produces the resolved translucent color. The pipeline
// blend (src = 1-srcAlpha, dst = srcAlpha, with srcAlpha carrying revealage) composites it over the
// opaque color target: color = avgColor*(1-reveal) + dst*reveal.

layout(input_attachment_index = 0, binding = 0) uniform subpassInput uAccum;
layout(input_attachment_index = 1, binding = 1) uniform subpassInput uReveal;

layout(location = 0) out vec4 outColor;

void main()
{
    float reveal = subpassLoad(uReveal).r;
    if (reveal > 0.9999)   // no translucent coverage here — leave the opaque color untouched
        discard;

    vec4 accum = subpassLoad(uAccum);
    vec3 avg = accum.rgb / max(accum.a, 1e-5);   // weighted average of the translucent colors
    outColor = vec4(avg, reveal);
}
