#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wowedit
{
using Faction = std::uint32_t;
using NPCFlags = std::uint32_t;

enum NPCFlag : NPCFlags
{
    NpcFlagNone = 0,
    NpcFlagGossip = 1u << 0u,
    NpcFlagQuestGiver = 1u << 1u,
    NpcFlagVendor = 1u << 2u,
    NpcFlagTrainer = 1u << 3u,
    NpcFlagFlightMaster = 1u << 4u,
    NpcFlagBanker = 1u << 5u,
    NpcFlagHostile = 1u << 6u,
    NpcFlagFriendly = 1u << 7u,
};

struct CreatureData
{
    std::uint32_t templateId = 0;
    std::string name;
    Faction factionId = 0;
    std::uint32_t displayId = 0;
    std::string modelPath;
    std::vector<std::string> tags;
};
} // namespace wowedit
