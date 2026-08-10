#pragma once
#include "editing/brush_tool.h"
#include "objects/transform_gizmo.h"
#include <functional>
namespace wowedit
{
enum class ActiveTool { Select, Move, Rotate, Scale, TerrainRaise, TerrainLower, TerrainSmooth, TerrainFlatten, TexturePaint, VertexPaint, PlaceDoodad, PlaceCreature };
class Toolbar
{
public:
    void setActiveTool(ActiveTool tool); ActiveTool activeTool() const { return active_; }
    BrushTool& brush() { return brush_; } TransformGizmo& transform() { return transform_; }
    void setChangedCallback(std::function<void(ActiveTool)> callback) { changed_ = std::move(callback); }
private:
    ActiveTool active_ = ActiveTool::Select; BrushTool brush_; TransformGizmo transform_; std::function<void(ActiveTool)> changed_;
};
} // namespace wowedit
