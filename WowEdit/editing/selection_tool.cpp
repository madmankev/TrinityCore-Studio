#include "editing/selection_tool.h"
#include "objects/doodad_manager.h"
#include "objects/object_selector.h"
namespace wowedit
{
std::uint64_t SelectionTool::click(const Ray& ray, bool additive)
{
    ObjectSelector selector(doodads_);
    const std::uint64_t id = selector.pick(ray);
    std::set<std::uint64_t> selected = additive ? doodads_.selection() : std::set<std::uint64_t>{};
    if (id) selected.insert(id);
    doodads_.setSelection(selected);
    return id;
}
void SelectionTool::marquee(const AABB& bounds, bool additive)
{
    ObjectSelector selector(doodads_);
    std::set<std::uint64_t> selected = additive ? doodads_.selection() : std::set<std::uint64_t>{};
    const std::set<std::uint64_t> picked = selector.pickBox(bounds);
    selected.insert(picked.begin(), picked.end());
    doodads_.setSelection(selected);
}
void SelectionTool::clear() { doodads_.clearSelection(); }
} // namespace wowedit
