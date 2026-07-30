// Layer E (ui) — Text tab: all display text + gossip/turn-in emotes.
// Edits quest_template text columns plus the three optional text tables
// (quest_details, quest_offer_reward, quest_request_items). See SPEC §5,§6.

#include "editors/quest/Tabs.h"
#include "editors/quest/QuestEditorContext.h"
#include "ui/Widgets.h"

#include "schema/Quest.h"

#include "imgui.h"

#include <cfloat>

namespace qe
{
namespace
{
// A labelled full-width multiline field: caption above, input below. The input
// uses a hidden "##" id so it fills the whole panel width without overflow.
bool LabeledMultiline(const char* caption, const char* id, std::string& value,
                      float height = 80.0f)
{
    ImGui::TextUnformatted(caption);
    return InputMultiline(id, value, height);
}

// A 4-row Emote/EmoteDelay table shared by quest_details and quest_offer_reward.
// Columns: [# (fixed 32) | Emote (stretch) | Delay ms (fixed 130)]. Both editors
// stretch to fill their column via SetNextItemWidth(-FLT_MIN), so nothing runs
// past the right edge. Returns true when any field changed this frame.
bool EmoteTable(const char* tableId, std::array<uint16_t, 4>& emote,
                std::array<uint32_t, 4>& emoteDelay)
{
    bool changed = false;
    if (ImGui::BeginTable(tableId, 3, ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Emote", ImGuiTableColumnFlags_WidthFixed, 300.0f);
        ImGui::TableSetupColumn("Delay (ms)", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 4; ++i)
        {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i + 1);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (EmoteCombo("##e", emote[i]))
                changed = true;

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputU32("##d", emoteDelay[i]))
                changed = true;

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    return changed;
}

// The "(row will be written / skipped)" hint drawn beside a present checkbox.
void PresentHint(bool present)
{
    ImGui::SameLine();
    ImGui::TextDisabled(present ? "(row will be written)" : "(row will be skipped)");
}
} // namespace

void DrawTextTab(QuestEditorContext& ctx)
{
    if (!ctx.quest)
    {
        ImGui::TextDisabled("No quest loaded.");
        return;
    }
    Quest& q = *ctx.quest;

    // --- Descriptions (quest_template) -----------------------------------
    ImGui::SeparatorText("Descriptions");
    {
        bool changed = false;
        changed |= LabeledMultiline("Log Description", "##logdesc", q.tmpl.logDescription);
        ImGui::Spacing();
        changed |= LabeledMultiline("Quest Description", "##questdesc", q.tmpl.questDescription);
        ImGui::Spacing();
        changed |= LabeledMultiline("Area Description", "##areadesc", q.tmpl.areaDescription);
        ImGui::Spacing();
        changed |= LabeledMultiline("Quest Completion Log", "##completionlog",
                                    q.tmpl.questCompletionLog);
        if (changed)
        {
            ctx.MarkChanged();
            q.tmplDirty = true;
        }
    }

    // --- Quest-Giver Gossip (quest_details) ------------------------------
    if (ImGui::CollapsingHeader("Quest-Giver Gossip (quest_details)",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool changed = false;

        bool present = q.details.present;
        if (ImGui::Checkbox("Write quest_details row", &present))
        {
            q.details.present = present;
            changed = true;
        }
        PresentHint(q.details.present);

        ImGui::Spacing();
        ImGui::TextUnformatted("Gossip Emotes");
        if (EmoteTable("details_emotes", q.details.emote, q.details.emoteDelay))
            changed = true;

        if (changed)
        {
            ctx.MarkChanged();
            q.detailsDirty = true;
            q.details.present = true;
        }
    }

    // --- Offer Reward (quest_offer_reward) -------------------------------
    if (ImGui::CollapsingHeader("Offer Reward (quest_offer_reward)",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool changed = false;

        bool present = q.offerReward.present;
        if (ImGui::Checkbox("Write quest_offer_reward row", &present))
        {
            q.offerReward.present = present;
            changed = true;
        }
        PresentHint(q.offerReward.present);

        ImGui::Spacing();
        if (LabeledMultiline("Reward Text", "##rewardtext", q.offerReward.rewardText))
            changed = true;

        ImGui::Spacing();
        ImGui::TextUnformatted("Turn-in Emotes");
        if (EmoteTable("offer_emotes", q.offerReward.emote, q.offerReward.emoteDelay))
            changed = true;

        if (changed)
        {
            ctx.MarkChanged();
            q.offerRewardDirty = true;
            q.offerReward.present = true;
        }
    }

    // --- Request Items (quest_request_items) -----------------------------
    if (ImGui::CollapsingHeader("Request Items (quest_request_items)",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool changed = false;

        bool present = q.requestItems.present;
        if (ImGui::Checkbox("Write quest_request_items row", &present))
        {
            q.requestItems.present = present;
            changed = true;
        }
        PresentHint(q.requestItems.present);

        ImGui::Spacing();
        if (LabeledMultiline("Completion Text", "##completiontext",
                             q.requestItems.completionText))
            changed = true;

        ImGui::Spacing();
        if (BeginFieldTable("request_items_emotes"))
        {
            FieldRow("Emote on Complete");
            if (EmoteCombo("##ec", q.requestItems.emoteOnComplete))
                changed = true;

            FieldRow("Emote on Incomplete");
            if (EmoteCombo("##ei", q.requestItems.emoteOnIncomplete))
                changed = true;

            EndFieldTable();
        }

        if (changed)
        {
            ctx.MarkChanged();
            q.requestItemsDirty = true;
            q.requestItems.present = true;
        }
    }
}
} // namespace qe
