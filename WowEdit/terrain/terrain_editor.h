#pragma once

#include "core/command.h"
#include "core/event_system.h"
#include "terrain/terrain_chunk.h"

#include <cstdint>
#include <vector>

namespace wowedit
{
enum class TerrainTool { Raise, Lower, Smooth, Flatten, Ramp, Noise, Stamp, Erosion };

struct TerrainBrushSettings
{
    float radius = 8.0f;
    float strength = 1.0f;
    BrushFalloffType falloff = BrushFalloffType::Smoothstep;
    float pressure = 1.0f;
    int smoothIterations = 1;
    bool preserveEdges = false;
    float flattenBlend = 0.5f;
    float flattenTargetHeight = 0.0f;
    float maxSlopeDegrees = 45.0f;
    float rampWidth = 4.0f;
    float noiseFrequency = 0.1f;
    float noiseAmplitude = 2.0f;
    std::uint32_t noiseOctaves = 3;
};

class TerrainEditor
{
public:
    explicit TerrainEditor(CommandManager& commands, EventBus* events = nullptr)
        : commands_(commands), events_(events) {}

    TerrainBrushSettings& settings() { return settings_; }
    const TerrainBrushSettings& settings() const { return settings_; }
    void sampleFlattenHeight(const Heightmap& heightmap, float worldX, float worldZ);
    void applyStroke(TerrainChunk& chunk, TerrainTool tool, const glm::vec3& worldPosition);
    void applyRamp(TerrainChunk& chunk, const glm::vec3& start, const glm::vec3& end,
                   float startHeight, float endHeight);
    void applyStamp(TerrainChunk& chunk, const Heightmap& stamp, const glm::vec3& center,
                    float worldScale, float rotationDegrees, float intensity);
    void erodeThermal(TerrainChunk& chunk, int iterations, float talus, float transport);
    void erodeHydraulic(TerrainChunk& chunk, int iterations, float rain, float evaporation, std::uint32_t seed);

private:
    void commitHeightDiff(Heightmap& heightmap, const std::vector<float>& before, const char* description);
    void notifyModified();

    CommandManager& commands_;
    EventBus* events_ = nullptr;
    TerrainBrushSettings settings_;
};
} // namespace wowedit
