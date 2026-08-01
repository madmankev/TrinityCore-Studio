// Layer E (ui) — Rewards tab. Edits reward-related columns across
// quest_template (money/misc, reward items, factions), quest_template_addon
// (mail template/delay) and quest_mail_sender (mail sender entry).

#include "editors/quest/Tabs.h"
#include "editors/quest/QuestEditorContext.h"
#include "ui/Widgets.h"
#include "clientdata/ClientAssets.h"

#include "imgui.h"

#include "schema/Quest.h"
#include "util/Enums.h"
#include "util/StringUtil.h"
#include "data/LookupCache.h"

#include <cfloat>

namespace we
{
void DrawRewardsTab(QuestEditorContext& ctx)
{
    if (!ctx.quest)
    {
        ImGui::TextDisabled("No quest loaded.");
        return;
    }

    Quest&         q     = *ctx.quest;
    QuestTemplate& t     = q.tmpl;
    LookupCache&   cache = *ctx.lookups;

    // Dirty helpers: tmpl fields, addon fields (mark present), mail sender (mark present).
    auto markTmpl = [&]() { ctx.MarkChanged(); q.tmplDirty = true; };
    auto markAddon = [&]() { ctx.MarkChanged(); q.addonDirty = true; q.addon.present = true; };
    auto markMailSender = [&]() { ctx.MarkChanged(); q.mailSenderDirty = true; q.mailSender.present = true; };

    // --- Money & Misc (quest_template) ------------------------------------
    ImGui::SeparatorText("Money & Misc");

    if (BeginFieldTable("qe_rewards_money"))
    {
        FieldRow("Money", "quest_template.RewardMoney (copper).");
        if (MoneyEditor("##money", t.rewardMoney))
            markTmpl();

        FieldRow("Bonus Money", "quest_template.RewardBonusMoney (copper).");
        if (InputU32("##bonusmoney", t.rewardBonusMoney))
            markTmpl();

        FieldRow("XP Difficulty", "quest_template.RewardXPDifficulty.");
        {
            uint32_t xpDiff = t.rewardXPDifficulty;
            if (EnumCombo("##xpdiff", xpDiff, RewardXPDifficultyValues()))
            {
                t.rewardXPDifficulty = static_cast<uint8_t>(xpDiff);
                markTmpl();
            }
        }

        FieldRow("Spell", "quest_template.RewardSpell.");
        if (IdNamePicker("##spell", t.rewardSpell, cache, RefKind::Spell))
            markTmpl();

        FieldRow("Display Spell", "quest_template.RewardDisplaySpell.");
        if (IdNamePicker("##dispspell", t.rewardDisplaySpell, cache, RefKind::Spell))
            markTmpl();

        FieldRow("Honor", "quest_template.RewardHonor.");
        if (InputI32("##honor", t.rewardHonor))
            markTmpl();

        FieldRow("Kill Honor", "quest_template.RewardKillHonor.");
        if (InputFloatField("##killhonor", t.rewardKillHonor))
            markTmpl();

        FieldRow("Title", "quest_template.RewardTitle (CharTitles id).");
        if (IdNamePicker("##title", t.rewardTitle, cache, RefKind::Title))
            markTmpl();

        FieldRow("Talents", "quest_template.RewardTalents.");
        if (InputU8("##talents", t.rewardTalents))
            markTmpl();

        FieldRow("Arena Points", "quest_template.RewardArenaPoints.");
        if (InputU16("##arena", t.rewardArenaPoints))
            markTmpl();

        EndFieldTable();
    }

    // --- Reward Items (guaranteed) ----------------------------------------
    ImGui::SeparatorText("Items (guaranteed)");

    if (ImGui::BeginTable("rewItems", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX))
    {
        ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Item",   ImGuiTableColumnFlags_WidthFixed, 340.0f);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 4; ++i)
        {
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i + 1);

            ImGui::TableSetColumnIndex(1);
            if (IdNamePicker("##it", t.rewardItemId[i], cache, RefKind::Item))
                markTmpl();

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputU16("##amt", t.rewardAmount[i]))
                markTmpl();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // --- Reward Items (player choice) -------------------------------------
    ImGui::SeparatorText("Items (player choice)");

    if (ImGui::BeginTable("rewChoiceItems", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX))
    {
        ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Item",     ImGuiTableColumnFlags_WidthFixed, 340.0f);
        ImGui::TableSetupColumn("Quantity", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 6; ++i)
        {
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i + 1);

            ImGui::TableSetColumnIndex(1);
            if (IdNamePicker("##cit", t.rewardChoiceItemId[i], cache, RefKind::Item))
                markTmpl();

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputU16("##cqty", t.rewardChoiceItemQuantity[i]))
                markTmpl();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // --- Faction Rewards --------------------------------------------------
    ImGui::SeparatorText("Faction Rewards");

    if (ImGui::BeginTable("rewFactions", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX))
    {
        ImGui::TableSetupColumn("#",          ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Faction ID", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value",      ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Override",   ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 5; ++i)
        {
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i + 1);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (IdNamePicker("##fac", t.rewardFactionId[i], cache, RefKind::Faction))
                markTmpl();

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputI32("##facval", t.rewardFactionValue[i]))
                markTmpl();

            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputI32("##facovr", t.rewardFactionOverride[i]))
                markTmpl();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // --- Mail Reward ------------------------------------------------------
    ImGui::SeparatorText("Mail Reward");

    if (BeginFieldTable("qe_rewards_mail"))
    {
        FieldRow("Mail Template ID", "quest_template_addon.RewardMailTemplateID.");
        if (IdNamePicker("##mailtmpl", q.addon.rewardMailTemplateID, cache, RefKind::MailTemplate))
            markAddon();

        FieldRow("Mail Delay (seconds)", "quest_template_addon.RewardMailDelay.");
        if (InputU32("##maildelay", q.addon.rewardMailDelay))
            markAddon();

        FieldRow("Mail Sender", "quest_mail_sender.RewardMailSenderEntry (creature).");
        if (IdNamePicker("##mailsender", q.mailSender.rewardMailSenderEntry, cache, RefKind::Creature))
            markMailSender();

        EndFieldTable();
    }
}
} // namespace we
