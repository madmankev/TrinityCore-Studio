#pragma once

#include <array>

#include <glm/glm.hpp>

namespace wowedit
{
enum class TransformMode { Select, Move, Rotate, Scale, Universal };
enum class TransformPivot { Center, Origin, SelectionBounds };

struct TransformSnapSettings
{
    bool snapToGrid = false;
    float gridSize = 1.0f;
    bool snapToGround = false;
    bool snapToObjects = false;
    bool angleSnap = false;
    float angleIncrement = 15.0f;
    TransformPivot pivot = TransformPivot::Center;
};

/** Backend-agnostic transform gizmo policy; ImGuizmo/Qt/etc. drives its handles. */
class TransformGizmo
{
public:
    void setMode(TransformMode mode) { mode_ = mode; }
    TransformMode mode() const { return mode_; }
    TransformSnapSettings& settings() { return settings_; }
    const TransformSnapSettings& settings() const { return settings_; }

    glm::vec3 snapTranslation(glm::vec3 value) const;
    glm::vec3 snapRotation(glm::vec3 degrees) const;
    glm::vec3 constrainScale(glm::vec3 scale, bool uniform) const;

private:
    TransformMode mode_ = TransformMode::Select;
    TransformSnapSettings settings_;
};
} // namespace wowedit
