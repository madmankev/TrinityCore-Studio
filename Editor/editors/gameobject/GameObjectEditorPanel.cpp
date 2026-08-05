// Layer E (ui) — GameObjectEditorPanel. See header.

#include "editors/gameobject/GameObjectEditorPanel.h"

#include "imgui.h"

#include "schema/GameObject.h"
#include "editors/gameobject/GameObjectEditorContext.h"
#include "editors/gameobject/Tabs.h"
#include "ui/Widgets.h"

namespace we
{
void GameObjectEditorPanel::Draw(GameObjectEditorContext& ctx, bool hasGo, bool dirty,
                                 const GameObjectEditorCallbacks& cb)
{
    if (!ImGui::Begin("GameObject Editor"))
    {
        ImGui::End();
        return;
    }

    if (hasGo && ctx.go)
    {
        GameObjectTemplate& t = ctx.go->tmpl;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Entry");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::BeginDisabled(!ctx.go->isNew);
        if (InputU32("##goentry", t.entry)) { ctx.MarkChanged(); ctx.go->tmplDirty = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300.0f);
        if (InputTextString("Name##goname", t.name)) { ctx.MarkChanged(); ctx.go->tmplDirty = true; }
        ImGui::SameLine();
        if (dirty)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "*");
        else
            ImGui::TextDisabled(" ");
    }
    else
    {
        ImGui::TextDisabled("No gameobject loaded. Use New or open one from the browser.");
    }

    if (ImGui::Button("New")) { if (cb.onNew) cb.onNew(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasGo);
    if (ImGui::Button("Clone")) { if (cb.onClone) cb.onClone(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasGo);
    if (ImGui::Button("Save")) { if (cb.onSave) cb.onSave(); }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) { if (cb.onRevert) cb.onRevert(); }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) { if (cb.onDelete) cb.onDelete(); }
    ImGui::EndDisabled();

    ImGui::Separator();

    if (ImGui::BeginTabBar("##gotabs", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        auto tf = [&](int idx) -> ImGuiTabItemFlags
        { return (pendingSelect == idx) ? ImGuiTabItemFlags_SetSelected : 0; };

        if (ImGui::BeginTabItem("General", nullptr, tf(0)))     { DrawGameObjectGeneralTab(ctx);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Data", nullptr, tf(1)))        { DrawGameObjectDataTab(ctx);       ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Addon", nullptr, tf(2)))       { DrawGameObjectAddonTab(ctx);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Locales", nullptr, tf(3)))     { DrawGameObjectLocalesTab(ctx);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Quest Items", nullptr, tf(4))) { DrawGameObjectQuestItemsTab(ctx); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Loot", nullptr, tf(5)))        { DrawGameObjectLootTab(ctx);       ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Spawns", nullptr, tf(6)))      { DrawGameObjectSpawnsTab(ctx);     ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    pendingSelect = -1;

    ImGui::End();
}
} // namespace we
