#version 450

// Weighted-Blended OIT variant of model.frag, used ONLY for blend mode 2 (alpha-over) — the one
// order-dependent M2 blend mode. Instead of blending straight to the color target, it emits two
// order-independent accumulators (McGuire/Bavoil 2013): a weighted premultiplied-color sum and a
// revealage product. A later fullscreen pass resolves them onto the color target. Same
// texture/lighting/liquid logic as model.frag; the alpha-key (mode 1) branch is gone (never mode 2).

layout(binding = 1) uniform sampler2D tex;

layout(push_constant) uniform Material {
    mat4 texMatrix;  // (vertex)
    vec4 color;      // animated RGBA modulation
    int  blendMode;  // always 2 here
    int  flags;      // bit0 = unlit; bit1 = liquid (opacity from vertex alpha, not texture)
    int  boneBase;   // (vertex) — declared here so `highlight` sits at the same offset in both stages
    vec4 highlight;  // additive selection/hover tint: rgb + strength in .a; all-zero = none
} mat;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4  outAccum;    // (premultiplied color, alpha) * weight
layout(location = 1) out float outReveal;   // fragment alpha; blend multiplies dst by (1 - a)

void main()
{
    vec4 c = texture(tex, inUV) * mat.color * inColor;
    if (mat.color.a * inColor.a < 0.02)
        discard;

    vec3 col = c.rgb;
    if ((mat.flags & 1) == 0)
    {
        float ndl = max(dot(normalize(inNormal), normalize(vec3(0.35, 0.4, 0.85))), 0.0);
        col *= (0.45 + 0.55 * ndl);
    }

    // Additive selection/hover highlight (a subtle glow wash over the lit surface).
    col += mat.highlight.rgb * mat.highlight.a;

    float a = ((mat.flags & 2) != 0) ? (mat.color.a * inColor.a) : c.a;

    // McGuire/Bavoil depth weight: nearer fragments dominate. gl_FragCoord.z is window-space
    // (non-linear) — an approximation that avoids threading eye-space depth from the vertex shader.
    float w = a * clamp(3e3 * pow(1.0 - gl_FragCoord.z, 3.0), 1e-2, 3e3);

    outAccum  = vec4(col * a, a) * w;
    outReveal = a;
}
