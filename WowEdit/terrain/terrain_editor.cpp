#include "terrain/terrain_editor.h"

#include "core/commands.h"
#include "utils/math_utils.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace wowedit
{
void TerrainEditor::sampleFlattenHeight(const Heightmap& heightmap, float worldX, float worldZ)
{
    settings_.flattenTargetHeight = heightmap.getInterpolatedHeight(worldX, worldZ);
}

void TerrainEditor::applyStroke(TerrainChunk& chunk, TerrainTool tool, const glm::vec3& worldPosition)
{
    Heightmap& heightmap = chunk.heightmap;
    const std::vector<float> before = heightmap.data();
    switch (tool)
    {
    case TerrainTool::Raise:
        heightmap.applyBrush(worldPosition, settings_.radius, settings_.strength, settings_.falloff,
                              Heightmap::BrushOperation::Raise, settings_.pressure);
        break;
    case TerrainTool::Lower:
        heightmap.applyBrush(worldPosition, settings_.radius, settings_.strength, settings_.falloff,
                              Heightmap::BrushOperation::Lower, settings_.pressure);
        break;
    case TerrainTool::Smooth:
        for (int iteration = 0; iteration < std::max(1, settings_.smoothIterations); ++iteration)
            heightmap.applyBrush(worldPosition, settings_.radius, settings_.strength, settings_.falloff,
                                  Heightmap::BrushOperation::Smooth, settings_.pressure);
        break;
    case TerrainTool::Flatten:
        heightmap.setFlattenTarget(settings_.flattenTargetHeight);
        heightmap.applyBrush(worldPosition, settings_.radius, settings_.flattenBlend, settings_.falloff,
                              Heightmap::BrushOperation::Flatten, settings_.pressure);
        break;
    case TerrainTool::Noise:
        heightmap.setNoiseSettings(settings_.noiseFrequency, settings_.noiseAmplitude, settings_.noiseOctaves, 0);
        heightmap.applyBrush(worldPosition, settings_.radius, settings_.strength, settings_.falloff,
                              Heightmap::BrushOperation::Noise, settings_.pressure);
        break;
    case TerrainTool::Ramp:
    case TerrainTool::Stamp:
    case TerrainTool::Erosion:
        return;
    }
    commitHeightDiff(heightmap, before, "Terrain brush stroke");
}

void TerrainEditor::applyRamp(TerrainChunk& chunk, const glm::vec3& start, const glm::vec3& end,
                              float startHeight, float endHeight)
{
    Heightmap& heightmap = chunk.heightmap;
    const std::vector<float> before = heightmap.data();
    const glm::vec2 begin(start.x, start.z);
    const glm::vec2 finish(end.x, end.z);
    const glm::vec2 direction = finish - begin;
    const float lengthSquared = std::max(0.0001f, glm::dot(direction, direction));
    const float halfWidth = std::max(0.05f, settings_.rampWidth * 0.5f);
    const float maxSlope = std::tan(glm::radians(std::max(0.1f, settings_.maxSlopeDegrees)));
    const float desiredSlope = std::abs(endHeight - startHeight) / std::sqrt(lengthSquared);
    const float allowedEnd = desiredSlope > maxSlope
        ? startHeight + glm::sign(endHeight - startHeight) * maxSlope * std::sqrt(lengthSquared)
        : endHeight;
    for (std::uint32_t y = 0; y < heightmap.getHeightCount(); ++y)
        for (std::uint32_t x = 0; x < heightmap.getWidth(); ++x)
        {
            const glm::vec2 point(static_cast<float>(x) * heightmap.getScale(), static_cast<float>(y) * heightmap.getScale());
            const float t = glm::clamp(glm::dot(point - begin, direction) / lengthSquared, 0.0f, 1.0f);
            const glm::vec2 nearest = begin + direction * t;
            const float lateral = glm::length(point - nearest);
            if (lateral > halfWidth)
                continue;
            const float edgeBlend = 1.0f - math::SmoothStep(0.0f, halfWidth, lateral);
            const float target = glm::mix(startHeight, allowedEnd, t);
            heightmap.setHeight(x, y, glm::mix(heightmap.getHeight(x, y), target, edgeBlend));
        }
    commitHeightDiff(heightmap, before, "Create terrain ramp");
}

void TerrainEditor::applyStamp(TerrainChunk& chunk, const Heightmap& stamp, const glm::vec3& center,
                               float worldScale, float rotationDegrees, float intensity)
{
    if (stamp.getWidth() == 0 || stamp.getHeightCount() == 0 || worldScale <= 0.0f)
        return;
    Heightmap& heightmap = chunk.heightmap;
    const std::vector<float> before = heightmap.data();
    const glm::mat2 inverseRotation(glm::vec2(std::cos(glm::radians(rotationDegrees)), -std::sin(glm::radians(rotationDegrees))),
                                    glm::vec2(std::sin(glm::radians(rotationDegrees)), std::cos(glm::radians(rotationDegrees))));
    const float halfX = static_cast<float>(stamp.getWidth() - 1) * stamp.getScale() * worldScale * 0.5f;
    const float halfY = static_cast<float>(stamp.getHeightCount() - 1) * stamp.getScale() * worldScale * 0.5f;
    for (std::uint32_t y = 0; y < heightmap.getHeightCount(); ++y)
        for (std::uint32_t x = 0; x < heightmap.getWidth(); ++x)
        {
            const glm::vec2 local = inverseRotation *
                (glm::vec2(static_cast<float>(x) * heightmap.getScale() - center.x,
                           static_cast<float>(y) * heightmap.getScale() - center.z));
            if (local.x < -halfX || local.x > halfX || local.y < -halfY || local.y > halfY)
                continue;
            const float sx = (local.x + halfX) / worldScale;
            const float sy = (local.y + halfY) / worldScale;
            heightmap.modifyHeight(x, y, stamp.getInterpolatedHeight(sx, sy) * intensity);
        }
    commitHeightDiff(heightmap, before, "Apply terrain stamp");
}

void TerrainEditor::erodeThermal(TerrainChunk& chunk, int iterations, float talus, float transport)
{
    Heightmap& heightmap = chunk.heightmap;
    const std::vector<float> before = heightmap.data();
    const float threshold = std::max(0.0f, talus);
    for (int iteration = 0; iteration < std::max(0, iterations); ++iteration)
    {
        const std::vector<float> old = heightmap.data();
        std::vector<float>& current = heightmap.mutableData();
        for (std::uint32_t y = 1; y + 1 < heightmap.getHeightCount(); ++y)
            for (std::uint32_t x = 1; x + 1 < heightmap.getWidth(); ++x)
            {
                const std::size_t center = static_cast<std::size_t>(y) * heightmap.getWidth() + x;
                static constexpr int offsets[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& offset : offsets)
                {
                    const std::size_t neighbor = static_cast<std::size_t>(static_cast<int>(y) + offset[1]) * heightmap.getWidth() +
                                                 static_cast<int>(x) + offset[0];
                    const float delta = old[center] - old[neighbor];
                    if (delta > threshold)
                    {
                        const float moved = std::min(delta * 0.5f, transport * (delta - threshold));
                        current[center] -= moved;
                        current[neighbor] += moved;
                    }
                }
            }
    }
    commitHeightDiff(heightmap, before, "Thermal erosion");
}

void TerrainEditor::erodeHydraulic(TerrainChunk& chunk, int iterations, float rain, float evaporation, std::uint32_t seed)
{
    Heightmap& heightmap = chunk.heightmap;
    const std::vector<float> before = heightmap.data();
    if (heightmap.getWidth() < 3 || heightmap.getHeightCount() < 3)
        return;
    std::mt19937 random(seed);
    std::uniform_int_distribution<std::uint32_t> xDistribution(1, heightmap.getWidth() - 2);
    std::uniform_int_distribution<std::uint32_t> yDistribution(1, heightmap.getHeightCount() - 2);
    for (int drop = 0; drop < std::max(0, iterations); ++drop)
    {
        std::uint32_t x = xDistribution(random);
        std::uint32_t y = yDistribution(random);
        float water = std::max(0.0f, rain);
        float sediment = 0.0f;
        for (int step = 0; step < 32 && water > 0.001f; ++step)
        {
            std::uint32_t bestX = x, bestY = y;
            float bestHeight = heightmap.getHeight(x, y);
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox)
                {
                    const std::uint32_t nx = static_cast<std::uint32_t>(static_cast<int>(x) + ox);
                    const std::uint32_t ny = static_cast<std::uint32_t>(static_cast<int>(y) + oy);
                    if (heightmap.getHeight(nx, ny) < bestHeight)
                    {
                        bestHeight = heightmap.getHeight(nx, ny);
                        bestX = nx;
                        bestY = ny;
                    }
                }
            const float current = heightmap.getHeight(x, y);
            const float capacity = std::max(0.0f, current - bestHeight) * water * 0.5f;
            if (sediment > capacity)
            {
                const float deposit = (sediment - capacity) * 0.25f;
                heightmap.modifyHeight(x, y, deposit);
                sediment -= deposit;
            }
            else
            {
                const float eroded = std::min(current, (capacity - sediment) * 0.1f);
                heightmap.modifyHeight(x, y, -eroded);
                sediment += eroded;
            }
            x = bestX;
            y = bestY;
            water *= glm::clamp(1.0f - evaporation, 0.0f, 1.0f);
        }
    }
    commitHeightDiff(heightmap, before, "Hydraulic erosion");
}

void TerrainEditor::commitHeightDiff(Heightmap& heightmap, const std::vector<float>& before, const char* description)
{
    std::vector<TerrainHeightDelta> changes;
    const std::vector<float>& after = heightmap.data();
    if (before.size() != after.size())
        return;
    for (std::uint32_t y = 0; y < heightmap.getHeightCount(); ++y)
        for (std::uint32_t x = 0; x < heightmap.getWidth(); ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y) * heightmap.getWidth() + x;
            if (std::abs(before[i] - after[i]) > 1e-6f)
                changes.push_back({x, y, before[i], after[i]});
        }
    if (changes.empty())
        return;
    // The brush already produced the preview state. Restore the original values;
    // CommandManager::executeCommand then reapplies the exact committed deltas.
    heightmap.mutableData() = before;
    commands_.executeCommand(std::make_unique<TerrainHeightModifyCommand>(heightmap, std::move(changes), description));
    notifyModified();
}

void TerrainEditor::notifyModified()
{
    if (events_)
        events_->publish({EventType::TerrainModified, 0, "TerrainEditor", {}});
}
} // namespace wowedit
