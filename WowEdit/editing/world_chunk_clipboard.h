#pragma once

#include "core/command.h"
#include "objects/doodad.h"
#include "creatures/creature_spawner.h"
#include "terrain/terrain_chunk.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace wowedit
{
/** Rectangle copy/paste controller for terrain, paint, objects, creatures, and water.
 * Coordinates are local X/Z world units of the configured source/target chunks. */
class WorldChunkClipboard
{
public:
    struct ClipboardContent
    {
        std::vector<std::uint8_t> heightmapDelta; // RLE float stream; decoded values retained below for preview/paste
        std::vector<std::uint8_t> splatmapData;
        std::vector<Doodad> doodads;
        std::vector<CreatureSpawner> creatures;
        std::vector<glm::vec3> waterCells;
        glm::ivec2 sourceOrigin{0};
        glm::ivec2 dimensions{0};
        glm::vec3 sourceCenterPoint{0.0f};
        std::vector<float> heights;
        std::vector<glm::u8vec4> splats;
        bool relativePositioning = true;
    };

    struct CopyOptions
    {
        bool includeTerrain = true;
        bool includeTextures = true;
        bool includeDoodads = true;
        bool includeCreatures = true;
        bool includeWater = true;
        bool relativePositioning = true;
    };

    enum class PasteMode { Overwrite, Merge, Additive, HeightBased };

    explicit WorldChunkClipboard(CommandManager* commands = nullptr) : commands_(commands) {}
    void setSource(TerrainChunk* source) { source_ = source; }
    void setTarget(TerrainChunk* target) { target_ = target; }

    void copyArea(glm::vec2 min, glm::vec2 max, CopyOptions options);
    void pasteAt(glm::vec2 position, PasteMode mode);
    void clear();
    bool hasData() const { return hasContent; }
    const ClipboardContent& preview() const { return content; }

    void setMirrorHorizontal(bool value) { mirrorHorizontal_ = value; }
    void setMirrorVertical(bool value) { mirrorVertical_ = value; }
    void rotateClockwise();
    void rotateCounterClockwise();

private:
    static std::vector<std::uint8_t> encodeHeights(const std::vector<float>& values);
    static std::vector<float> decodeHeights(const std::vector<std::uint8_t>& bytes);
    void applyPaste(TerrainChunk& target, const glm::vec2& position, PasteMode mode) const;
    glm::ivec2 transformedSourceCoordinate(glm::ivec2 coordinate) const;

    ClipboardContent content;
    bool hasContent = false;
    TerrainChunk* source_ = nullptr;
    TerrainChunk* target_ = nullptr;
    CommandManager* commands_ = nullptr;
    bool mirrorHorizontal_ = false;
    bool mirrorVertical_ = false;
    int rotationQuarterTurns_ = 0;
};
} // namespace wowedit
