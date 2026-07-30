#pragma once

// Layer A aggregate: everything the editor knows about a single quest ID,
// owning one row (or row set) from every related table. See docs/SPEC.md §6.
//
// Sub-part optionality is carried on the sub-structs themselves via `present`
// (addon, offerReward, requestItems, details, mailSender) or by container
// emptiness (greetings, questgiver lists, pois, locales). Per-part `dirty`
// flags let a later save write only what changed.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "QuestCondition.h"
#include "QuestGreeting.h"
#include "QuestLocale.h"
#include "QuestMailSender.h"
#include "QuestPoi.h"
#include "QuestTemplate.h"
#include "QuestTemplateAddon.h"
#include "QuestText.h"

namespace qe
{
struct Quest
{
    QuestTemplate      tmpl;                 // quest_template (always present)
    QuestTemplateAddon addon;                // quest_template_addon (addon.present)
    QuestOfferReward   offerReward;          // quest_offer_reward (.present)
    QuestRequestItems  requestItems;         // quest_request_items (.present)
    QuestDetails       details;              // quest_details (.present)
    QuestMailSender    mailSender;           // quest_mail_sender (.present)

    std::vector<QuestGreeting> greetings;    // quest_greeting, by Type

    // Questgiver links (creature_/gameobject_ queststarter/questender). Each
    // vector holds the linked entry ids.
    std::vector<uint32_t> creatureStarters;
    std::vector<uint32_t> creatureEnders;
    std::vector<uint32_t> goStarters;
    std::vector<uint32_t> goEnders;

    std::vector<QuestPoi> pois;              // quest_poi (+ owned quest_poi_points)

    std::map<std::string, QuestLocale> locales; // key = locale code (see kLocales)

    // conditions rows gating this quest (SourceType 19 = quest available).
    std::vector<QuestCondition> conditions;

    // --- Dirty tracking ---------------------------------------------------
    // Set by editor UI when a sub-part is modified so save writes only deltas.
    bool tmplDirty = false;
    bool addonDirty = false;
    bool offerRewardDirty = false;
    bool requestItemsDirty = false;
    bool detailsDirty = false;
    bool mailSenderDirty = false;
    bool greetingsDirty = false;
    bool questgiversDirty = false;
    bool poisDirty = false;
    bool localesDirty = false;
    bool conditionsDirty = false;

    // True when this quest was newly created in the editor and has no DB rows yet.
    bool isNew = false;

    bool AnyDirty() const
    {
        return tmplDirty || addonDirty || offerRewardDirty || requestItemsDirty ||
               detailsDirty || mailSenderDirty || greetingsDirty || questgiversDirty ||
               poisDirty || localesDirty || conditionsDirty;
    }

    void ClearDirty()
    {
        tmplDirty = addonDirty = offerRewardDirty = requestItemsDirty = detailsDirty =
            mailSenderDirty = greetingsDirty = questgiversDirty = poisDirty =
                localesDirty = conditionsDirty = false;
    }
};
} // namespace qe
