// Layer E (ui) — CreatureEditorPanel. See header.

#include "editors/creature/CreatureEditorPanel.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "editors/creature/CreatureEditorContext.h"
#include "editors/creature/Tabs.h"
#include "ui/Widgets.h"

namespace we
{
void CreatureEditorPanel::Draw(CreatureEditorContext& ctx, bool hasCreature, bool dirty,
                               const CreatureEditorCallbacks& cb)
{
    if (!ImGui::Begin("Creature Editor"))
    {
        ImGui::End();
        return;
    }

    if (hasCreature && ctx.creature)
    {
        CreatureTemplate& t = ctx.creature->tmpl;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Entry");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::BeginDisabled(!ctx.creature->isNew);
        if (InputU32("##crentry", t.entry)) { ctx.MarkChanged(); ctx.creature->tmplDirty = true; }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(300.0f);
        if (InputTextString("Name##crname", t.name)) { ctx.MarkChanged(); ctx.creature->tmplDirty = true; }

        ImGui::SameLine();
        if (dirty)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "*");
        else
            ImGui::TextDisabled(" ");
    }
    else
    {
        ImGui::TextDisabled("No creature loaded. Use New or open one from the browser.");
    }

    if (ImGui::Button("New")) { if (cb.onNew) cb.onNew(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasCreature);
    if (ImGui::Button("Clone")) { if (cb.onClone) cb.onClone(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasCreature);
    if (ImGui::Button("Save")) { if (cb.onSave) cb.onSave(); }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) { if (cb.onRevert) cb.onRevert(); }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) { if (cb.onDelete) cb.onDelete(); }
    ImGui::EndDisabled();

    ImGui::Separator();

    if (ImGui::BeginTabBar("##creaturetabs", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        auto tf = [&](int idx) -> ImGuiTabItemFlags
        { return (pendingSelect == idx) ? ImGuiTabItemFlags_SetSelected : 0; };

        if (ImGui::BeginTabItem("General", nullptr, tf(0)))     { DrawCreatureGeneralTab(ctx);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Stats", nullptr, tf(1)))       { DrawCreatureStatsTab(ctx);     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Combat", nullptr, tf(2)))      { DrawCreatureCombatTab(ctx);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Flags", nullptr, tf(3)))       { DrawCreatureFlagsTab(ctx);     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Type & Loot", nullptr, tf(4))) { DrawCreatureTypeLootTab(ctx);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Movement", nullptr, tf(5)))    { DrawCreatureMovementTab(ctx);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Scripting", nullptr, tf(6)))   { DrawCreatureScriptingTab(ctx); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Addon", nullptr, tf(7)))       { DrawCreatureAddonTab(ctx);     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Equipment", nullptr, tf(8)))   { DrawCreatureEquipmentTab(ctx); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Locales", nullptr, tf(9)))     { DrawCreatureLocalesTab(ctx);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Vendor", nullptr, tf(10)))     { DrawCreatureVendorTab(ctx);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Trainer", nullptr, tf(11)))    { DrawCreatureTrainerTab(ctx);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Loot", nullptr, tf(12)))       { DrawCreatureLootTab(ctx);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Spawns", nullptr, tf(13)))     { DrawCreatureSpawnsTab(ctx);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Quest Items", nullptr, tf(14))){ DrawCreatureQuestItemsTab(ctx);ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    pendingSelect = -1;

    ImGui::End();
}
} // namespace we
