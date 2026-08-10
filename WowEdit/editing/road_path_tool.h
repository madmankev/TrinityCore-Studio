#pragma once

#include "core/command.h"
#include "core/event_system.h"
#include "core/types.h"
#include "terrain/terrain_chunk.h"

#include <cstdint>
#include <vector>

namespace wowedit
{
/** Parameters for a terrain-conforming road, trail, riverbank, or wall foundation. */
struct RoadPathSettings
{
    float width = 5.0f;                 // finished road surface width in world units
    float shoulderWidth = 2.0f;         // smooth blend outside the paved surface
    float elevationBlend = 0.85f;       // 0..1 blend toward centerline grade
    float maxSlopeDegrees = 22.5f;      // clamps authored control-point grade
    float sampleSpacing = 1.0f;         // preview mesh density / path sampling
    bool conformToTerrain = true;       // derive each centerline height from the existing terrain
    bool paintTexture = true;
    std::uint8_t textureLayer = 1;
    float textureOpacity = 0.90f;
};

struct RoadBuildResult
{
    std::uint32_t terrainVerticesChanged = 0;
    std::uint32_t textureTexelsChanged = 0;
    float length = 0.0f;
    bool changed() const { return terrainVerticesChanged != 0 || textureTexelsChanged != 0; }
};

/**
 * Command-backed road/path authoring. It produces a centerline-conforming grade,
 * soft shoulders, material paint, and a preview strip without depending on a game
 * engine. The same model can drive an ImGui, Qt, or plugin-facing road panel.
 */
class RoadPathTool
{
public:
    explicit RoadPathTool(CommandManager& commands, EventBus* events = nullptr)
        : commands_(commands), events_(events) {}

    RoadPathSettings& settings() { return settings_; }
    const RoadPathSettings& settings() const { return settings_; }

    /** Snap arbitrary control points to current terrain, preserving X/Z. */
    void snapControlPoints(const Heightmap& heightmap, std::vector<glm::vec3>& points) const;

    /** Build height/texture changes as one undoable macro. Returns exact modified counts. */
    RoadBuildResult build(TerrainChunk& chunk, const std::vector<glm::vec3>& controlPoints);

    /** Build a CPU mesh for an editor preview, export, or a renderer adapter. */
    Mesh buildPreviewMesh(const TerrainChunk& chunk, const std::vector<glm::vec3>& controlPoints) const;

private:
    struct Segment
    {
        glm::vec3 a{0.0f};
        glm::vec3 b{0.0f};
        glm::vec2 horizontal{0.0f};
        float horizontalLength = 0.0f;
        float yA = 0.0f;
        float yB = 0.0f;
    };
    struct NearestPoint
    {
        bool valid = false;
        float lateralDistance = 0.0f;
        float centerHeight = 0.0f;
        glm::vec2 tangent{1.0f, 0.0f};
    };

    std::vector<Segment> makeSegments(const Heightmap& heightmap,
                                      const std::vector<glm::vec3>& controlPoints) const;
    NearestPoint nearest(const std::vector<Segment>& segments, const glm::vec2& world) const;
    float roadWeight(float lateralDistance) const;
    void publish(EventType event) const;

    CommandManager& commands_;
    EventBus* events_ = nullptr;
    RoadPathSettings settings_;
};
} // namespace wowedit
