// Layer E (ui) — GameObject Loot tab: the loot slice keyed by Data1, into
// gameobject_loot_template (CHEST) or fishing_loot_template (FISHINGHOLE).

#include "editors/gameobject/Tabs.h"
#include "editors/gameobject/GameObjectEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/GameObject.h"
#include "data/LookupCache.h"

#include <cfloat>

namespace qe
{
void DrawGameObjectLootTab(GameObjectEditorContext& ctx)
{
    if (!ctx.go)
    {
        ImGui::TextDisabled("No gameobject loaded.");
        return;
    }
    GameObject& go = *ctx.go;
    GameObjectTemplate& t = go.tmpl;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); go.lootDirty = true; };

    const char* lootTable = (t.type == 3) ? "gameobject_loot_template"
                            : (t.type == 25) ? "fishing_loot_template"
                                             : nullptr;
    if (!lootTable)
    {
        ImGui::TextDisabled("This gameobject type has no loot table. Loot is only used by "
                            "Chest (type 3) and Fishing Hole (type 25).");
        return;
    }

    ImGui::Text("Loot table: %s (keyed by Data1)", lootTable);
    ImGui::Text("Loot id (Data1):");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    { int32_t v = t.data[1]; if (InputI32("##lootid", v)) { t.data[1] = v; ctx.MarkChanged(); go.tmplDirty = true; } }
    ImGui::SameLine();
    if (ImGui::Button("Add loot"))
    {
        if (t.data[1] == 0) { t.data[1] = static_cast<int32_t>(t.entry); ctx.MarkChanged(); go.tmplDirty = true; }
        go.loot.push_back(LootItem{});
        md();
    }
    ImGui::TextDisabled("Editing this loot template affects every object that shares its id.");

    int rm = -1;
    if (ImGui::BeginTable("##goloot", 10,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollX))
    {
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ref", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Chance", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Quest", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Comment", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(go.loot.size()); ++i)
        {
            ImGui::PushID(i);
            LootItem& l = go.loot[i];
            ImGui::TableNextRow();
            int col = 0;
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (IdNamePicker("##item", l.item, lk, RefKind::Item)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU32("##ref", l.reference)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##chance", l.chance)) md();
            ImGui::TableSetColumnIndex(col++); { bool b = l.questRequired != 0; if (ImGui::Checkbox("##quest", &b)) { l.questRequired = b ? 1 : 0; md(); } }
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU16("##mode", l.lootMode)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU8("##group", l.groupId)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU8("##min", l.minCount)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU8("##max", l.maxCount)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputTextString("##comment", l.comment)) md();
            ImGui::TableSetColumnIndex(col++); if (ImGui::Button("X")) rm = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rm >= 0) { go.loot.erase(go.loot.begin() + rm); md(); }
}
} // namespace qe
