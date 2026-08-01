// Layer E (ui) — QuestEditorPanel. See QuestEditorPanel.h.

#include "editors/quest/QuestEditorPanel.h"

#include "imgui.h"

#include "schema/Quest.h"
#include "editors/quest/QuestEditorContext.h"
#include "editors/quest/Tabs.h"
#include "ui/Widgets.h"

namespace we
{
void QuestEditorPanel::Draw(QuestEditorContext& ctx, bool hasQuest, bool dirty,
                            const QuestEditorCallbacks& cb)
{
    if (!ImGui::Begin("Quest Editor"))
    {
        ImGui::End();
        return;
    }

    // --- Header row: ID + title + dirty marker -------------------------------
    if (hasQuest && ctx.quest)
    {
        QuestTemplate& t = ctx.quest->tmpl;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Quest ID");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        // Editing the ID is only meaningful for a freshly created quest.
        ImGui::BeginDisabled(!ctx.quest->isNew);
        if (InputU32("##questid", t.id))
        {
            ctx.MarkChanged();
            ctx.quest->tmplDirty = true;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(320.0f);
        if (InputTextString("Title##logtitle", t.logTitle))
        {
            ctx.MarkChanged();
            ctx.quest->tmplDirty = true;
        }

        ImGui::SameLine();
        if (dirty)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "*");
        else
            ImGui::TextDisabled(" ");
    }
    else
    {
        ImGui::TextDisabled("No quest loaded. Use New or open one from the browser.");
    }

    // --- Action buttons ------------------------------------------------------
    if (ImGui::Button("New"))
    {
        if (cb.onNew)
            cb.onNew();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasQuest);
    if (ImGui::Button("Clone"))
    {
        if (cb.onClone)
            cb.onClone();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(!hasQuest);
    if (ImGui::Button("Save"))
    {
        if (cb.onSave)
            cb.onSave();
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        if (cb.onRevert)
            cb.onRevert();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
    {
        if (cb.onDelete)
            cb.onDelete();
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    // --- Tabs ----------------------------------------------------------------
    if (ImGui::BeginTabBar("##questtabs", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        auto tabFlags = [&](int idx) -> ImGuiTabItemFlags
        { return (pendingSelect == idx) ? ImGuiTabItemFlags_SetSelected : 0; };

        if (ImGui::BeginTabItem("General", nullptr, tabFlags(0)))      { DrawGeneralTab(ctx);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Requirements", nullptr, tabFlags(1))) { DrawRequirementsTab(ctx); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Objectives", nullptr, tabFlags(2)))   { DrawObjectivesTab(ctx);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Rewards", nullptr, tabFlags(3)))      { DrawRewardsTab(ctx);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Text", nullptr, tabFlags(4)))         { DrawTextTab(ctx);         ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Chain/Addon", nullptr, tabFlags(5)))  { DrawChainTab(ctx);        ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Questgivers", nullptr, tabFlags(6)))  { DrawQuestgiversTab(ctx);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("POI", nullptr, tabFlags(7)))          { DrawPoiTab(ctx);          ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Locales", nullptr, tabFlags(8)))      { DrawLocalesTab(ctx);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Conditions", nullptr, tabFlags(9)))   { DrawConditionsTab(ctx);   ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    pendingSelect = -1;

    ImGui::End();
}
} // namespace we
