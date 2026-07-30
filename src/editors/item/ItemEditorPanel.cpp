// Layer E (ui) — ItemEditorPanel. See ItemEditorPanel.h.

#include "editors/item/ItemEditorPanel.h"

#include "imgui.h"

#include "schema/Item.h"
#include "editors/item/ItemEditorContext.h"
#include "editors/item/Tabs.h"
#include "ui/Widgets.h"

namespace qe
{
void ItemEditorPanel::Draw(ItemEditorContext& ctx, bool hasItem, bool dirty,
                           const ItemEditorCallbacks& cb)
{
    if (!ImGui::Begin("Item Editor"))
    {
        ImGui::End();
        return;
    }

    // --- Header row: entry id + name + dirty marker --------------------------
    if (hasItem && ctx.item)
    {
        ItemTemplate& t = ctx.item->tmpl;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Entry");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        // Editing the entry id is only meaningful for a freshly created item.
        ImGui::BeginDisabled(!ctx.item->isNew);
        if (InputU32("##itementry", t.entry))
        {
            ctx.MarkChanged();
            ctx.item->tmplDirty = true;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(320.0f);
        if (InputTextString("Name##itemname", t.name))
        {
            ctx.MarkChanged();
            ctx.item->tmplDirty = true;
        }

        ImGui::SameLine();
        if (dirty)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "*");
        else
            ImGui::TextDisabled(" ");
    }
    else
    {
        ImGui::TextDisabled("No item loaded. Use New or open one from the browser.");
    }

    // --- Action buttons ------------------------------------------------------
    if (ImGui::Button("New"))
        if (cb.onNew)
            cb.onNew();
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasItem);
    if (ImGui::Button("Clone"))
        if (cb.onClone)
            cb.onClone();
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(!hasItem);
    if (ImGui::Button("Save"))
        if (cb.onSave)
            cb.onSave();
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
        if (cb.onRevert)
            cb.onRevert();
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
        if (cb.onDelete)
            cb.onDelete();
    ImGui::EndDisabled();

    ImGui::Separator();

    // --- Tabs ----------------------------------------------------------------
    if (ImGui::BeginTabBar("##itemtabs", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        auto tabFlags = [&](int idx) -> ImGuiTabItemFlags
        { return (pendingSelect == idx) ? ImGuiTabItemFlags_SetSelected : 0; };

        if (ImGui::BeginTabItem("General", nullptr, tabFlags(0)))      { DrawItemGeneralTab(ctx);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Flags", nullptr, tabFlags(1)))        { DrawItemFlagsTab(ctx);        ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Requirements", nullptr, tabFlags(2))) { DrawItemRequirementsTab(ctx); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Stats", nullptr, tabFlags(3)))        { DrawItemStatsTab(ctx);        ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Weapon/Armor", nullptr, tabFlags(4))) { DrawItemWeaponArmorTab(ctx);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Spells", nullptr, tabFlags(5)))       { DrawItemSpellsTab(ctx);       ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Sockets", nullptr, tabFlags(6)))      { DrawItemSocketsTab(ctx);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Text & Set", nullptr, tabFlags(7)))   { DrawItemTextSetTab(ctx);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Locales", nullptr, tabFlags(8)))      { DrawItemLocalesTab(ctx);      ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    pendingSelect = -1;

    ImGui::End();
}
} // namespace qe
