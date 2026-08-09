#version 450

// Weighted-Blended OIT variant of model.frag, used ONLY for blend mode 2 (alpha-over) — the one
// order-dependent M2 blend mode. Instead of blending straight to the color target, it emits two
// order-independent accumulators (McGuire/Bavoil 2013): a weighted premultiplied-color sum and a
// revealage product. A later fullscreen pass resolves them onto the color target. It intentionally
// mirrors the World Editor directional/ambient/point/spot/fog evaluation in model.frag.

const int MAX_WORLD_LIGHTS = 16;
struct WorldLight {
    vec4 positionRange;
    vec4 colorIntensity;
    vec4 directionInnerCos;
    vec4 outerType;
};

layout(binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
    vec4 sunDirectionIntensity;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 fogColor;
    vec4 fogParams;
    WorldLight lights[MAX_WORLD_LIGHTS];
} scene;

layout(binding = 1) uniform sampler2D tex;

layout(push_constant) uniform Material {
    mat4 texMatrix;  // (vertex)
    vec4 color;      // animated RGBA modulation
    int  blendMode;  // always 2 here
    int  flags;      // bit0 = unlit; bit1 = liquid (opacity from vertex alpha, not texture)
    int  boneBase;   // (vertex)
    int  pad0;       // aligns highlight to the shared C++ MeshPush layout
    vec4 highlight;  // additive selection/hover tint: rgb + strength in .a; all-zero = none
} mat;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec3 inWorldPos;

layout(location = 0) out vec4  outAccum;    // (premultiplied color, alpha) * weight
layout(location = 1) out float outReveal;   // fragment alpha; blend multiplies dst by (1 - a)

vec3 worldLighting(vec3 worldPos, vec3 normal)
{
    vec3 n = normalize(normal);
    vec3 result = scene.ambientColor.rgb * max(scene.ambientColor.a, 0.0);

    float sunLength = length(scene.sunDirectionIntensity.xyz);
    if (sunLength > 1e-5 && scene.sunDirectionIntensity.a > 0.0)
    {
        vec3 sunDir = scene.sunDirectionIntensity.xyz / sunLength;
        result += scene.sunColor.rgb * scene.sunDirectionIntensity.a * max(dot(n, sunDir), 0.0);
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
        if (light.outerType.y > 0.5)
        {
            float directionLength = length(light.directionInnerCos.xyz);
            if (directionLength <= 1e-5)
                continue;
            float cosAngle = dot(light.directionInnerCos.xyz / directionLength, -lightDir);
            float outerCos = light.outerType.x;
            float innerCos = max(light.directionInnerCos.w, outerCos + 0.0001);
            cone = smoothstep(outerCos, innerCos, cosAngle);
        }
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
    vec4 c = texture(tex, inUV) * mat.color * inColor;
    if (mat.color.a * inColor.a < 0.02)
        discard;

    vec3 col = c.rgb;
    if ((mat.flags & 1) == 0)
        col *= worldLighting(inWorldPos, inNormal);
    col = applyFog(col, inWorldPos);

    // Additive selection/hover highlight (a subtle glow wash over the lit surface).
    col += mat.highlight.rgb * mat.highlight.a;

    float a = ((mat.flags & 2) != 0) ? (mat.color.a * inColor.a) : c.a;

    // McGuire/Bavoil depth weight: nearer fragments dominate. gl_FragCoord.z is window-space
    // (non-linear) — an approximation that avoids threading eye-space depth from the vertex shader.
    float w = a * clamp(3e3 * pow(1.0 - gl_FragCoord.z, 3.0), 1e-2, 3e3);

    outAccum  = vec4(col * a, a) * w;
    outReveal = a;
}
