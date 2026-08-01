// Layer E (ui) — CreatureBrowserPanel. See header.

#include "editors/creature/CreatureBrowserPanel.h"

#include "imgui.h"

#include "data/LookupCache.h"
#include "ui/Widgets.h"
#include "util/Enums.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace we
{
void CreatureBrowserPanel::Draw(const std::vector<CreatureListEntry>& entries, bool connected,
                                LookupCache& lookups, CreatureBrowserCallbacks& cb)
{
    (void)lookups;
    if (!ImGui::Begin("Creature Browser"))
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

    // Type + rank filters.
    {
        const std::vector<EnumEntry>& types = CreatureTypeValues();
        std::string tp = "Any type";
        if (filter.type >= 0)
        {
            const char* l = LabelFor(types, static_cast<uint32_t>(filter.type));
            tp = l ? l : std::to_string(filter.type);
        }
        ImGui::SetNextItemWidth(150.0f);
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
        const std::vector<EnumEntry>& ranks = CreatureRankValues();
        std::string rk = "Any rank";
        if (filter.rank >= 0)
        {
            const char* l = LabelFor(ranks, static_cast<uint32_t>(filter.rank));
            rk = l ? l : std::to_string(filter.rank);
        }
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::BeginCombo("Rank", rk.c_str()))
        {
            if (ImGui::Selectable("Any", filter.rank < 0)) { filter.rank = -1; requery(true); }
            for (const EnumEntry& e : ranks)
                if (ImGui::Selectable(e.label, filter.rank == static_cast<int>(e.value)))
                {
                    filter.rank = static_cast<int>(e.value);
                    requery(true);
                }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            filter.text.clear();
            filter.type = -1;
            filter.rank = -1;
            filter.minLevel = 0;
            filter.maxLevel = 0;
            requery(true);
        }
    }

    // Paging.
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
    if (ImGui::BeginTable("##creaturelist", 5, flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 70.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Subname", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Lvl", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
            if (specs->SpecsDirty && specs->SpecsCount > 0)
            {
                static const int kColToSort[5] = {0, 1, 1, 2, 4};
                const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
                const int col = (s.ColumnIndex >= 0 && s.ColumnIndex < 5) ? kColToSort[s.ColumnIndex] : 0;
                const bool asc = s.SortDirection != ImGuiSortDirection_Descending;
                if (col != filter.sortColumn || asc != filter.sortAsc)
                {
                    filter.sortColumn = col;
                    filter.sortAsc = asc;
                    requery(true);
                }
                specs->SpecsDirty = false;
            }

        const std::vector<EnumEntry>& types = CreatureTypeValues();
        for (const CreatureListEntry& e : entries)
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
            ImGui::TextUnformatted(e.subname.c_str());
            ImGui::TableNextColumn();
            if (const char* tl = LabelFor(types, e.type))
                ImGui::TextUnformatted(tl);
            else
                ImGui::Text("%u", e.type);
            ImGui::TableNextColumn();
            if (e.minLevel == e.maxLevel)
                ImGui::Text("%u", e.minLevel);
            else
                ImGui::Text("%u-%u", e.minLevel, e.maxLevel);
        }
        ImGui::EndTable();
    }

    ImGui::End();

    if (doRefresh && connected && cb.onRefresh)
        cb.onRefresh(filter);
}
} // namespace we
