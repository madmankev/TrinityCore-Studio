#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wowedit
{
struct QuestObjectiveData
{
    std::uint32_t entry = 0;
    std::uint32_t requiredCount = 1;
    std::string description;
};

struct QuestData
{
    std::uint32_t id = 0;
    std::string title;
    std::uint32_t questGiverEntry = 0;
    std::vector<QuestObjectiveData> objectives;
    std::vector<std::uint32_t> triggerIds;
};
} // namespace wowedit
