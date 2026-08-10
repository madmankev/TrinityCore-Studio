#include "ui/quest_integration_panel.h"

#include <algorithm>
namespace wowedit
{
std::uint64_t QuestIntegrationPanel::addTrigger(QuestTriggerVolume trigger) { if (!trigger.id) trigger.id = nextId_++; nextId_ = std::max(nextId_, trigger.id + 1); triggers_.push_back(std::move(trigger)); return triggers_.back().id; }
bool QuestIntegrationPanel::linkQuest(std::uint32_t questId, std::uint32_t npcEntry) { return questId != 0 && npcEntry != 0; }
} // namespace wowedit
