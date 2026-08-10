#version 450

// ADT terrain vertex shader. No skinning: terrain is static. Passes chunk-local UV (0..1, for the
// alpha map), the MCNR normal, the MCCV vertex color, and the per-vertex CHUNK ID (packed into the
// unused bone-index slot) so the fragment shader can look up that chunk's layer/alpha parameters.

const int MAX_WORLD_LIGHTS = 16;
const int MAX_TERRAIN_PREVIEW_STROKES = 32;
struct WorldLight {
    vec4 positionRange;
    vec4 colorIntensity;
    vec4 directionInnerCos;
    vec4 outerType;
};
struct TerrainPreviewStroke {
    vec4 centerRadius; // xy local center, z radius
    vec4 params;       // x strength, y flatten Z, z mode
};

layout(set = 0, binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
    vec4 sunDirectionIntensity;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 fogColor;
    vec4 fogParams;
    WorldLight lights[MAX_WORLD_LIGHTS];
    vec4 terrainPreviewParams; // x active stroke count
    TerrainPreviewStroke terrainPreview[MAX_TERRAIN_PREVIEW_STROKES];
} scene;

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in uvec4 inBone;   // .x = chunk id (0..255); other bone attrs unused
layout(location = 5) in vec4  inColor;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColor;
layout(location = 3) flat out uint outChunk;
layout(location = 4) out vec3 outWorldPos;
layout(location = 5) out float outPreviewDelta;

// Match AdtWriter::SmoothFalloff + SculptTerrain exactly enough for the live stage: strokes are
// applied in chronological order, so a later Flatten sees the height from earlier Raise/Lower work.
float previewHeight(vec3 position)
{
    float z = position.z;
    int count = clamp(int(scene.terrainPreviewParams.x + 0.5), 0, MAX_TERRAIN_PREVIEW_STROKES);
    for (int i = 0; i < count; ++i)
    {
        TerrainPreviewStroke stroke = scene.terrainPreview[i];
        float radius = stroke.centerRadius.z;
        if (radius <= 0.001)
            continue;
        float distanceToCenter = length(position.xy - stroke.centerRadius.xy);
        if (distanceToCenter > radius)
            continue;
        float t = clamp(1.0 - distanceToCenter / radius, 0.0, 1.0);
        float falloff = t * t * (3.0 - 2.0 * t);
        if (stroke.params.z < 0.5)
            z += stroke.params.x * falloff;
        else if (stroke.params.z < 1.5)
            z -= stroke.params.x * falloff;
        else
            z += (stroke.params.y - z) * falloff;
    }
    return z;
}

void main()
{
    vec3 previewPos = inPos;
    previewPos.z = previewHeight(inPos);
    gl_Position = scene.proj * scene.view * vec4(previewPos, 1.0);
    outUV = inUV;
    outNormal = inNormal; // terrain.frag derives an accurate preview normal from outWorldPos when needed
    outColor = inColor;
    outChunk = inBone.x;
    outWorldPos = previewPos;
    outPreviewDelta = previewPos.z - inPos.z;
}
