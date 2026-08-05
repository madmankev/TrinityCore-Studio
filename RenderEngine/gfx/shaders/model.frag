#version 450

// M2 model fragment shader. The blend equation itself is baked into the pipeline (one
// per M2 blend mode); this shader handles the two per-material bits that can't be: the
// alpha-key discard (blend mode 1) and unlit materials (flag 0x01, e.g. additive glows).

layout(binding = 1) uniform sampler2D tex;

layout(push_constant) uniform Material {
    mat4 texMatrix;  // (vertex)
    vec4 color;      // animated RGBA modulation
    int  blendMode;  // M2Material.blending_mode (0..6)
    int  flags;      // bit0 = unlit; bit1 = liquid (opacity from vertex alpha, not texture)
    int  boneBase;   // (vertex) — declared here so `highlight` sits at the same offset in both stages
    vec4 highlight;  // additive selection/hover tint: rgb + strength in .a; all-zero = none
} mat;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;   // baked per-vertex color (WMO MOCV); white for M2

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 c = texture(tex, inUV) * mat.color * inColor;   // texture x material x vertex color

    // Animated transparency: a material/vertex alpha driven to ~0 hides the surface even in
    // opaque/alpha-key modes (e.g. M2 particle-helper meshes faded out by a transparency
    // track). Keyed on material x vertex alpha only, so opaque textures with junk alpha
    // aren't holed.
    if (mat.color.a * inColor.a < 0.02)
        discard;

    // Alpha key (mode 1): hard cutout, no blending.
    if (mat.blendMode == 1 && c.a < 0.5)
        discard;

    vec3 col = c.rgb;
    if ((mat.flags & 1) == 0)
    {
        float ndl = max(dot(normalize(inNormal), normalize(vec3(0.35, 0.4, 0.85))), 0.0);
        col *= (0.45 + 0.55 * ndl);
    }

    // Additive selection/hover highlight (a subtle glow wash over the lit surface).
    col += mat.highlight.rgb * mat.highlight.a;

    // Liquid surfaces take opacity from the material x vertex alpha; the texture's own alpha
    // is a ripple mask, not a surface opacity, so ignore it.
    float ca = ((mat.flags & 2) != 0) ? (mat.color.a * inColor.a) : c.a;

    // Opaque / alpha-key modes ignore the framebuffer alpha; force 1 so nothing leaks
    // through if the target is ever read as RGBA.
    float a = (mat.blendMode <= 1) ? 1.0 : ca;
    outColor = vec4(col, a);
}
