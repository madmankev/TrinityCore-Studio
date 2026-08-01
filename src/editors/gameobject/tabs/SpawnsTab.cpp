// Layer E (ui) — GameObject Spawns tab: gameobject world spawns, edited in place per
// guid (new/dirty/deleted), mirroring the creature Spawns tab.

#include "editors/gameobject/Tabs.h"
#include "editors/gameobject/GameObjectEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/GameObject.h"

#include <cfloat>

namespace we
{
void DrawGameObjectSpawnsTab(GameObjectEditorContext& ctx)
{
    if (!ctx.go)
    {
        ImGui::TextDisabled("No gameobject loaded.");
        return;
    }
    GameObject& go = *ctx.go;
    auto touch = [&](GameObjectSpawn& s) { if (!s.isNewRow) s.rowDirty = true; ctx.MarkChanged(); go.spawnsDirty = true; };

    ImGui::TextDisabled("World spawns of this gameobject (gameobject table). Edits apply per-guid on save.");
    if (ImGui::Button("Add spawn"))
    {
        GameObjectSpawn s;
        s.isNewRow = true;
        s.rotation[3] = 1.0f;  // identity-ish quaternion default (w)
        go.spawns.push_back(s);
        ctx.MarkChanged();
        go.spawnsDirty = true;
    }
    int pendingDel = 0;
    for (const GameObjectSpawn& s : go.spawns)
        if (s.deleted)
            ++pendingDel;
    if (pendingDel > 0)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.6f, 1.0f), "%d marked for deletion", pendingDel);
    }

    int rm = -1;
    if (ImGui::BeginTable("##gospawns", 9,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollX))
    {
        ImGui::TableSetupColumn("guid", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("map", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("zone", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("O", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("SpawnT", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(go.spawns.size()); ++i)
        {
            GameObjectSpawn& s = go.spawns[i];
            if (s.deleted)
                continue;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            int col = 0;
            ImGui::TableSetColumnIndex(col++);
            ImGui::AlignTextToFramePadding();
            if (s.isNewRow) ImGui::TextDisabled("new"); else ImGui::Text("%u", s.guid);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU16("##map", s.map)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU16("##zone", s.zoneId)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##x", s.x)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##y", s.y)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##z", s.z)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##o", s.o)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputI32("##st", s.spawnTimeSecs)) touch(s);
            ImGui::TableSetColumnIndex(col++); if (ImGui::Button("X")) rm = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rm >= 0)
    {
        if (go.spawns[rm].isNewRow)
            go.spawns.erase(go.spawns.begin() + rm);
        else
            go.spawns[rm].deleted = true;
        ctx.MarkChanged();
        go.spawnsDirty = true;
    }
}
} // namespace we
