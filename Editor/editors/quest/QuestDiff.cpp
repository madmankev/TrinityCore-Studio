// QuestDiff — see QuestDiff.h.

#include "editors/quest/QuestDiff.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace we
{
namespace
{
std::string U(uint64_t v) { return std::to_string(v); }
std::string I(int64_t v) { return std::to_string(v); }
std::string F(float v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "%g", v);
    return b;
}
std::string Str(const std::string& s)
{
    std::string t = s;
    if (t.size() > 60)
        t = t.substr(0, 57) + "...";
    // single-line
    for (char& c : t)
        if (c == '\n' || c == '\r')
            c = ' ';
    return "\"" + t + "\"";
}

struct Sink
{
    std::vector<FieldDiff>& out;
    void add(const char* tab, const std::string& field, const std::string& o, const std::string& n)
    {
        if (o != n)
            out.push_back({tab, field, o, n});
    }
};
} // namespace

std::vector<FieldDiff> QuestDiff::Compare(const Quest& a, const Quest& b) const
{
    std::vector<FieldDiff> out;
    Sink s{out};

    const QuestTemplate& x = a.tmpl;
    const QuestTemplate& y = b.tmpl;

    // General
    s.add("General", "QuestType", U(x.questType), U(y.questType));
    s.add("General", "QuestLevel", I(x.questLevel), I(y.questLevel));
    s.add("General", "MinLevel", U(x.minLevel), U(y.minLevel));
    s.add("General", "QuestSortID", I(x.questSortID), I(y.questSortID));
    s.add("General", "QuestInfoID", U(x.questInfoID), U(y.questInfoID));
    s.add("General", "SuggestedGroupNum", U(x.suggestedGroupNum), U(y.suggestedGroupNum));
    s.add("General", "TimeAllowed", U(x.timeAllowed), U(y.timeAllowed));
    s.add("General", "Flags", U(x.flags), U(y.flags));
    s.add("General", "AllowableRaces", U(x.allowableRaces), U(y.allowableRaces));
    s.add("General", "RequiredPlayerKills", U(x.requiredPlayerKills), U(y.requiredPlayerKills));

    // Objectives arrays
    for (int i = 0; i < 4; ++i)
    {
        char f[32];
        std::snprintf(f, sizeof(f), "RequiredNpcOrGo%d", i + 1);
        s.add("Objectives", f, I(x.requiredNpcOrGo[i]), I(y.requiredNpcOrGo[i]));
        std::snprintf(f, sizeof(f), "RequiredNpcOrGoCount%d", i + 1);
        s.add("Objectives", f, U(x.requiredNpcOrGoCount[i]), U(y.requiredNpcOrGoCount[i]));
        std::snprintf(f, sizeof(f), "ItemDrop%d", i + 1);
        s.add("Objectives", f, U(x.itemDrop[i]), U(y.itemDrop[i]));
        std::snprintf(f, sizeof(f), "ItemDropQuantity%d", i + 1);
        s.add("Objectives", f, U(x.itemDropQuantity[i]), U(y.itemDropQuantity[i]));
        std::snprintf(f, sizeof(f), "ObjectiveText%d", i + 1);
        s.add("Objectives", f, Str(x.objectiveText[i]), Str(y.objectiveText[i]));
    }
    for (int i = 0; i < 6; ++i)
    {
        char f[32];
        std::snprintf(f, sizeof(f), "RequiredItemId%d", i + 1);
        s.add("Objectives", f, U(x.requiredItemId[i]), U(y.requiredItemId[i]));
        std::snprintf(f, sizeof(f), "RequiredItemCount%d", i + 1);
        s.add("Objectives", f, U(x.requiredItemCount[i]), U(y.requiredItemCount[i]));
    }

    // Rewards
    s.add("Rewards", "RewardMoney", I(x.rewardMoney), I(y.rewardMoney));
    s.add("Rewards", "RewardBonusMoney", U(x.rewardBonusMoney), U(y.rewardBonusMoney));
    s.add("Rewards", "RewardXPDifficulty", U(x.rewardXPDifficulty), U(y.rewardXPDifficulty));
    s.add("Rewards", "RewardSpell", I(x.rewardSpell), I(y.rewardSpell));
    s.add("Rewards", "RewardDisplaySpell", U(x.rewardDisplaySpell), U(y.rewardDisplaySpell));
    s.add("Rewards", "RewardHonor", I(x.rewardHonor), I(y.rewardHonor));
    s.add("Rewards", "RewardKillHonor", F(x.rewardKillHonor), F(y.rewardKillHonor));
    s.add("Rewards", "RewardTitle", U(x.rewardTitle), U(y.rewardTitle));
    s.add("Rewards", "RewardTalents", U(x.rewardTalents), U(y.rewardTalents));
    s.add("Rewards", "RewardArenaPoints", U(x.rewardArenaPoints), U(y.rewardArenaPoints));
    for (int i = 0; i < 4; ++i)
    {
        char f[32];
        std::snprintf(f, sizeof(f), "RewardItem%d", i + 1);
        s.add("Rewards", f, U(x.rewardItemId[i]), U(y.rewardItemId[i]));
        std::snprintf(f, sizeof(f), "RewardAmount%d", i + 1);
        s.add("Rewards", f, U(x.rewardAmount[i]), U(y.rewardAmount[i]));
    }
    for (int i = 0; i < 6; ++i)
    {
        char f[40];
        std::snprintf(f, sizeof(f), "RewardChoiceItemID%d", i + 1);
        s.add("Rewards", f, U(x.rewardChoiceItemId[i]), U(y.rewardChoiceItemId[i]));
        std::snprintf(f, sizeof(f), "RewardChoiceItemQuantity%d", i + 1);
        s.add("Rewards", f, U(x.rewardChoiceItemQuantity[i]), U(y.rewardChoiceItemQuantity[i]));
    }
    for (int i = 0; i < 5; ++i)
    {
        char f[40];
        std::snprintf(f, sizeof(f), "RewardFactionID%d", i + 1);
        s.add("Rewards", f, U(x.rewardFactionId[i]), U(y.rewardFactionId[i]));
        std::snprintf(f, sizeof(f), "RewardFactionValue%d", i + 1);
        s.add("Rewards", f, I(x.rewardFactionValue[i]), I(y.rewardFactionValue[i]));
    }

    // Text
    s.add("Text", "LogTitle", Str(x.logTitle), Str(y.logTitle));
    s.add("Text", "LogDescription", Str(x.logDescription), Str(y.logDescription));
    s.add("Text", "QuestDescription", Str(x.questDescription), Str(y.questDescription));
    s.add("Text", "AreaDescription", Str(x.areaDescription), Str(y.areaDescription));
    s.add("Text", "QuestCompletionLog", Str(x.questCompletionLog), Str(y.questCompletionLog));

    // Chain / addon
    s.add("Chain", "StartItem", U(x.startItem), U(y.startItem));
    s.add("Chain", "RewardNextQuest", U(x.rewardNextQuest), U(y.rewardNextQuest));
    s.add("Chain", "addon.present", a.addon.present ? "yes" : "no", b.addon.present ? "yes" : "no");
    s.add("Chain", "PrevQuestID", I(a.addon.prevQuestID), I(b.addon.prevQuestID));
    s.add("Chain", "NextQuestID", U(a.addon.nextQuestID), U(b.addon.nextQuestID));
    s.add("Chain", "ExclusiveGroup", I(a.addon.exclusiveGroup), I(b.addon.exclusiveGroup));
    s.add("Chain", "BreadcrumbForQuestId", I(a.addon.breadcrumbForQuestId),
          I(b.addon.breadcrumbForQuestId));
    s.add("Chain", "AllowableClasses", U(a.addon.allowableClasses), U(b.addon.allowableClasses));

    // Optional sub-rows: present-flag flips + a couple of key fields.
    s.add("Text", "quest_offer_reward", a.offerReward.present ? "present" : "absent",
          b.offerReward.present ? "present" : "absent");
    s.add("Text", "OfferRewardText", Str(a.offerReward.rewardText), Str(b.offerReward.rewardText));
    s.add("Text", "quest_request_items", a.requestItems.present ? "present" : "absent",
          b.requestItems.present ? "present" : "absent");
    s.add("Text", "CompletionText", Str(a.requestItems.completionText),
          Str(b.requestItems.completionText));

    // Container-level summaries.
    auto vecEq = [](const std::vector<uint32_t>& p, const std::vector<uint32_t>& q)
    { return p == q; };
    if (!vecEq(a.creatureStarters, b.creatureStarters))
        s.add("Questgivers", "creatureStarters", U(a.creatureStarters.size()),
              U(b.creatureStarters.size()));
    if (!vecEq(a.creatureEnders, b.creatureEnders))
        s.add("Questgivers", "creatureEnders", U(a.creatureEnders.size()),
              U(b.creatureEnders.size()));
    if (!vecEq(a.goStarters, b.goStarters))
        s.add("Questgivers", "goStarters", U(a.goStarters.size()), U(b.goStarters.size()));
    if (!vecEq(a.goEnders, b.goEnders))
        s.add("Questgivers", "goEnders", U(a.goEnders.size()), U(b.goEnders.size()));
    if (a.greetings.size() != b.greetings.size())
        s.add("Questgivers", "greetings", U(a.greetings.size()), U(b.greetings.size()));
    if (a.pois.size() != b.pois.size())
        s.add("POI", "pois", U(a.pois.size()), U(b.pois.size()));
    if (a.locales.size() != b.locales.size())
        s.add("Locales", "locales", U(a.locales.size()), U(b.locales.size()));
    if (a.conditions.size() != b.conditions.size())
        s.add("Conditions", "conditions", U(a.conditions.size()), U(b.conditions.size()));

    std::stable_sort(out.begin(), out.end(),
                     [](const FieldDiff& p, const FieldDiff& q)
                     {
                         if (p.tab != q.tab)
                             return p.tab < q.tab;
                         return p.field < q.field;
                     });
    return out;
}
} // namespace we
