// Layer E (ui) — QuestBrowserPanel. See QuestBrowserPanel.h.

#include "editors/quest/QuestBrowserPanel.h"

#include "imgui.h"

#include "data/LookupCache.h"
#include "ui/Widgets.h"
#include "ui/Enums.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <vector>

namespace we
{
void QuestBrowserPanel::Draw(const std::vector<QuestListEntry>& entries, bool connected,
                             LookupCache& lookups, BrowserCallbacks& cb)
{
    if (!ImGui::Begin("Quest Browser"))
    {
        ImGui::End();
        return;
    }

    lastPageCount = static_cast<int>(entries.size());
    bool doRefresh = false;   // deferred to end-of-frame (never mutate `entries` mid-draw)
    auto requery = [&](bool resetPage)
    {
        if (resetPage)
            filter.offset = 0;
        doRefresh = true;
    };

    // --- Text search -------------------------------------------------------
    ImGui::SetNextItemWidth(220.0f);
    if (InputTextString("Filter (id or title)", filter.text, ImGuiInputTextFlags_EnterReturnsTrue))
        requery(true);
    ImGui::SameLine();
    ImGui::BeginDisabled(!connected);
    if (ImGui::Button("Search"))
        requery(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Checkbox("in text", &filter.searchBody))
        requery(true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Also search description / objective text, not just the title.");

    // --- Type + level filters ---------------------------------------------
    {
        // QuestInfoID type combo: "Any" + the known QuestInfo values.
        const std::vector<EnumEntry>& infos = QuestInfoIDValues();
        std::string preview = "Any";
        if (filter.questInfoId >= 0)
        {
            const char* lbl = LabelFor(infos, static_cast<uint32_t>(filter.questInfoId));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d%s%s", filter.questInfoId, lbl ? " - " : "",
                          lbl ? lbl : "");
            preview = buf;
        }
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("Type", preview.c_str()))
        {
            if (ImGui::Selectable("Any", filter.questInfoId < 0))
            {
                filter.questInfoId = -1;
                requery(true);
            }
            for (const EnumEntry& e : infos)
            {
                char buf[80];
                std::snprintf(buf, sizeof(buf), "%u - %s", e.value, e.label);
                if (ImGui::Selectable(buf, filter.questInfoId == static_cast<int>(e.value)))
                {
                    filter.questInfoId = static_cast<int>(e.value);
                    requery(true);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("Min lvl", &filter.minLevel, 0))
            requery(true);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("Max lvl", &filter.maxLevel, 0))
            requery(true);
        if (filter.minLevel < 0) filter.minLevel = 0;
        if (filter.maxLevel < 0) filter.maxLevel = 0;

        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            filter.text.clear();
            filter.searchBody = false;
            filter.questInfoId = -1;
            filter.hasSortId = false;
            filter.minLevel = 0;
            filter.maxLevel = 0;
            requery(true);
        }
    }

    // --- Paging ------------------------------------------------------------
    {
        const int page = filter.limit > 0 ? filter.offset / filter.limit : 0;
        ImGui::BeginDisabled(!connected || filter.offset <= 0);
        if (ImGui::ArrowButton("##prev", ImGuiDir_Left))
        {
            filter.offset = std::max(0, filter.offset - filter.limit);
            doRefresh = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("Page %d", page + 1);
        ImGui::SameLine();
        // Next is available only when the current page was full (more may follow).
        ImGui::BeginDisabled(!connected || lastPageCount < filter.limit);
        if (ImGui::ArrowButton("##next", ImGuiDir_Right))
        {
            filter.offset += filter.limit;
            doRefresh = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%d row%s%s", lastPageCount, lastPageCount == 1 ? "" : "s",
                            lastPageCount >= filter.limit ? " (more →)" : "");
    }

    ImGui::Separator();

    // --- Table -------------------------------------------------------------
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate;
    const ImVec2 tableSize(0.0f, ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginTable("##questlist", 4, flags, tableSize))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 70.0f);
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Lvl", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn("Zone / Sort", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();

        // Header-click sorting drives a server-side re-query (so it sorts the whole
        // result set, not just the current page). Column index maps to filter.sortColumn.
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
        {
            if (specs->SpecsDirty && specs->SpecsCount > 0)
            {
                const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
                const int col = s.ColumnIndex; // 0 ID,1 Title,2 Lvl,3 Sort -> same as filter
                const bool asc = s.SortDirection != ImGuiSortDirection_Descending;
                if (col != filter.sortColumn || asc != filter.sortAsc)
                {
                    filter.sortColumn = col;
                    filter.sortAsc = asc;
                    requery(true);
                }
                specs->SpecsDirty = false;
            }
        }

        for (const QuestListEntry& e : entries)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::PushID(static_cast<int>(e.id));
            char idlabel[32];
            std::snprintf(idlabel, sizeof(idlabel), "%u", e.id);
            const bool selected = (e.id == selectedId);
            if (ImGui::Selectable(idlabel, selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                selectedId = e.id;
                if (cb.onOpen)
                    cb.onOpen(e.id);
            }
            ImGui::PopID();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.title.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", static_cast<int>(e.questLevel));
            ImGui::TableNextColumn();
            // Resolve QuestSortID to a zone name when possible (>0 = AreaTable id).
            std::string zone;
            if (e.questSortId > 0)
                zone = lookups.NameOfArea(static_cast<uint32_t>(e.questSortId));
            if (!zone.empty())
                ImGui::Text("%d - %s", static_cast<int>(e.questSortId), zone.c_str());
            else
                ImGui::Text("%d", static_cast<int>(e.questSortId));
        }
        ImGui::EndTable();
    }

    ImGui::End();

    if (doRefresh && connected && cb.onRefresh)
        cb.onRefresh(filter);
}
} // namespace we
