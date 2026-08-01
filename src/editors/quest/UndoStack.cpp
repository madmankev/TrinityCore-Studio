#include "editors/quest/UndoStack.h"

#include <utility>

namespace we
{
namespace
{
// --- Member-wise equality for each sub-struct ---------------------------------
// The schema structs carry no operator==, so equality is spelled out here.
// std::array / std::string already provide ==; only the aggregate structs and
// vectors of them need explicit helpers. Presence flags are part of editable
// content and ARE compared; per-part dirty flags live on Quest and are ignored.

bool Eq(const QuestTemplate& a, const QuestTemplate& b)
{
    return a.id == b.id &&
           a.questType == b.questType &&
           a.questLevel == b.questLevel &&
           a.minLevel == b.minLevel &&
           a.questSortID == b.questSortID &&
           a.questInfoID == b.questInfoID &&
           a.suggestedGroupNum == b.suggestedGroupNum &&
           a.requiredFactionId1 == b.requiredFactionId1 &&
           a.requiredFactionId2 == b.requiredFactionId2 &&
           a.requiredFactionValue1 == b.requiredFactionValue1 &&
           a.requiredFactionValue2 == b.requiredFactionValue2 &&
           a.rewardNextQuest == b.rewardNextQuest &&
           a.rewardXPDifficulty == b.rewardXPDifficulty &&
           a.rewardMoney == b.rewardMoney &&
           a.rewardBonusMoney == b.rewardBonusMoney &&
           a.rewardDisplaySpell == b.rewardDisplaySpell &&
           a.rewardSpell == b.rewardSpell &&
           a.rewardHonor == b.rewardHonor &&
           a.rewardKillHonor == b.rewardKillHonor &&
           a.startItem == b.startItem &&
           a.flags == b.flags &&
           a.requiredPlayerKills == b.requiredPlayerKills &&
           a.rewardItemId == b.rewardItemId &&
           a.rewardAmount == b.rewardAmount &&
           a.itemDrop == b.itemDrop &&
           a.itemDropQuantity == b.itemDropQuantity &&
           a.rewardChoiceItemId == b.rewardChoiceItemId &&
           a.rewardChoiceItemQuantity == b.rewardChoiceItemQuantity &&
           a.poiContinent == b.poiContinent &&
           a.poiX == b.poiX &&
           a.poiY == b.poiY &&
           a.poiPriority == b.poiPriority &&
           a.rewardTitle == b.rewardTitle &&
           a.rewardTalents == b.rewardTalents &&
           a.rewardArenaPoints == b.rewardArenaPoints &&
           a.rewardFactionId == b.rewardFactionId &&
           a.rewardFactionValue == b.rewardFactionValue &&
           a.rewardFactionOverride == b.rewardFactionOverride &&
           a.timeAllowed == b.timeAllowed &&
           a.allowableRaces == b.allowableRaces &&
           a.logTitle == b.logTitle &&
           a.logDescription == b.logDescription &&
           a.questDescription == b.questDescription &&
           a.areaDescription == b.areaDescription &&
           a.questCompletionLog == b.questCompletionLog &&
           a.requiredNpcOrGo == b.requiredNpcOrGo &&
           a.requiredNpcOrGoCount == b.requiredNpcOrGoCount &&
           a.requiredItemId == b.requiredItemId &&
           a.requiredItemCount == b.requiredItemCount &&
           a.rewardFactionFlags == b.rewardFactionFlags &&
           a.objectiveText == b.objectiveText &&
           a.verifiedBuild == b.verifiedBuild;
}

bool Eq(const QuestTemplateAddon& a, const QuestTemplateAddon& b)
{
    return a.id == b.id &&
           a.maxLevel == b.maxLevel &&
           a.allowableClasses == b.allowableClasses &&
           a.sourceSpellID == b.sourceSpellID &&
           a.prevQuestID == b.prevQuestID &&
           a.nextQuestID == b.nextQuestID &&
           a.exclusiveGroup == b.exclusiveGroup &&
           a.breadcrumbForQuestId == b.breadcrumbForQuestId &&
           a.rewardMailTemplateID == b.rewardMailTemplateID &&
           a.rewardMailDelay == b.rewardMailDelay &&
           a.requiredSkillID == b.requiredSkillID &&
           a.requiredSkillPoints == b.requiredSkillPoints &&
           a.requiredMinRepFaction == b.requiredMinRepFaction &&
           a.requiredMaxRepFaction == b.requiredMaxRepFaction &&
           a.requiredMinRepValue == b.requiredMinRepValue &&
           a.requiredMaxRepValue == b.requiredMaxRepValue &&
           a.providedItemCount == b.providedItemCount &&
           a.specialFlags == b.specialFlags &&
           a.present == b.present;
}

bool Eq(const QuestOfferReward& a, const QuestOfferReward& b)
{
    return a.id == b.id &&
           a.emote == b.emote &&
           a.emoteDelay == b.emoteDelay &&
           a.rewardText == b.rewardText &&
           a.verifiedBuild == b.verifiedBuild &&
           a.present == b.present;
}

bool Eq(const QuestRequestItems& a, const QuestRequestItems& b)
{
    return a.id == b.id &&
           a.emoteOnComplete == b.emoteOnComplete &&
           a.emoteOnIncomplete == b.emoteOnIncomplete &&
           a.completionText == b.completionText &&
           a.verifiedBuild == b.verifiedBuild &&
           a.present == b.present;
}

bool Eq(const QuestDetails& a, const QuestDetails& b)
{
    return a.id == b.id &&
           a.emote == b.emote &&
           a.emoteDelay == b.emoteDelay &&
           a.verifiedBuild == b.verifiedBuild &&
           a.present == b.present;
}

bool Eq(const QuestMailSender& a, const QuestMailSender& b)
{
    return a.questId == b.questId &&
           a.rewardMailSenderEntry == b.rewardMailSenderEntry &&
           a.present == b.present;
}

bool Eq(const QuestGreeting& a, const QuestGreeting& b)
{
    return a.id == b.id &&
           a.type == b.type &&
           a.greetEmoteType == b.greetEmoteType &&
           a.greetEmoteDelay == b.greetEmoteDelay &&
           a.greeting == b.greeting &&
           a.verifiedBuild == b.verifiedBuild;
}

bool Eq(const QuestPoiPoint& a, const QuestPoiPoint& b)
{
    return a.questID == b.questID &&
           a.idx1 == b.idx1 &&
           a.idx2 == b.idx2 &&
           a.x == b.x &&
           a.y == b.y &&
           a.verifiedBuild == b.verifiedBuild;
}

bool Eq(const QuestPoi& a, const QuestPoi& b)
{
    if (!(a.questID == b.questID &&
          a.id == b.id &&
          a.objectiveIndex == b.objectiveIndex &&
          a.mapID == b.mapID &&
          a.worldMapAreaId == b.worldMapAreaId &&
          a.floor == b.floor &&
          a.priority == b.priority &&
          a.flags == b.flags &&
          a.verifiedBuild == b.verifiedBuild))
        return false;

    if (a.points.size() != b.points.size())
        return false;
    for (size_t i = 0; i < a.points.size(); ++i)
        if (!Eq(a.points[i], b.points[i]))
            return false;
    return true;
}

bool Eq(const QuestLocale& a, const QuestLocale& b)
{
    return a.locale == b.locale &&
           a.title == b.title &&
           a.details == b.details &&
           a.objectives == b.objectives &&
           a.endText == b.endText &&
           a.completedText == b.completedText &&
           a.objectiveText == b.objectiveText &&
           a.rewardText == b.rewardText &&
           a.completionText == b.completionText &&
           a.greetingCreature == b.greetingCreature &&
           a.greetingGameObject == b.greetingGameObject &&
           a.templatePresent == b.templatePresent &&
           a.offerRewardPresent == b.offerRewardPresent &&
           a.requestItemsPresent == b.requestItemsPresent &&
           a.greetingCreaturePresent == b.greetingCreaturePresent &&
           a.greetingGameObjectPresent == b.greetingGameObjectPresent;
}

bool Eq(const QuestCondition& a, const QuestCondition& b)
{
    return a.sourceTypeOrReferenceId == b.sourceTypeOrReferenceId &&
           a.sourceGroup == b.sourceGroup &&
           a.sourceEntry == b.sourceEntry &&
           a.sourceId == b.sourceId &&
           a.elseGroup == b.elseGroup &&
           a.conditionTypeOrReference == b.conditionTypeOrReference &&
           a.conditionTarget == b.conditionTarget &&
           a.conditionValue1 == b.conditionValue1 &&
           a.conditionValue2 == b.conditionValue2 &&
           a.conditionValue3 == b.conditionValue3 &&
           a.negativeCondition == b.negativeCondition &&
           a.errorType == b.errorType &&
           a.errorTextId == b.errorTextId &&
           a.scriptName == b.scriptName &&
           a.comment == b.comment;
}

// Order-sensitive vector comparison using the element Eq helpers.
template <typename T>
bool VecEq(const std::vector<T>& a, const std::vector<T>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!Eq(a[i], b[i]))
            return false;
    return true;
}
} // namespace

