#version 450

// ADT terrain fragment shader. Each chunk blends up to 4 tiled ground textures using a
// packed alpha map (R/G/B = layer 1/2/3 weights; layer 0's weight is 1 - their sum), then
// modulates by the MCCV vertex color and a fixed directional sun (matching model.frag), so
// terrain relief reads the same as M2/WMO geometry. WotLK blend formula:
//   finalColor = tex0*(1 - (a1+a2+a3)) + tex1*a1 + tex2*a2 + tex3*a3

layout(binding = 1) uniform sampler2D layers[4];   // the chunk's up-to-4 ground textures
layout(binding = 2) uniform sampler2D alphaMap;    // 64x64 packed layer weights (RGB)

layout(push_constant) uniform Terrain {
    float tileFactor;   // ground-texture repeats per chunk (UV 0..1 -> 0..tileFactor)
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 a = texture(alphaMap, inUV).rgb;                 // layer 1,2,3 weights
    float w0 = clamp(1.0 - (a.r + a.g + a.b), 0.0, 1.0);  // base layer fills the remainder

    vec2 tuv = inUV * pc.tileFactor;
    vec3 col = texture(layers[0], tuv).rgb * w0
             + texture(layers[1], tuv).rgb * a.r
             + texture(layers[2], tuv).rgb * a.g
             + texture(layers[3], tuv).rgb * a.b;

    col *= inColor.rgb;   // MCCV (2x already folded in on the CPU: 0x7F -> 1.0)

    float ndl = max(dot(normalize(inNormal), normalize(vec3(0.35, 0.4, 0.85))), 0.0);
    col *= (0.45 + 0.55 * ndl);

    outColor = vec4(col, 1.0);
}
