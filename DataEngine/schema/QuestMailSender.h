#pragma once

// Mirror of the `quest_mail_sender` table (CREATE TABLE at line 2599).
// PK `QuestId`. Row is optional; `present` is false when absent.

#include <cstdint>

namespace we
{
struct QuestMailSender
{
    uint32_t questId = 0;                   // `QuestId`               int unsigned
    uint32_t rewardMailSenderEntry = 0;     // `RewardMailSenderEntry` int unsigned

    bool present = false;
};
} // namespace we
