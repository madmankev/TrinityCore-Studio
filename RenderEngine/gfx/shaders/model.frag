#version 450

// M2 model fragment shader. The blend equation itself is baked into the pipeline (one
// per M2 blend mode); this shader handles the two per-material bits that can't be: the
// alpha-key discard (blend mode 1) and unlit materials (flag 0x01, e.g. additive glows).
// World Editor lighting arrives in the shared Scene UBO: directional sun + ambient + a small,
// camera-nearest point/spot-light list authored by the Light Editor.

const int MAX_WORLD_LIGHTS = 16;
struct WorldLight {
    vec4 positionRange;      // xyz position, w range
    vec4 colorIntensity;     // rgb color, w intensity
    vec4 directionInnerCos;  // xyz spot direction, w cos(inner cone)
    vec4 outerType;          // x cos(outer cone), y type, z falloff exponent
};

layout(binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
    vec4 sunDirectionIntensity;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 fogColor;
    vec4 fogParams;          // x start, y end, z enabled, w light count
    WorldLight lights[MAX_WORLD_LIGHTS];
} scene;

layout(binding = 1) uniform sampler2D tex;

layout(push_constant) uniform Material {
    mat4 texMatrix;  // (vertex)
    vec4 color;      // animated RGBA modulation
    int  blendMode;  // M2Material.blending_mode (0..6)
    int  flags;      // bit0 = unlit; bit1 = liquid (opacity from vertex alpha, not texture)
    int  boneBase;   // (vertex)
    int  pad0;       // aligns highlight to the shared C++ MeshPush layout
    vec4 highlight;  // additive selection/hover tint: rgb + strength in .a; all-zero = none
} mat;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;   // baked per-vertex color (WMO MOCV); white for M2
layout(location = 3) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

vec3 worldLighting(vec3 worldPos, vec3 normal)
{
    vec3 n = normalize(normal);
    vec3 result = scene.ambientColor.rgb * max(scene.ambientColor.a, 0.0);

    float sunLength = length(scene.sunDirectionIntensity.xyz);
    if (sunLength > 1e-5 && scene.sunDirectionIntensity.a > 0.0)
    {
        vec3 sunDir = scene.sunDirectionIntensity.xyz / sunLength;
        float ndl = max(dot(n, sunDir), 0.0);
        result += scene.sunColor.rgb * scene.sunDirectionIntensity.a * ndl;
    }

    int count = clamp(int(scene.fogParams.w + 0.5), 0, MAX_WORLD_LIGHTS);
    for (int i = 0; i < count; ++i)
    {
        WorldLight light = scene.lights[i];
        float range = light.positionRange.w;
        float intensity = light.colorIntensity.w;
        if (range <= 0.001 || intensity <= 0.0)
            continue;

        vec3 toLight = light.positionRange.xyz - worldPos;
        float distanceToLight = length(toLight);
        if (distanceToLight >= range || distanceToLight <= 1e-5)
            continue;
        vec3 lightDir = toLight / distanceToLight;
        float attenuation = pow(clamp(1.0 - distanceToLight / range, 0.0, 1.0),
                                max(light.outerType.z, 0.05));

        float cone = 1.0;
        if (light.outerType.y > 0.5) // spot light
        {
            float directionLength = length(light.directionInnerCos.xyz);
            if (directionLength <= 1e-5)
                continue;
            vec3 fromLight = -lightDir;
            float cosAngle = dot(light.directionInnerCos.xyz / directionLength, fromLight);
            float outerCos = light.outerType.x;
            float innerCos = max(light.directionInnerCos.w, outerCos + 0.0001);
            cone = smoothstep(outerCos, innerCos, cosAngle);
        }

        // Keep a small wrap term so an editor point light remains legible on rough/low-poly
        // terrain while still preserving directional surface definition.
        float diffuse = 0.15 + 0.85 * max(dot(n, lightDir), 0.0);
        result += light.colorIntensity.rgb * intensity * attenuation * cone * diffuse;
    }
    return result;
}

vec3 applyFog(vec3 color, vec3 worldPos)
{
    if (scene.fogParams.z <= 0.5)
        return color;
    float begin = max(scene.fogParams.x, 0.0);
    float end = max(scene.fogParams.y, begin + 0.01);
    float viewDistance = length((scene.view * vec4(worldPos, 1.0)).xyz);
    return mix(color, scene.fogColor.rgb, smoothstep(begin, end, viewDistance));
}

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
        col *= worldLighting(inWorldPos, inNormal);
    col = applyFog(col, inWorldPos);

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
