#pragma once
#include "data/quest_data.h"
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
namespace wowedit
{
enum class QuestTriggerType { Area, Proximity, Interaction, Timer };
struct QuestTriggerVolume { std::uint64_t id = 0; QuestTriggerType type = QuestTriggerType::Area; glm::vec3 center{0.0f}; glm::vec3 extents{5.0f}; float delaySeconds = 0.0f; std::string scriptHook; };
class QuestIntegrationPanel
{
public:
    std::uint64_t addTrigger(QuestTriggerVolume trigger); bool linkQuest(std::uint32_t questId, std::uint32_t npcEntry);
    const std::vector<QuestTriggerVolume>& triggers() const { return triggers_; }
private:
    std::vector<QuestTriggerVolume> triggers_; std::uint64_t nextId_ = 1;
};
} // namespace wowedit
