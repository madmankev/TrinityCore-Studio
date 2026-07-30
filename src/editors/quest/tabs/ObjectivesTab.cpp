// Layer E (ui) — Objectives tab: what the player must DO to complete the quest.
// All fields here live in quest_template, so every edit sets q.tmplDirty.

#include "editors/quest/Tabs.h"
#include "editors/quest/QuestEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include <cfloat>
#include <cstdio>

#include "schema/Quest.h"
#include "data/LookupCache.h"

namespace qe
{
void DrawObjectivesTab(QuestEditorContext& ctx)
{
    if (!ctx.quest)
    {
        ImGui::TextDisabled("No quest loaded.");
        return;
    }
    Quest& q = *ctx.quest;
    QuestTemplate& t = q.tmpl;

    auto touch = [&]() {
        ctx.MarkChanged();
        q.tmplDirty = true;
    };

    // Overflow-proof id+count table: a fixed "#" column, a stretch "Target"
    // column that clips the picker's trailing name inside the cell, and a fixed
    // "Count" column so it is always visible on the right.
    const ImGuiTableFlags kIdCountFlags =
        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;

    // --- Player-vs-player kills ------------------------------------------
    ImGui::SeparatorText("Player Kills");
    if (BeginFieldTable("obj_playerkills"))
    {
        FieldRow("Player Kills", "quest_template.RequiredPlayerKills");
        if (InputU8("##playerkills", t.requiredPlayerKills))
            touch();
        EndFieldTable();
    }

    // --- Kill / Interact (NPC or GameObject) -----------------------------
    ImGui::SeparatorText("Kill / Interact (NPC or GameObject)");
    if (ImGui::BeginTable("killgo", 3, kIdCountFlags))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 340.0f);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 4; ++i)
        {
            ImGui::TableNextRow();
            ImGui::PushID(i);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i + 1);
            ImGui::TableSetColumnIndex(1);
            if (IdNamePickerSigned("##t", t.requiredNpcOrGo[i], *ctx.lookups))
                touch();
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputU16("##c", t.requiredNpcOrGoCount[i]))
                touch();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // --- Required (gathered) items ---------------------------------------
    ImGui::SeparatorText("Required Items");
    if (ImGui::BeginTable("reqitems", 3, kIdCountFlags))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 340.0f);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 6; ++i)
        {
            ImGui::TableNextRow();
            ImGui::PushID(i);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i + 1);
            ImGui::TableSetColumnIndex(1);
            if (IdNamePicker("##t", t.requiredItemId[i], *ctx.lookups, RefKind::Item))
                touch();
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputU16("##c", t.requiredItemCount[i]))
                touch();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // --- Item drops (mob drops while on quest) ---------------------------
    ImGui::SeparatorText("Item Drops (mob drops while on quest)");
    if (ImGui::BeginTable("itemdrops", 3, kIdCountFlags))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 340.0f);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 4; ++i)
        {
            ImGui::TableNextRow();
            ImGui::PushID(i);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i + 1);
            ImGui::TableSetColumnIndex(1);
            if (IdNamePicker("##t", t.itemDrop[i], *ctx.lookups, RefKind::Item))
                touch();
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputU16("##c", t.itemDropQuantity[i]))
                touch();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // --- Free-text objective lines ---------------------------------------
    ImGui::SeparatorText("Text Objectives");
    if (BeginFieldTable("obj_textobjectives"))
    {
        for (int i = 0; i < 4; ++i)
        {
            ImGui::PushID(1000 + i);
            char label[32];
            std::snprintf(label, sizeof(label), "Objective Text %d", i + 1);
            FieldRow(label);
            if (InputTextString("##objtext", t.objectiveText[i]))
                touch();
            ImGui::PopID();
        }
        EndFieldTable();
    }
}
} // namespace qe
