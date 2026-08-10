#pragma once
#include "objects/transform_gizmo.h"
#include <glm/glm.hpp>
namespace wowedit
{
class DoodadManager;
class TransformTool
{
public:
    explicit TransformTool(DoodadManager& doodads) : doodads_(doodads) {}
    TransformGizmo& gizmo() { return gizmo_; }
    void moveSelection(glm::vec3 delta);
    void rotateSelection(glm::vec3 deltaDegrees);
    void scaleSelection(glm::vec3 factor, bool uniform);
private:
    DoodadManager& doodads_;
    TransformGizmo gizmo_;
};
} // namespace wowedit
