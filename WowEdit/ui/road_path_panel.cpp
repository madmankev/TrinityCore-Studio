#include "ui/road_path_panel.h"

namespace wowedit
{
bool RoadPathPanel::movePoint(std::size_t index, glm::vec3 point)
{
    if (index >= points_.size())
        return false;
    points_[index] = point;
    return true;
}

bool RoadPathPanel::removePoint(std::size_t index)
{
    if (index >= points_.size())
        return false;
    points_.erase(points_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void RoadPathPanel::snapToTerrain(const TerrainChunk& chunk)
{
    tool_.snapControlPoints(chunk.heightmap, points_);
}

RoadBuildResult RoadPathPanel::build(TerrainChunk& chunk)
{
    lastBuild_ = tool_.build(chunk, points_);
    return lastBuild_;
}

Mesh RoadPathPanel::preview(const TerrainChunk& chunk) const
{
    return tool_.buildPreviewMesh(chunk, points_);
}
} // namespace wowedit
