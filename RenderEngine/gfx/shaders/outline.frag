#version 450

// Selection-outline fragment shader: a flat, opaque outline color. Paired with outline.vert's
// inverted-hull expansion + the outline pipeline's front-face culling.

layout(push_constant) uniform Material {
    mat4 texMatrix;   // unused
    vec4 color;       // unused
    int  blendMode;   // unused
    int  flags;       // unused
    int  boneBase;    // unused (vertex)
    vec4 highlight;   // rgb = outline color; .a = width (vertex)
} mat;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(mat.highlight.rgb, 1.0);
}
