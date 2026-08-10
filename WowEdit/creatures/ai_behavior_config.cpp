#include "creatures/ai_behavior_config.h"

#include <algorithm>
#include <cmath>

namespace wowedit
{
glm::vec3 AiBehaviorSimulator::moveToward(glm::vec3 current, const glm::vec3& target, float maxDistance)
{
    const glm::vec3 difference = target - current;
    const float distance = glm::length(difference);
    return distance <= maxDistance || distance < 1e-5f ? target : current + difference / distance * maxDistance;
}

void AiBehaviorSimulator::update(AiPreviewAgent& agent, const AiBehaviorConfig& config, const WaypointSystem& route,
                                 const glm::vec3& previewTarget, float deltaSeconds) const
{
    const float dt = std::max(0.0f, deltaSeconds);
    const float targetDistance = glm::length(previewTarget - agent.position);
    const float homeDistance = glm::length(agent.position - agent.homePosition);
    if (config.aggroEnabled && targetDistance <= config.aggroRadius && homeDistance <= config.leashRadius)
        agent.state = AiPreviewState::Chasing;
    else if (agent.state == AiPreviewState::Chasing && homeDistance > config.leashRadius)
        agent.state = AiPreviewState::Returning;

    if (agent.state == AiPreviewState::Chasing)
    {
        agent.position = moveToward(agent.position, previewTarget, config.chaseSpeed * dt);
        return;
    }
    if (agent.state == AiPreviewState::Returning)
    {
        agent.position = moveToward(agent.position, agent.homePosition, config.moveSpeed * dt);
        if (glm::length(agent.position - agent.homePosition) < 0.01f)
            agent.state = route.points().empty() ? AiPreviewState::Idle : AiPreviewState::Patrol;
        return;
    }
    if (route.points().empty())
    {
        agent.state = AiPreviewState::Idle;
        return;
    }

    agent.state = AiPreviewState::Patrol;
    const std::vector<Waypoint>& points = route.points();
    agent.waypointIndex = std::min(agent.waypointIndex, points.size() - 1);
    const glm::vec3 destination = points[agent.waypointIndex].position;
    agent.position = moveToward(agent.position, destination, config.moveSpeed * dt);
    if (glm::length(agent.position - destination) >= 0.01f)
        return;

    if (config.patrolPattern == PatrolPattern::Loop)
        agent.waypointIndex = (agent.waypointIndex + 1) % points.size();
    else if (config.patrolPattern == PatrolPattern::Once)
        agent.waypointIndex = std::min(agent.waypointIndex + 1, points.size() - 1);
    else
    {
        if (points.size() == 1)
            return;
        if (!agent.reverseDirection && agent.waypointIndex + 1 >= points.size())
            agent.reverseDirection = true;
        else if (agent.reverseDirection && agent.waypointIndex == 0)
            agent.reverseDirection = false;
        agent.waypointIndex = agent.reverseDirection ? agent.waypointIndex - 1 : agent.waypointIndex + 1;
    }
}

const char* AiBehaviorSimulator::stateName(AiPreviewState state)
{
    switch (state)
    {
    case AiPreviewState::Idle: return "IDLE";
    case AiPreviewState::Patrol: return "PATROL";
    case AiPreviewState::Chasing: return "CHASING";
    case AiPreviewState::Returning: return "RETURNING";
    }
    return "UNKNOWN";
}
} // namespace wowedit
