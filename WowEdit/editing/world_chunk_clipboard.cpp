#include "editing/world_chunk_clipboard.h"

#include "core/commands.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace wowedit
{
namespace
{
bool Inside(glm::vec3 position, glm::vec2 minimum, glm::vec2 maximum)
{
    return position.x >= minimum.x && position.x <= maximum.x && position.z >= minimum.y && position.z <= maximum.y;
}

std::uint64_t NextDoodadId(const std::vector<Doodad>& values)
{
    std::uint64_t result = 1;
    for (const Doodad& value : values)
        result = std::max(result, value.uniqueId + 1);
    return result;
}

std::uint64_t NextCreatureId(const std::vector<CreatureSpawner>& values)
{
    std::uint64_t result = 1;
    for (const CreatureSpawner& value : values)
        result = std::max(result, value.uniqueId + 1);
    return result;
}
} // namespace

void WorldChunkClipboard::copyArea(glm::vec2 minimum, glm::vec2 maximum, CopyOptions options)
{
    if (!source_)
        return;
    minimum = glm::min(minimum, maximum);
    maximum = glm::max(minimum, maximum);
    const ClipboardContent before = content;
    ClipboardContent copied;
    copied.relativePositioning = options.relativePositioning;
    const Heightmap& heightmap = source_->heightmap;
    const float scale = heightmap.getScale();
    const int minX = std::max(0, static_cast<int>(std::floor(minimum.x / scale)));
    const int minY = std::max(0, static_cast<int>(std::floor(minimum.y / scale)));
    const int maxX = std::min(static_cast<int>(heightmap.getWidth()) - 1, static_cast<int>(std::ceil(maximum.x / scale)));
    const int maxY = std::min(static_cast<int>(heightmap.getHeightCount()) - 1, static_cast<int>(std::ceil(maximum.y / scale)));
    if (maxX < minX || maxY < minY)
        return;
    copied.sourceOrigin = {minX, minY};
    copied.dimensions = {maxX - minX + 1, maxY - minY + 1};
    copied.sourceCenterPoint = {(minimum.x + maximum.x) * 0.5f, 0.0f, (minimum.y + maximum.y) * 0.5f};

    if (options.includeTerrain)
    {
        copied.heights.reserve(static_cast<std::size_t>(copied.dimensions.x) * copied.dimensions.y);
        for (int y = minY; y <= maxY; ++y)
            for (int x = minX; x <= maxX; ++x)
                copied.heights.push_back(heightmap.getHeight(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
        copied.heightmapDelta = encodeHeights(copied.heights);
    }
    if (options.includeTextures)
    {
        copied.splats.reserve(static_cast<std::size_t>(copied.dimensions.x) * copied.dimensions.y);
        for (int y = minY; y <= maxY; ++y)
            for (int x = minX; x <= maxX; ++x)
            {
                const glm::u8vec4 texel = source_->splatmap.getTexel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                copied.splats.push_back(texel);
                copied.splatmapData.insert(copied.splatmapData.end(), {texel.r, texel.g, texel.b, texel.a});
            }
    }
    if (options.includeDoodads)
        for (const Doodad& original : source_->doodads)
            if (Inside(original.position, minimum, maximum))
            {
                Doodad copy = original;
                if (copied.relativePositioning)
                    copy.position -= copied.sourceCenterPoint;
                copied.doodads.push_back(std::move(copy));
            }
    if (options.includeCreatures)
        for (const CreatureSpawner& original : source_->creatures)
            if (Inside(original.position, minimum, maximum))
            {
                CreatureSpawner copy = original;
                if (copied.relativePositioning)
                {
                    copy.position -= copied.sourceCenterPoint;
                    for (const Waypoint& point : copy.waypointSystem.points())
                        copy.waypointSystem.move(point.id, point.position - copied.sourceCenterPoint);
                }
                copied.creatures.push_back(std::move(copy));
            }
    if (options.includeWater)
        for (int y = minY; y <= maxY; ++y)
            for (int x = minX; x <= maxX; ++x)
                if (source_->water.hasLiquidCell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)))
                {
                    const WaterPlane::LiquidCell& cell = source_->water.cells()[static_cast<std::size_t>(y) * heightmap.getWidth() + x];
                    copied.waterCells.emplace_back(static_cast<float>(x - minX), source_->water.getGlobalHeight() + cell.heightVariation,
                                                    static_cast<float>(y - minY));
                }

    auto apply = [this, copied]() { content = copied; hasContent = true; };
    auto restore = [this, before]() { content = before; hasContent = !content.heights.empty() || !content.splats.empty() ||
                                                       !content.doodads.empty() || !content.creatures.empty() || !content.waterCells.empty(); };
    if (commands_)
        commands_->executeCommand(std::make_unique<WorldChunkCopyCommand>(std::move(apply), std::move(restore)));
    else
        apply();
}

void WorldChunkClipboard::pasteAt(glm::vec2 position, PasteMode mode)
{
    if (!target_ || !hasContent)
        return;
    TerrainChunk before = *target_;
    TerrainChunk after = before;
    applyPaste(after, position, mode);
    TerrainChunk* const target = target_;
    auto apply = [target, after]() { *target = after; };
    auto restore = [target, before]() { *target = before; };
    if (commands_)
        commands_->executeCommand(std::make_unique<WorldChunkPasteCommand>(std::move(apply), std::move(restore)));
    else
        apply();
}

void WorldChunkClipboard::clear()
{
    content = {};
    hasContent = false;
    mirrorHorizontal_ = false;
    mirrorVertical_ = false;
    rotationQuarterTurns_ = 0;
}

void WorldChunkClipboard::rotateClockwise()
{
    rotationQuarterTurns_ = (rotationQuarterTurns_ + 1) % 4;
}

void WorldChunkClipboard::rotateCounterClockwise()
{
    rotationQuarterTurns_ = (rotationQuarterTurns_ + 3) % 4;
}

std::vector<std::uint8_t> WorldChunkClipboard::encodeHeights(const std::vector<float>& values)
{
    std::vector<std::uint8_t> result;
    for (std::size_t i = 0; i < values.size();)
    {
        std::uint8_t run = 1;
        while (i + run < values.size() && run < 255 && values[i + run] == values[i])
            ++run;
        result.push_back(run);
        const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(&values[i]);
        result.insert(result.end(), bytes, bytes + sizeof(float));
        i += run;
    }
    return result;
}

std::vector<float> WorldChunkClipboard::decodeHeights(const std::vector<std::uint8_t>& bytes)
{
    std::vector<float> result;
    for (std::size_t cursor = 0; cursor + sizeof(float) < bytes.size();)
    {
        const std::uint8_t run = bytes[cursor++];
        float value = 0.0f;
        std::memcpy(&value, bytes.data() + cursor, sizeof(float));
        cursor += sizeof(float);
        result.insert(result.end(), run, value);
    }
    return result;
}

void WorldChunkClipboard::applyPaste(TerrainChunk& target, const glm::vec2& position, PasteMode mode) const
{
    if (content.dimensions.x <= 0 || content.dimensions.y <= 0)
        return;
    const float scale = target.heightmap.getScale();
    const glm::ivec2 start(static_cast<int>(std::round(position.x / scale)) - content.dimensions.x / 2,
                           static_cast<int>(std::round(position.y / scale)) - content.dimensions.y / 2);
    const std::vector<float> heights = content.heights.empty() ? decodeHeights(content.heightmapDelta) : content.heights;
    for (int y = 0; y < content.dimensions.y; ++y)
        for (int x = 0; x < content.dimensions.x; ++x)
        {
            const glm::ivec2 local = transformedSourceCoordinate({x, y});
            const int targetX = start.x + x;
            const int targetY = start.y + y;
            if (targetX < 0 || targetY < 0 || targetX >= static_cast<int>(target.heightmap.getWidth()) ||
                targetY >= static_cast<int>(target.heightmap.getHeightCount()))
                continue;
            const std::size_t sourceIndex = static_cast<std::size_t>(local.y) * content.dimensions.x + local.x;
            bool acceptedHeight = true;
            if (sourceIndex < heights.size())
            {
                const float existing = target.heightmap.getHeight(static_cast<std::uint32_t>(targetX), static_cast<std::uint32_t>(targetY));
                const float incoming = heights[sourceIndex];
                float result = incoming;
                if (mode == PasteMode::Merge)
                    result = glm::mix(existing, incoming, 0.5f);
                else if (mode == PasteMode::Additive)
                    result = existing + incoming;
                else if (mode == PasteMode::HeightBased && incoming <= existing)
                {
                    result = existing;
                    acceptedHeight = false;
                }
                target.heightmap.setHeight(static_cast<std::uint32_t>(targetX), static_cast<std::uint32_t>(targetY), result);
            }
            if (sourceIndex < content.splats.size() && (mode != PasteMode::HeightBased || acceptedHeight))
                target.splatmap.setTexel(static_cast<std::uint32_t>(targetX), static_cast<std::uint32_t>(targetY), content.splats[sourceIndex]);
        }

    std::uint64_t doodadId = NextDoodadId(target.doodads);
    for (const Doodad& source : content.doodads)
    {
        Doodad copy = source;
        if (content.relativePositioning)
            copy.position += glm::vec3(position.x, 0.0f, position.y);
        copy.uniqueId = doodadId++;
        copy.recalculateBounds();
        target.doodads.push_back(std::move(copy));
    }
    std::uint64_t creatureId = NextCreatureId(target.creatures);
    for (const CreatureSpawner& source : content.creatures)
    {
        CreatureSpawner copy = source;
        if (content.relativePositioning)
        {
            const glm::vec3 offset(position.x, 0.0f, position.y);
            copy.position += offset;
            for (const Waypoint& waypoint : copy.waypointSystem.points())
                copy.waypointSystem.move(waypoint.id, waypoint.position + offset);
        }
        copy.uniqueId = creatureId++;
        target.creatures.push_back(std::move(copy));
    }
    for (const glm::vec3& cell : content.waterCells)
    {
        const int x = start.x + static_cast<int>(cell.x);
        const int y = start.y + static_cast<int>(cell.z);
        if (x >= 0 && y >= 0 && x < static_cast<int>(target.heightmap.getWidth()) && y < static_cast<int>(target.heightmap.getHeightCount()))
        {
            target.water.editLiquidCell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), true);
            target.water.setCellVariation(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), cell.y - target.water.getGlobalHeight());
        }
    }
}

glm::ivec2 WorldChunkClipboard::transformedSourceCoordinate(glm::ivec2 coordinate) const
{
    glm::ivec2 result = coordinate;
    const int width = content.dimensions.x;
    const int height = content.dimensions.y;
    if (mirrorHorizontal_)
        result.x = width - 1 - result.x;
    if (mirrorVertical_)
        result.y = height - 1 - result.y;
    // The clipboard UI constrains rotation to square selections; rectangular content
    // remains valid and uses a bounded rotation around its center.
    for (int turn = 0; turn < rotationQuarterTurns_; ++turn)
        result = {std::min(width - 1, result.y), std::min(height - 1, width - 1 - result.x)};
    result.x = glm::clamp(result.x, 0, width - 1);
    result.y = glm::clamp(result.y, 0, height - 1);
    return result;
}
} // namespace wowedit
