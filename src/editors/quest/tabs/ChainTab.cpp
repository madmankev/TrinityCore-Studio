// Layer E (ui) — Chain/Addon tab: quest chain / relationship editing.
//
// Edits chain-related fields spread across two tables:
//   quest_template.RewardNextQuest        (auto offered/handed next)
//   quest_template_addon.PrevQuestID      (required completed previous quest)
//   quest_template_addon.NextQuestID      (next quest in the chain)
//   quest_template_addon.ExclusiveGroup   (mutually-exclusive / one-of grouping)
//   quest_template_addon.BreadcrumbForQuestId (this quest is a breadcrumb)
//
// Values are edited as plain quest ids (no title lookup available here).

#include "editors/quest/Tabs.h"
#include "editors/quest/QuestEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"
#include "schema/Quest.h"
#include "data/LookupCache.h"

#include <string>

namespace qe
{
namespace
{
// Small grey wrapped explanation shown under a section. Wraps within the panel
// so long help text never widens the layout.
void HelpText(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}
} // namespace

void DrawChainTab(QuestEditorContext& ctx)
{
    if (!ctx.quest)
    {
        ImGui::TextDisabled("No quest loaded.");
        return;
    }

    Quest&              q     = *ctx.quest;
    QuestTemplate&      tmpl  = q.tmpl;
    QuestTemplateAddon& addon = q.addon;

    auto tmplChanged = [&]()
    {
        ctx.MarkChanged();
        q.tmplDirty = true;
    };
    auto addonChanged = [&]()
    {
        ctx.MarkChanged();
        q.addonDirty  = true;
        addon.present = true;
    };

    // --- Chain -----------------------------------------------------------
    ImGui::SeparatorText("Chain");

    if (BeginFieldTable("qe_chain"))
    {
        FieldRow("PrevQuestID",
                 "Quest that must already be rewarded before this one is offered.\n"
                 "A negative value references the quest by |id| but only requires it to be "
                 "in the log / active (single-quest of an exclusive group), not fully "
                 "completed.");
        if (InputI32("##PrevQuestID", addon.prevQuestID))
            addonChanged();

        FieldRow("NextQuestID",
                 "Id of the quest that lists this quest as its prerequisite — the next link "
                 "in the chain. Used to walk the chain forward; it does not itself auto-offer "
                 "the next quest.");
        if (InputU32("##NextQuestID", addon.nextQuestID))
            addonChanged();

        FieldRow("RewardNextQuest",
                 "Quest automatically offered (and, if valid, handed to the player) the "
                 "moment this quest is turned in — the seamless \"next\" hand-off. Lives on "
                 "quest_template, not the addon.");
        if (InputU32("##RewardNextQuest", tmpl.rewardNextQuest))
            tmplChanged();

        EndFieldTable();
    }
    HelpText("PrevQuestID / NextQuestID link the chain across quest_template_addon; "
             "RewardNextQuest (on quest_template) is the only one that auto-offers the next "
             "quest at turn-in.");

    // --- Exclusive Group -------------------------------------------------
    ImGui::SeparatorText("Exclusive Group");

    if (BeginFieldTable("qe_chain_exclusive"))
    {
        FieldRow("ExclusiveGroup",
                 "Groups quests that share the same non-zero value.\n"
                 ">0  ALL-required group: taking/completing one excludes the others "
                 "(one-of; the group is mutually exclusive).\n"
                 "<0  ONE-of group: the player may complete only one quest from the group; "
                 "the rest become unavailable.\n"
                 "0   no grouping.");
        if (InputI32("##ExclusiveGroup", addon.exclusiveGroup))
            addonChanged();

        EndFieldTable();
    }
    HelpText("Non-zero groups quests together. >0 = mutually-exclusive set (pick one, the "
             "others lock). <0 = do only one of the group. 0 = ungrouped.");

    // --- Breadcrumb ------------------------------------------------------
    ImGui::SeparatorText("Breadcrumb");

    if (BeginFieldTable("qe_chain_breadcrumb"))
    {
        FieldRow("BreadcrumbForQuestId",
                 "Marks this quest as a breadcrumb pointing the player toward the given quest "
                 "id. The breadcrumb auto-hides / cannot be taken once the target quest is "
                 "active or already completed.");
        if (InputI32("##BreadcrumbForQuestId", addon.breadcrumbForQuestId))
            addonChanged();

        EndFieldTable();
    }
    HelpText("Marks this quest as a breadcrumb pointing toward the given quest id. It "
             "auto-hides once the target quest is active or already completed.");

    // --- Navigate --------------------------------------------------------
    ImGui::SeparatorText("Linked Quests");
    LookupCache* lk = ctx.lookups;
    auto link = [&](const char* label, int32_t rawId)
    {
        const uint32_t qid = (rawId < 0) ? static_cast<uint32_t>(-static_cast<int64_t>(rawId))
                                         : static_cast<uint32_t>(rawId);
        if (qid == 0)
            return;
        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        const std::string title = lk ? lk->LabelQuest(qid) : std::to_string(qid);
        ImGui::TextDisabled("%s", title.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Open"))
            ctx.requestOpenQuestId = qid;
        ImGui::PopID();
    };
    const bool any = addon.prevQuestID || addon.nextQuestID || tmpl.rewardNextQuest ||
                     addon.breadcrumbForQuestId;
    if (!any)
    {
        ImGui::TextDisabled("No linked quests. (Titles resolve once connected.)");
    }
    else
    {
        link("Prev quest:", addon.prevQuestID);
        link("Next quest:", addon.nextQuestID);
        link("Reward-next quest:", static_cast<int32_t>(tmpl.rewardNextQuest));
        link("Breadcrumb for:", addon.breadcrumbForQuestId);
    }
}
} // namespace qe
