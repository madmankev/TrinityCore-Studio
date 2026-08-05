#pragma once

// ViewportCamera — a shared orbit/fly camera for the model + WMO + ADT viewports. Z-up (WoW).
// Orbit: spherical around a framed center (drag rotates, wheel zooms). Fly: free-look
// (drag looks, WASD/QE moves, wheel adjusts speed, Shift boosts). Both modes share one
// yaw/pitch "forward" so switching modes is continuous (eye + view direction preserved).
//
// This is an EDITOR helper: it reads ImGui input (ImGuiIO / IsKeyDown) and draws an ImGui
// control, so it lives on the ImGui side of the engine/editor split. The pure frustum-cull
// math it used to carry now lives in viewer/Frustum.h (engine-safe); this header re-exports it
// for the viewers that included ViewportCamera.h for both.

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"

#include "viewer/Frustum.h"   // SphereInFrustum / Frustum (re-exported for existing consumers)

namespace we
{
class ViewportCamera
{
public:
    enum class Mode { Orbit, Fly };

    // Frame the camera on a bounding sphere (called on model load / reset).
    void Frame(const glm::vec3& center, float radius)
    {
        center_ = center;
        radius_ = std::max(radius, 0.01f);
        dist_ = radius_ * 2.4f;
        yaw_ = 0.7f;
        pitch_ = 0.25f;
        moveSpeed_ = radius_ * 1.2f;
        flyPos_ = center_ - Forward() * dist_;
    }

    // Advance from this frame's input. `hovered`/`active` come from the viewport
    // InvisibleButton (active == a mouse button is held on it).
    void Update(bool hovered, bool active, float dt)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (mode_ == Mode::Orbit)
        {
            if (hovered && io.MouseWheel != 0.0f)
                dist_ = std::clamp(dist_ * (1.0f - io.MouseWheel * 0.1f), radius_ * 0.01f, radius_ * 60.0f);
            if (active && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
            {
                yaw_ -= io.MouseDelta.x * 0.01f;
                pitch_ = std::clamp(pitch_ + io.MouseDelta.y * 0.01f, -1.5f, 1.5f);
            }
        }
        else   // Fly
        {
            if (active && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
            {
                yaw_ -= io.MouseDelta.x * 0.005f;
                pitch_ = std::clamp(pitch_ - io.MouseDelta.y * 0.005f, -1.5f, 1.5f);
            }
            if (hovered && io.MouseWheel != 0.0f)
                moveSpeed_ = std::clamp(moveSpeed_ * (1.0f + io.MouseWheel * 0.1f), radius_ * 0.01f, radius_ * 50.0f);
            if ((hovered || active) && !io.WantTextInput)
            {
                const glm::vec3 fwd = Forward();
                const glm::vec3 up(0.0f, 0.0f, 1.0f);
                glm::vec3 right = glm::cross(fwd, up);
                float rl = glm::length(right);
                right = rl > 1e-5f ? right / rl : glm::vec3(1.0f, 0.0f, 0.0f);
                float v = moveSpeed_ * dt * (io.KeyShift ? 5.0f : 1.0f);
                if (ImGui::IsKeyDown(ImGuiKey_W)) flyPos_ += fwd * v;
                if (ImGui::IsKeyDown(ImGuiKey_S)) flyPos_ -= fwd * v;
                if (ImGui::IsKeyDown(ImGuiKey_D)) flyPos_ += right * v;
                if (ImGui::IsKeyDown(ImGuiKey_A)) flyPos_ -= right * v;
                if (ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_Space)) flyPos_ += up * v;
                if (ImGui::IsKeyDown(ImGuiKey_Q) || ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) flyPos_ -= up * v;
            }
        }
    }

    glm::vec3 Eye() const { return mode_ == Mode::Orbit ? (center_ - Forward() * dist_) : flyPos_; }

    glm::mat4 View() const
    {
        const glm::vec3 eye = Eye();
        const glm::vec3 target = mode_ == Mode::Orbit ? center_ : (flyPos_ + Forward());
        return glm::lookAt(eye, target, glm::vec3(0.0f, 0.0f, 1.0f));
    }

    // A perspective projection with Vulkan clip space (Y flipped) for this camera + aspect.
    // Fly mode keeps a small near plane (clamped) so you can move right up to surfaces.
    glm::mat4 Proj(float aspect) const
    {
        float nearP = mode_ == Mode::Orbit ? std::max(dist_ * 0.02f, 0.01f)
                                           : std::clamp(radius_ * 0.001f, 0.02f, 0.5f);
        float farP = mode_ == Mode::Orbit ? (dist_ * 4.0f + radius_ * 6.0f) : (radius_ * 40.0f);
        glm::mat4 p = glm::perspective(glm::radians(45.0f), aspect, nearP, farP);
        p[1][1] *= -1.0f;
        return p;
    }

    // A small ImGui control: a mode combo, plus a Speed slider in Fly mode (the mouse wheel
    // also adjusts speed while flying). Returns true if the mode changed.
    bool DrawControls()
    {
        int m = static_cast<int>(mode_);
        bool changed = false;
        ImGui::SetNextItemWidth(80);
        if (ImGui::Combo("##cammode", &m, "Orbit\0Fly\0") && static_cast<Mode>(m) != mode_)
        {
            SetMode(static_cast<Mode>(m));
            changed = true;
        }
        if (mode_ == Mode::Fly)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130);
            ImGui::SliderFloat("Speed", &moveSpeed_, radius_ * 0.02f, radius_ * 20.0f, "%.2f",
                               ImGuiSliderFlags_Logarithmic);
        }
        return changed;
    }

    Mode mode() const { return mode_; }
    const glm::vec3& center() const { return center_; }
    float radius() const { return radius_; }

private:
    glm::vec3 Forward() const
    {
        return glm::vec3(std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_));
    }
    void SetMode(Mode m)   // preserve eye + view direction across the switch
    {
        if (m == mode_)
            return;
        if (m == Mode::Fly)
            flyPos_ = center_ - Forward() * dist_;   // = current orbit eye
        else
            center_ = flyPos_ + Forward() * dist_;   // orbit around a point ahead
        mode_ = m;
    }

    Mode mode_ = Mode::Orbit;
    glm::vec3 center_{0.0f};
    float radius_ = 1.0f;
    float dist_ = 3.0f;
    float yaw_ = 0.7f, pitch_ = 0.25f;
    glm::vec3 flyPos_{0.0f};
    float moveSpeed_ = 1.0f;
};
} // namespace we
