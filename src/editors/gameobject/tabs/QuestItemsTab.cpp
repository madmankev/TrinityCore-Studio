// Layer E (ui) — GameObject Quest Items tab: gameobject_questitem (ItemId list).

#include "editors/gameobject/Tabs.h"
#include "editors/gameobject/GameObjectEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/GameObject.h"
#include "data/LookupCache.h"

#include <cfloat>
#include <cstdio>

namespace qe
{
void DrawGameObjectQuestItemsTab(GameObjectEditorContext& ctx)
{
    if (!ctx.go)
    {
        ImGui::TextDisabled("No gameobject loaded.");
        return;
    }
    GameObject& go = *ctx.go;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); go.questItemsDirty = true; };

    ImGui::TextDisabled("Quest items this GO provides (gameobject_questitem). Order = Idx.");
    if (ImGui::Button("Add quest item"))
    {
        go.questItems.push_back(0);
        md();
    }

    int rm = -1;
    if (ImGui::BeginTable("##goquestitems", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(go.questItems.size()); ++i)
        {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (IdNamePicker("##item", go.questItems[i], lk, RefKind::Item)) md();
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("X")) rm = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rm >= 0) { go.questItems.erase(go.questItems.begin() + rm); md(); }
}
} // namespace qe
