#pragma once

// Mirrors the three per-quest text/emote tables:
//   quest_offer_reward   (line 2613)  - turn-in text + 4 emotes
//   quest_request_items  (line 2724)  - requested-items text + complete/incomplete emote
//   quest_details        (line 2542)  - quest-giver gossip emotes (4)
// Each row is optional; `present` reflects whether the DB row exists.

#include <array>
#include <cstdint>
#include <string>

namespace qe
{
struct QuestOfferReward
{
    uint32_t id = 0;                        // `ID`  int unsigned
    std::array<uint16_t, 4> emote{};        // `Emote1..4`       smallint unsigned
    std::array<uint32_t, 4> emoteDelay{};   // `EmoteDelay1..4`  int unsigned
    std::string rewardText;                 // `RewardText`      mediumtext
    int32_t  verifiedBuild = 0;             // `VerifiedBuild`   int  DEFAULT 0

    bool present = false;
};

struct QuestRequestItems
{
    uint32_t id = 0;                        // `ID`  int unsigned
    uint16_t emoteOnComplete = 0;           // `EmoteOnComplete`   smallint unsigned
    uint16_t emoteOnIncomplete = 0;         // `EmoteOnIncomplete` smallint unsigned
    std::string completionText;             // `CompletionText`    mediumtext
    int32_t  verifiedBuild = 0;             // `VerifiedBuild`     int  DEFAULT 0

    bool present = false;
};

struct QuestDetails
{
    uint32_t id = 0;                        // `ID`  int unsigned
    std::array<uint16_t, 4> emote{};        // `Emote1..4`       smallint unsigned
    std::array<uint32_t, 4> emoteDelay{};   // `EmoteDelay1..4`  int unsigned
    int32_t  verifiedBuild = 0;             // `VerifiedBuild`   int  DEFAULT 0

    bool present = false;
};
} // namespace qe
