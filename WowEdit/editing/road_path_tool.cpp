#include "editing/road_path_tool.h"

#include "core/commands.h"
#include "utils/math_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wowedit
{
namespace
{
float ClampSlopeEnd(float start, float requestedEnd, float horizontalLength, float maxSlopeDegrees)
{
    const float maxSlope = std::tan(glm::radians(glm::clamp(maxSlopeDegrees, 0.1f, 89.0f)));
    const float maxDelta = maxSlope * horizontalLength;
    return glm::clamp(requestedEnd, start - maxDelta, start + maxDelta);
}
} // namespace

void RoadPathTool::snapControlPoints(const Heightmap& heightmap, std::vector<glm::vec3>& points) const
{
    for (glm::vec3& point : points)
        point.y = heightmap.getInterpolatedHeight(point.x, point.z);
}

std::vector<RoadPathTool::Segment> RoadPathTool::makeSegments(
    const Heightmap& heightmap, const std::vector<glm::vec3>& controlPoints) const
{
    std::vector<Segment> segments;
    if (controlPoints.size() < 2)
        return segments;

    glm::vec3 previous = controlPoints.front();
    if (settings_.conformToTerrain)
        previous.y = heightmap.getInterpolatedHeight(previous.x, previous.z);
    for (std::size_t index = 1; index < controlPoints.size(); ++index)
    {
        glm::vec3 next = controlPoints[index];
        if (settings_.conformToTerrain)
            next.y = heightmap.getInterpolatedHeight(next.x, next.z);
        Segment segment;
        segment.a = previous;
        segment.b = next;
        segment.horizontal = glm::vec2(next.x - previous.x, next.z - previous.z);
        segment.horizontalLength = glm::length(segment.horizontal);
        if (segment.horizontalLength <= 1e-4f)
        {
            previous = next;
            continue;
        }
        segment.yA = previous.y;
        segment.yB = ClampSlopeEnd(segment.yA, next.y, segment.horizontalLength, settings_.maxSlopeDegrees);
        segment.b.y = segment.yB;
        segments.push_back(segment);
        previous = segment.b;
    }
    return segments;
}

RoadPathTool::NearestPoint RoadPathTool::nearest(const std::vector<Segment>& segments, const glm::vec2& world) const
{
    NearestPoint best;
    float bestDistanceSquared = std::numeric_limits<float>::max();
    for (const Segment& segment : segments)
    {
        const glm::vec2 start(segment.a.x, segment.a.z);
        const float t = glm::clamp(glm::dot(world - start, segment.horizontal) /
                                   (segment.horizontalLength * segment.horizontalLength), 0.0f, 1.0f);
        const glm::vec2 closest = start + segment.horizontal * t;
        const float distanceSquared = glm::dot(world - closest, world - closest);
        if (distanceSquared >= bestDistanceSquared)
            continue;
        bestDistanceSquared = distanceSquared;
        best.valid = true;
        best.lateralDistance = std::sqrt(distanceSquared);
        best.centerHeight = glm::mix(segment.yA, segment.yB, t);
        best.tangent = segment.horizontal / segment.horizontalLength;
    }
    return best;
}

float RoadPathTool::roadWeight(float lateralDistance) const
{
    const float inner = std::max(0.05f, settings_.width * 0.5f);
    const float outer = inner + std::max(0.0f, settings_.shoulderWidth);
    if (lateralDistance <= inner)
        return 1.0f;
    if (lateralDistance >= outer)
        return 0.0f;
    return 1.0f - math::SmoothStep(inner, outer, lateralDistance);
}

RoadBuildResult RoadPathTool::build(TerrainChunk& chunk, const std::vector<glm::vec3>& controlPoints)
{
    RoadBuildResult result;
    Heightmap& heightmap = chunk.heightmap;
    TextureSplatmap& splatmap = chunk.splatmap;
    const std::vector<Segment> segments = makeSegments(heightmap, controlPoints);
    if (segments.empty() || heightmap.getWidth() == 0 || heightmap.getHeightCount() == 0)
        return result;

    for (const Segment& segment : segments)
        result.length += segment.horizontalLength;

    const std::vector<float> beforeHeights = heightmap.data();
    const std::vector<glm::u8vec4> beforeSplat = splatmap.getSplatData();
    const float blend = glm::clamp(settings_.elevationBlend, 0.0f, 1.0f);
    for (std::uint32_t y = 0; y < heightmap.getHeightCount(); ++y)
        for (std::uint32_t x = 0; x < heightmap.getWidth(); ++x)
        {
            const glm::vec2 world(static_cast<float>(x) * heightmap.getScale(),
                                  static_cast<float>(y) * heightmap.getScale());
            const NearestPoint point = nearest(segments, world);
            const float weight = point.valid ? roadWeight(point.lateralDistance) : 0.0f;
            if (weight <= 1e-5f)
                continue;
            const std::size_t vertex = static_cast<std::size_t>(y) * heightmap.getWidth() + x;
            const float resultHeight = glm::mix(beforeHeights[vertex], point.centerHeight, blend * weight);
            heightmap.setHeight(x, y, resultHeight);
            if (settings_.paintTexture && x < splatmap.getWidth() && y < splatmap.getHeight())
                splatmap.paintTexture(x, y, settings_.textureLayer,
                                      glm::clamp(settings_.textureOpacity, 0.0f, 1.0f) * weight,
                                      1.0f, 1.0f);
        }

    std::vector<TerrainHeightDelta> heightChanges;
    heightChanges.reserve(beforeHeights.size() / 8);
    const std::vector<float>& afterHeights = heightmap.data();
    for (std::uint32_t y = 0; y < heightmap.getHeightCount(); ++y)
        for (std::uint32_t x = 0; x < heightmap.getWidth(); ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * heightmap.getWidth() + x;
            if (std::abs(beforeHeights[index] - afterHeights[index]) > 1e-6f)
                heightChanges.push_back({x, y, beforeHeights[index], afterHeights[index]});
        }

    std::vector<TexturePaintChange> textureChanges;
    const std::vector<glm::u8vec4>& afterSplat = splatmap.getSplatData();
    const std::uint32_t textureWidth = splatmap.getWidth();
    for (std::uint32_t y = 0; y < splatmap.getHeight(); ++y)
        for (std::uint32_t x = 0; x < splatmap.getWidth(); ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * textureWidth + x;
            if (beforeSplat[index] != afterSplat[index])
                textureChanges.push_back({x, y, beforeSplat[index], afterSplat[index]});
        }

    if (heightChanges.empty() && textureChanges.empty())
        return result;

    // Both tools paint their preview before history submission. Restore exact prior
    // samples, then command execution reapplies one atomic road operation for undo/redo.
    heightmap.mutableData() = beforeHeights;
    for (const TexturePaintChange& change : textureChanges)
        splatmap.setTexel(change.x, change.y, change.before);

    commands_.beginMacro("Build terrain road");
    if (!heightChanges.empty())
    {
        result.terrainVerticesChanged = static_cast<std::uint32_t>(heightChanges.size());
        commands_.executeCommand(std::make_unique<TerrainHeightModifyCommand>(
            heightmap, std::move(heightChanges), "Grade terrain road"));
    }
    if (!textureChanges.empty())
    {
        result.textureTexelsChanged = static_cast<std::uint32_t>(textureChanges.size());
        commands_.executeCommand(std::make_unique<TexturePaintCommand>(
            splatmap, std::move(textureChanges), "Paint terrain road"));
    }
    commands_.endMacro();
    if (result.terrainVerticesChanged)
        publish(EventType::TerrainModified);
    if (result.textureTexelsChanged)
        publish(EventType::TexturePainted);
    return result;
}

Mesh RoadPathTool::buildPreviewMesh(const TerrainChunk& chunk,
                                    const std::vector<glm::vec3>& controlPoints) const
{
    Mesh mesh;
    const std::vector<Segment> segments = makeSegments(chunk.heightmap, controlPoints);
    if (segments.empty())
        return mesh;

    const float spacing = std::max(settings_.sampleSpacing, chunk.heightmap.getScale() * 0.25f);
    const float halfWidth = std::max(0.05f, settings_.width * 0.5f);
    std::vector<glm::vec3> centers;
    std::vector<glm::vec2> tangents;
    for (std::size_t segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex)
    {
        const Segment& segment = segments[segmentIndex];
        const std::uint32_t count = std::max<std::uint32_t>(1u,
            static_cast<std::uint32_t>(std::ceil(segment.horizontalLength / spacing)));
        for (std::uint32_t sample = segmentIndex == 0 ? 0u : 1u; sample <= count; ++sample)
        {
            const float t = static_cast<float>(sample) / static_cast<float>(count);
            centers.push_back(glm::vec3(glm::mix(glm::vec2(segment.a.x, segment.a.z),
                                                   glm::vec2(segment.b.x, segment.b.z), t).x,
                                        glm::mix(segment.yA, segment.yB, t),
                                        glm::mix(glm::vec2(segment.a.x, segment.a.z),
                                                 glm::vec2(segment.b.x, segment.b.z), t).y));
            tangents.push_back(segment.horizontal / segment.horizontalLength);
        }
    }
    if (centers.size() < 2)
        return mesh;

    mesh.vertices.reserve(centers.size() * 2);
    mesh.indices.reserve((centers.size() - 1) * 6);
    for (std::size_t index = 0; index < centers.size(); ++index)
    {
        const glm::vec2 side(-tangents[index].y, tangents[index].x);
        const glm::vec3 left = centers[index] + glm::vec3(side.x * halfWidth, 0.02f, side.y * halfWidth);
        const glm::vec3 right = centers[index] - glm::vec3(side.x * halfWidth, -0.02f, side.y * halfWidth);
        const float v = static_cast<float>(index) / static_cast<float>(centers.size() - 1);
        mesh.vertices.push_back({left, {0.0f, 1.0f, 0.0f}, {0.0f, v}, {1.0f, 0.76f, 0.25f, 0.85f}});
        mesh.vertices.push_back({right, {0.0f, 1.0f, 0.0f}, {1.0f, v}, {1.0f, 0.76f, 0.25f, 0.85f}});
    }
    for (std::uint32_t index = 0; index + 1 < centers.size(); ++index)
    {
        const std::uint32_t a = index * 2;
        mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 2, a + 2, a + 1, a + 3});
    }
    return mesh;
}

void RoadPathTool::publish(EventType event) const
{
    if (events_)
        events_->publish({event, 0, "RoadPathTool", {}});
}
} // namespace wowedit