bool Differs(const Quest& a, const Quest& b)
{
    if (!Eq(a.tmpl, b.tmpl))
        return true;
    if (!Eq(a.addon, b.addon))
        return true;
    if (!Eq(a.offerReward, b.offerReward))
        return true;
    if (!Eq(a.requestItems, b.requestItems))
        return true;
    if (!Eq(a.details, b.details))
        return true;
    if (!Eq(a.mailSender, b.mailSender))
        return true;

    if (!VecEq(a.greetings, b.greetings))
        return true;

    // Questgiver vectors are plain uint32_t -- std::vector::operator== suffices.
    if (a.creatureStarters != b.creatureStarters)
        return true;
    if (a.creatureEnders != b.creatureEnders)
        return true;
    if (a.goStarters != b.goStarters)
        return true;
    if (a.goEnders != b.goEnders)
        return true;

    if (!VecEq(a.pois, b.pois))
        return true;

    // Locales: compare key set and each value.
    if (a.locales.size() != b.locales.size())
        return true;
    for (const auto& [key, val] : a.locales)
    {
        auto it = b.locales.find(key);
        if (it == b.locales.end())
            return true;
        if (!Eq(val, it->second))
            return true;
    }

    if (!VecEq(a.conditions, b.conditions))
        return true;

    return false;
}

