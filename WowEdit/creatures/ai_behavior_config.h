#pragma once

#include "creatures/waypoint_system.h"

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace wowedit
{
enum class PatrolPattern : std::uint8_t { Loop, PingPong, Once };
enum class AiPreviewState : std::uint8_t { Idle, Patrol, Chasing, Returning };

struct AiBehaviorConfig
{
    float aggroRadius = 20.0f;
    float leashRadius = 50.0f;
    PatrolPattern patrolPattern = PatrolPattern::Loop;
    bool aggroEnabled = true;
    float moveSpeed = 4.5f;
    float chaseSpeed = 7.0f;
};

struct AiPreviewAgent
{
    glm::vec3 homePosition{0.0f};
    glm::vec3 position{0.0f};
    std::size_t waypointIndex = 0;
    bool reverseDirection = false;
    AiPreviewState state = AiPreviewState::Idle;
};

/** Deterministic in-editor patrol/chase simulation, not a replacement for core AI. */
class AiBehaviorSimulator
{
public:
    void update(AiPreviewAgent& agent, const AiBehaviorConfig& config, const WaypointSystem& route,
                const glm::vec3& previewTarget, float deltaSeconds) const;
    static const char* stateName(AiPreviewState state);

private:
    static glm::vec3 moveToward(glm::vec3 current, const glm::vec3& target, float maxDistance);
};
} // namespace wowedit
