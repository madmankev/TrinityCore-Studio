#include "editors/quest/QuestValidator.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "data/LookupCache.h"

namespace we
{
namespace
{
// TrinityCore 3.3.5a QUEST_FLAGS_* bits (verified against Enums.cpp QuestFlagsBits()).
constexpr uint32_t QUEST_FLAGS_DAILY  = 0x00001000;
constexpr uint32_t QUEST_FLAGS_WEEKLY = 0x00008000;

std::string U(uint32_t v) { return std::to_string(v); }
std::string I(int32_t v) { return std::to_string(v); }

class Collector
{
public:
    explicit Collector(std::vector<ValidationIssue>& out) : out_(out) {}

    void Add(Severity sev, std::string tab, std::string field, std::string msg)
    {
        out_.push_back({sev, std::move(tab), std::move(field), std::move(msg)});
    }

private:
    std::vector<ValidationIssue>& out_;
};

// True when any nonzero reward item, choice item or reward spell is granted.
bool HasRewardItemsOrSpell(const QuestTemplate& t)
{
    for (uint32_t id : t.rewardItemId)
        if (id != 0)
            return true;
    for (uint32_t id : t.rewardChoiceItemId)
        if (id != 0)
            return true;
    return t.rewardSpell != 0 || t.rewardDisplaySpell != 0;
}

bool HasNpcOrGoObjectives(const QuestTemplate& t)
{
    for (int32_t v : t.requiredNpcOrGo)
        if (v != 0)
            return true;
    return false;
}

bool HasRequiredItems(const QuestTemplate& t)
{
    for (uint32_t id : t.requiredItemId)
        if (id != 0)
            return true;
    return false;
}
} // namespace

std::vector<ValidationIssue> QuestValidator::Validate(const Quest& q, const LookupCache& cache) const
{
    std::vector<ValidationIssue> issues;
    Collector add(issues);

    const QuestTemplate&      t  = q.tmpl;
    const QuestTemplateAddon& ad = q.addon;

    // --- Questgivers -----------------------------------------------------------
    const bool hasStarter = !q.creatureStarters.empty() || !q.goStarters.empty();
    const bool hasEnder   = !q.creatureEnders.empty() || !q.goEnders.empty();
    if (!hasStarter)
        add.Add(Severity::Warning, "Questgivers", "starter",
                "No quest starter (creature/gameobject)");
    if (!hasEnder)
        add.Add(Severity::Warning, "Questgivers", "ender",
                "No quest ender (creature/gameobject)");

    // --- Referenced ids exist (Error, only when the category is loaded) ---------
    if (cache.ItemsLoaded())
    {
        auto checkItem = [&](uint32_t id, const char* field) {
            if (id != 0 && cache.NameOfItem(id).empty())
                add.Add(Severity::Error, "Rewards", field,
                        std::string(field) + ": item " + U(id) + " does not exist");
        };
        for (size_t i = 0; i < t.rewardItemId.size(); ++i)
            checkItem(t.rewardItemId[i], "RewardItem");
        for (size_t i = 0; i < t.rewardChoiceItemId.size(); ++i)
            checkItem(t.rewardChoiceItemId[i], "RewardChoiceItem");
        for (size_t i = 0; i < t.requiredItemId.size(); ++i)
        {
            uint32_t id = t.requiredItemId[i];
            if (id != 0 && cache.NameOfItem(id).empty())
                add.Add(Severity::Error, "Objectives", "RequiredItem",
                        "RequiredItem: item " + U(id) + " does not exist");
        }
        for (size_t i = 0; i < t.itemDrop.size(); ++i)
        {
            uint32_t id = t.itemDrop[i];
            if (id != 0 && cache.NameOfItem(id).empty())
                add.Add(Severity::Error, "Objectives", "ItemDrop",
                        "ItemDrop: item " + U(id) + " does not exist");
        }
        if (t.startItem != 0 && cache.NameOfItem(t.startItem).empty())
            add.Add(Severity::Error, "General", "StartItem",
                    "StartItem: item " + U(t.startItem) + " does not exist");
    }

    // RequiredNpcOrGo: >0 creature, <0 gameobject (negated).
    for (size_t i = 0; i < t.requiredNpcOrGo.size(); ++i)
    {
        int32_t v = t.requiredNpcOrGo[i];
        if (v > 0 && cache.CreaturesLoaded())
        {
            if (cache.NameOfCreature(static_cast<uint32_t>(v)).empty())
                add.Add(Severity::Error, "Objectives", "RequiredNpcOrGo",
                        "RequiredNpcOrGo: creature " + I(v) + " does not exist");
        }
        else if (v < 0 && cache.GameObjectsLoaded())
        {
            uint32_t go = static_cast<uint32_t>(-v);
            if (cache.NameOfGameObject(go).empty())
                add.Add(Severity::Error, "Objectives", "RequiredNpcOrGo",
                        "RequiredNpcOrGo: gameobject " + U(go) + " does not exist");
        }
    }

    // Questgiver starter/ender ids.
    if (cache.CreaturesLoaded())
    {
        auto checkCreatures = [&](const std::vector<uint32_t>& ids, const char* field) {
            for (uint32_t id : ids)
                if (id != 0 && cache.NameOfCreature(id).empty())
                    add.Add(Severity::Error, "Questgivers", field,
                            std::string(field) + ": creature " + U(id) + " does not exist");
        };
        checkCreatures(q.creatureStarters, "CreatureStarter");
        checkCreatures(q.creatureEnders, "CreatureEnder");
    }
    if (cache.GameObjectsLoaded())
    {
        auto checkGos = [&](const std::vector<uint32_t>& ids, const char* field) {
            for (uint32_t id : ids)
                if (id != 0 && cache.NameOfGameObject(id).empty())
                    add.Add(Severity::Error, "Questgivers", field,
                            std::string(field) + ": gameobject " + U(id) + " does not exist");
        };
        checkGos(q.goStarters, "GameObjectStarter");
        checkGos(q.goEnders, "GameObjectEnder");
    }

    // mailSender entry -> creature.
    if (q.mailSender.present && q.mailSender.rewardMailSenderEntry != 0 && cache.CreaturesLoaded())
    {
        uint32_t id = q.mailSender.rewardMailSenderEntry;
        if (cache.NameOfCreature(id).empty())
            add.Add(Severity::Error, "Rewards", "RewardMailSenderEntry",
                    "RewardMailSenderEntry: creature " + U(id) + " does not exist");
    }

    // --- DBC-backed references (Warning; the name maps can be incomplete, so a
    //     miss is "suspicious", not a hard error) ---------------------------------
    if (cache.SpellsLoaded())
    {
        auto checkSpell = [&](uint32_t id, const char* tab, const char* field) {
            if (id != 0 && cache.NameOfSpell(id).empty())
                add.Add(Severity::Warning, tab, field,
                        std::string(field) + ": spell " + U(id) + " not found in Spell.dbc");
        };
        if (t.rewardSpell > 0)
            checkSpell(static_cast<uint32_t>(t.rewardSpell), "Rewards", "RewardSpell");
        checkSpell(t.rewardDisplaySpell, "Rewards", "RewardDisplaySpell");
        if (ad.present)
            checkSpell(ad.sourceSpellID, "Requirements", "SourceSpellID");
    }
    if (cache.FactionsLoaded())
    {
        auto checkFaction = [&](uint32_t id, const char* tab, const char* field) {
            if (id != 0 && cache.NameOfFaction(id).empty())
                add.Add(Severity::Warning, tab, field,
                        std::string(field) + ": faction " + U(id) + " not found in Faction.dbc");
        };
        checkFaction(t.requiredFactionId1, "Requirements", "RequiredFactionId1");
        checkFaction(t.requiredFactionId2, "Requirements", "RequiredFactionId2");
        if (ad.present)
        {
            checkFaction(ad.requiredMinRepFaction, "Requirements", "RequiredMinRepFaction");
            checkFaction(ad.requiredMaxRepFaction, "Requirements", "RequiredMaxRepFaction");
        }
        for (uint32_t id : t.rewardFactionId)
            checkFaction(id, "Rewards", "RewardFactionID");
    }
    if (cache.SkillsLoaded() && ad.present && ad.requiredSkillID != 0 &&
        cache.NameOfSkill(ad.requiredSkillID).empty())
        add.Add(Severity::Warning, "Requirements", "RequiredSkillID",
                "RequiredSkillID: skill " + U(ad.requiredSkillID) + " not found in SkillLine.dbc");
    if (cache.TitlesLoaded() && t.rewardTitle != 0 && cache.NameOfTitle(t.rewardTitle).empty())
        add.Add(Severity::Warning, "Rewards", "RewardTitle",
                "RewardTitle: title " + U(t.rewardTitle) + " not found in CharTitles.dbc");

    // --- Prev/Next quest exist (Error when QuestsLoaded) ------------------------
    if (cache.QuestsLoaded())
    {
        auto checkQuest = [&](uint32_t id, const char* field) {
            if (id != 0 && cache.NameOfQuest(id).empty())
                add.Add(Severity::Error, "Chain", field,
                        std::string(field) + ": quest " + U(id) + " does not exist");
        };
        if (ad.present)
        {
            uint32_t prev = static_cast<uint32_t>(std::abs(ad.prevQuestID));
            checkQuest(prev, "PrevQuestID");
            checkQuest(ad.nextQuestID, "NextQuestID");
            uint32_t bc = static_cast<uint32_t>(std::abs(ad.breadcrumbForQuestId));
            checkQuest(bc, "BreadcrumbForQuestId");
        }
        checkQuest(t.rewardNextQuest, "RewardNextQuest");
    }

    // --- Id/amount pairing (Warning) -------------------------------------------
    auto pair = [&](bool hasId, bool hasAmt, const char* tab, const char* field) {
        if (hasId && !hasAmt)
            add.Add(Severity::Warning, tab, field,
                    std::string(field) + ": id set but count/amount is 0");
        else if (!hasId && hasAmt)
            add.Add(Severity::Warning, tab, field,
                    std::string(field) + ": count/amount set but id is 0");
    };
    for (size_t i = 0; i < t.rewardItemId.size(); ++i)
        pair(t.rewardItemId[i] != 0, t.rewardAmount[i] != 0, "Rewards", "RewardItem");
    for (size_t i = 0; i < t.rewardChoiceItemId.size(); ++i)
        pair(t.rewardChoiceItemId[i] != 0, t.rewardChoiceItemQuantity[i] != 0, "Rewards",
             "RewardChoiceItem");
    for (size_t i = 0; i < t.requiredItemId.size(); ++i)
        pair(t.requiredItemId[i] != 0, t.requiredItemCount[i] != 0, "Objectives", "RequiredItem");
    for (size_t i = 0; i < t.requiredNpcOrGo.size(); ++i)
        pair(t.requiredNpcOrGo[i] != 0, t.requiredNpcOrGoCount[i] != 0, "Objectives",
             "RequiredNpcOrGo");
    for (size_t i = 0; i < t.itemDrop.size(); ++i)
        pair(t.itemDrop[i] != 0, t.itemDropQuantity[i] != 0, "Objectives", "ItemDrop");

    // --- Levels (Warning) ------------------------------------------------------
    if (t.questLevel != -1 && static_cast<int32_t>(t.minLevel) > t.questLevel)
        add.Add(Severity::Warning, "General", "MinLevel",
                "MinLevel (" + U(t.minLevel) + ") is above QuestLevel (" + I(t.questLevel) + ")");
    if (ad.present && ad.maxLevel != 0 && ad.maxLevel < t.minLevel)
        add.Add(Severity::Warning, "General", "MaxLevel",
                "MaxLevel (" + U(ad.maxLevel) + ") is below MinLevel (" + U(t.minLevel) + ")");

    // --- Flags (Warning) -------------------------------------------------------
    if ((t.flags & QUEST_FLAGS_DAILY) && (t.flags & QUEST_FLAGS_WEEKLY))
        add.Add(Severity::Warning, "General", "Flags",
                "Both DAILY and WEEKLY flags are set");

    // --- Text (Warning/Info) ---------------------------------------------------
    if (t.logTitle.empty())
        add.Add(Severity::Warning, "Text", "LogTitle", "LogTitle (quest title) is empty");
    if (t.questDescription.empty())
        add.Add(Severity::Warning, "Text", "QuestDescription", "QuestDescription is empty");
    if ((HasRequiredItems(t) || HasNpcOrGoObjectives(t)) && !q.requestItems.present)
        add.Add(Severity::Info, "Text", "requestItems",
                "Quest has item/npc-or-go objectives but no quest_request_items row (completion text)");

    // --- Rewards (Info) --------------------------------------------------------
    if (t.rewardMoney == 0 && t.rewardBonusMoney == 0 && !HasRewardItemsOrSpell(t))
        add.Add(Severity::Info, "Rewards", "rewards", "Quest grants no rewards");

    // --- Additional sanity checks ----------------------------------------------
    if (t.id == 0)
        add.Add(Severity::Error, "General", "ID", "Quest ID is 0");

    // Stable sort: Error first, then Warning, then Info.
    std::stable_sort(issues.begin(), issues.end(),
                     [](const ValidationIssue& a, const ValidationIssue& b) {
                         return static_cast<int>(a.severity) < static_cast<int>(b.severity);
                     });
    return issues;
}
} // namespace we