void UndoStack::Reset(const Quest& initial)
{
    (void)initial; // baseline is the caller's current state; no snapshot kept
    undo_.clear();
    redo_.clear();
}

void UndoStack::Push(const Quest& stateBeforeEdit)
{
    undo_.push_back(stateBeforeEdit);
    if (undo_.size() > kMax)
        undo_.erase(undo_.begin(), undo_.begin() + (undo_.size() - kMax));
    redo_.clear();
}

bool UndoStack::CanUndo() const
{
    return !undo_.empty();
}

bool UndoStack::CanRedo() const
{
    return !redo_.empty();
}

Quest UndoStack::Undo(const Quest& current)
{
    if (undo_.empty())
        return current;

    redo_.push_back(current);
    if (redo_.size() > kMax)
        redo_.erase(redo_.begin(), redo_.begin() + (redo_.size() - kMax));

    Quest previous = std::move(undo_.back());
    undo_.pop_back();
    return previous;
}

Quest UndoStack::Redo(const Quest& current)
{
    if (redo_.empty())
        return current;

    undo_.push_back(current);
    if (undo_.size() > kMax)
        undo_.erase(undo_.begin(), undo_.begin() + (undo_.size() - kMax));

    Quest next = std::move(redo_.back());
    redo_.pop_back();
    return next;
}

void UndoStack::Clear()
{
    undo_.clear();
    redo_.clear();
}

size_t UndoStack::UndoDepth() const
{
    return undo_.size();
}

size_t UndoStack::RedoDepth() const
{
    return redo_.size();
}
} // namespace we
