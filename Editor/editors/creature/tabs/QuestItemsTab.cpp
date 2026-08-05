// Layer E (ui) — Creature Quest Items tab: creature_questitem (ItemId list, Idx = order).
// Mirrors the GameObject editor's Quest Items tab (gameobject_questitem).

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "data/LookupCache.h"

#include <cfloat>

namespace we
{
void DrawCreatureQuestItemsTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    Creature& cr = *ctx.creature;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); cr.questItemsDirty = true; };

    ImGui::TextDisabled("Quest items this creature provides (creature_questitem). Order = Idx.");
    if (ImGui::Button("Add quest item"))
    {
        cr.questItems.push_back(0);
        md();
    }

    int rm = -1;
    if (ImGui::BeginTable("##crquestitems", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(cr.questItems.size()); ++i)
        {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (IdNamePicker("##item", cr.questItems[i], lk, RefKind::Item)) md();
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("X")) rm = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rm >= 0) { cr.questItems.erase(cr.questItems.begin() + rm); md(); }
}
} // namespace we
