#pragma once

#include "editing/road_path_tool.h"

#include <cstddef>
#include <vector>

namespace wowedit
{
/** Presentation-model controller for a Road / Path authoring panel. A host renders
 * its points/settings/preview while this class keeps document mutations undoable. */
class RoadPathPanel
{
public:
    explicit RoadPathPanel(RoadPathTool& tool) : tool_(tool) {}

    RoadPathSettings& settings() { return tool_.settings(); }
    const RoadPathSettings& settings() const { return tool_.settings(); }
    const std::vector<glm::vec3>& points() const { return points_; }
    const RoadBuildResult& lastBuild() const { return lastBuild_; }

    void addPoint(glm::vec3 point) { points_.push_back(point); }
    bool movePoint(std::size_t index, glm::vec3 point);
    bool removePoint(std::size_t index);
    void clear() { points_.clear(); lastBuild_ = {}; }
    void snapToTerrain(const TerrainChunk& chunk);
    RoadBuildResult build(TerrainChunk& chunk);
    Mesh preview(const TerrainChunk& chunk) const;

private:
    RoadPathTool& tool_;
    std::vector<glm::vec3> points_;
    RoadBuildResult lastBuild_;
};
} // namespace wowedit
