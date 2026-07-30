// Layer E (ui) — GameObjectBrowserPanel. See header.

#include "editors/gameobject/GameObjectBrowserPanel.h"

#include "imgui.h"

#include "data/LookupCache.h"
#include "ui/Widgets.h"
#include "util/Enums.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace qe
{
void GameObjectBrowserPanel::Draw(const std::vector<GameObjectListEntry>& entries, bool connected,
                                  LookupCache& lookups, GameObjectBrowserCallbacks& cb)
{
    (void)lookups;
    if (!ImGui::Begin("GameObject Browser"))
    {
        ImGui::End();
        return;
    }

    lastPageCount = static_cast<int>(entries.size());
    bool doRefresh = false;
    auto requery = [&](bool resetPage) { if (resetPage) filter.offset = 0; doRefresh = true; };

    ImGui::SetNextItemWidth(220.0f);
    if (InputTextString("Filter (id or name)", filter.text, ImGuiInputTextFlags_EnterReturnsTrue))
        requery(true);
    ImGui::SameLine();
    ImGui::BeginDisabled(!connected);
    if (ImGui::Button("Search"))
        requery(true);
    ImGui::EndDisabled();

    {
        const std::vector<EnumEntry>& types = GameObjectTypeValues();
        std::string tp = "Any type";
        if (filter.type >= 0)
        {
            const char* l = LabelFor(types, static_cast<uint32_t>(filter.type));
            tp = l ? l : std::to_string(filter.type);
        }
        ImGui::SetNextItemWidth(170.0f);
        if (ImGui::BeginCombo("Type", tp.c_str()))
        {
            if (ImGui::Selectable("Any", filter.type < 0)) { filter.type = -1; requery(true); }
            for (const EnumEntry& e : types)
                if (ImGui::Selectable(e.label, filter.type == static_cast<int>(e.value)))
                {
                    filter.type = static_cast<int>(e.value);
                    requery(true);
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) { filter.text.clear(); filter.type = -1; requery(true); }
    }

    {
        const int page = filter.limit > 0 ? filter.offset / filter.limit : 0;
        ImGui::BeginDisabled(!connected || filter.offset <= 0);
        if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) { filter.offset = std::max(0, filter.offset - filter.limit); doRefresh = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("Page %d", page + 1);
        ImGui::SameLine();
        ImGui::BeginDisabled(!connected || lastPageCount < filter.limit);
        if (ImGui::ArrowButton("##next", ImGuiDir_Right)) { filter.offset += filter.limit; doRefresh = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%d row%s%s", lastPageCount, lastPageCount == 1 ? "" : "s",
                            lastPageCount >= filter.limit ? " (more →)" : "");
    }

    ImGui::Separator();

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate;
    if (ImGui::BeginTable("##golist", 3, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 70.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
            if (specs->SpecsDirty && specs->SpecsCount > 0)
            {
                const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
                const int col = (s.ColumnIndex >= 0 && s.ColumnIndex < 3) ? s.ColumnIndex : 0;
                const bool asc = s.SortDirection != ImGuiSortDirection_Descending;
                if (col != filter.sortColumn || asc != filter.sortAsc)
                {
                    filter.sortColumn = col;
                    filter.sortAsc = asc;
                    requery(true);
                }
                specs->SpecsDirty = false;
            }

        const std::vector<EnumEntry>& types = GameObjectTypeValues();
        for (const GameObjectListEntry& e : entries)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(e.entry));
            char idlabel[32];
            std::snprintf(idlabel, sizeof(idlabel), "%u", e.entry);
            if (ImGui::Selectable(idlabel, e.entry == selectedId, ImGuiSelectableFlags_SpanAllColumns))
            {
                selectedId = e.entry;
                if (cb.onOpen)
                    cb.onOpen(e.entry);
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::TableNextColumn();
            if (const char* tl = LabelFor(types, e.type))
                ImGui::TextUnformatted(tl);
            else
                ImGui::Text("%u", e.type);
        }
        ImGui::EndTable();
    }

    ImGui::End();

    if (doRefresh && connected && cb.onRefresh)
        cb.onRefresh(filter);
}
} // namespace qe
