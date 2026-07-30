#pragma once

// Aggregates every localized string for a single quest in one locale, gathered
// across the four *_locale tables:
//   quest_template_locale       (line 2904)
//   quest_offer_reward_locale   (line 2636)
//   quest_request_items_locale  (line 2741)
//   quest_greeting_locale       (line 2582) - keyed additionally by Type (creature/GO)
// One QuestLocale corresponds to one `locale` code (see qe::kLocales); the base
// enUS text lives in the main tables, not here. Empty strings mean "no localized
// value for that column in this locale".

#include <array>
#include <cstdint>
#include <string>

namespace qe
{
struct QuestLocale
{
    std::string locale;                     // `locale`  varchar(4) (koKR, frFR, ...)

    // quest_template_locale
    std::string title;                      // `Title`
    std::string details;                    // `Details`
    std::string objectives;                 // `Objectives`
    std::string endText;                    // `EndText`
    std::string completedText;              // `CompletedText`
    std::array<std::string, 4> objectiveText; // `ObjectiveText1..4`

    // quest_offer_reward_locale
    std::string rewardText;                 // `RewardText`

    // quest_request_items_locale
    std::string completionText;             // `CompletionText`

    // quest_greeting_locale (`Greeting`), split by Type.
    std::string greetingCreature;           // Type 0 greeting
    std::string greetingGameObject;         // Type 1 greeting

    // Per-source presence: which locale rows actually exist for this quest.
    bool templatePresent = false;
    bool offerRewardPresent = false;
    bool requestItemsPresent = false;
    bool greetingCreaturePresent = false;
    bool greetingGameObjectPresent = false;
};
} // namespace qe
