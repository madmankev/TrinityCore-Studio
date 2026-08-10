#pragma once
#include "terrain/terrain_editor.h"
namespace wowedit
{
class TerrainToolController
{
public:
    explicit TerrainToolController(TerrainEditor& editor) : editor_(editor) {}
    void setActive(TerrainTool tool) { active_ = tool; }
    TerrainTool active() const { return active_; }
    void stroke(TerrainChunk& chunk, const glm::vec3& worldPosition) { editor_.applyStroke(chunk, active_, worldPosition); }
private:
    TerrainEditor& editor_;
    TerrainTool active_ = TerrainTool::Raise;
};
} // namespace wowedit
