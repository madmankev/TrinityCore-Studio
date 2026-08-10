#include "editing/transform_tool.h"
#include "objects/doodad_manager.h"
namespace wowedit
{
void TransformTool::moveSelection(glm::vec3 delta)
{
    for (std::uint64_t id : doodads_.selection())
        if (const Doodad* doodad = doodads_.find(id))
            doodads_.moveDoodad(id, gizmo_.snapTranslation(doodad->position + delta));
}
void TransformTool::rotateSelection(glm::vec3 deltaDegrees)
{
    for (std::uint64_t id : doodads_.selection())
        if (const Doodad* doodad = doodads_.find(id))
            doodads_.transformDoodad(id, gizmo_.snapRotation(doodad->rotation + deltaDegrees), doodad->scale);
}
void TransformTool::scaleSelection(glm::vec3 factor, bool uniform)
{
    for (std::uint64_t id : doodads_.selection())
        if (const Doodad* doodad = doodads_.find(id))
            doodads_.transformDoodad(id, doodad->rotation, gizmo_.constrainScale(doodad->scale * factor, uniform));
}
} // namespace wowedit
