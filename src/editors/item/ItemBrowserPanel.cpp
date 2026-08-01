// Layer E (ui) — ItemBrowserPanel. See ItemBrowserPanel.h.

#include "editors/item/ItemBrowserPanel.h"

#include "imgui.h"

#include "data/LookupCache.h"
#include "ui/Widgets.h"
#include "util/Enums.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace we
{
void ItemBrowserPanel::Draw(const std::vector<ItemListEntry>& entries, bool connected,
                            LookupCache& lookups, ItemBrowserCallbacks& cb)
{
    (void)lookups;
    if (!ImGui::Begin("Item Browser"))
    {
        ImGui::End();
        return;
    }

    lastPageCount = static_cast<int>(entries.size());
    bool doRefresh = false;
    auto requery = [&](bool resetPage)
    {
        if (resetPage)
            filter.offset = 0;
        doRefresh = true;
    };

    // --- Text search -------------------------------------------------------
    ImGui::SetNextItemWidth(220.0f);
    if (InputTextString("Filter (id or name)", filter.text, ImGuiInputTextFlags_EnterReturnsTrue))
        requery(true);
    ImGui::SameLine();
    ImGui::BeginDisabled(!connected);
    if (ImGui::Button("Search"))
        requery(true);
    ImGui::EndDisabled();

    // --- Class + quality filters ------------------------------------------
    {
        const std::vector<EnumEntry>& classes = ItemClassValues();
        std::string preview = "Any class";
        if (filter.itemClass >= 0)
        {
            const char* lbl = LabelFor(classes, static_cast<uint32_t>(filter.itemClass));
            preview = lbl ? lbl : std::to_string(filter.itemClass);
        }
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("Class", preview.c_str()))
        {
            if (ImGui::Selectable("Any", filter.itemClass < 0))
            {
                filter.itemClass = -1;
                filter.subclass = -1;
                requery(true);
            }
            for (const EnumEntry& e : classes)
                if (ImGui::Selectable(e.label, filter.itemClass == static_cast<int>(e.value)))
                {
                    filter.itemClass = static_cast<int>(e.value);
                    filter.subclass = -1;
                    requery(true);
                }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        const std::vector<EnumEntry>& quals = ItemQualityValues();
        std::string qprev = "Any quality";
        if (filter.quality >= 0)
        {
            const char* lbl = LabelFor(quals, static_cast<uint32_t>(filter.quality));
            qprev = lbl ? lbl : std::to_string(filter.quality);
        }
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("Quality", qprev.c_str()))
        {
            if (ImGui::Selectable("Any", filter.quality < 0))
            {
                filter.quality = -1;
                requery(true);
            }
            for (const EnumEntry& e : quals)
                if (ImGui::Selectable(e.label, filter.quality == static_cast<int>(e.value)))
                {
                    filter.quality = static_cast<int>(e.value);
                    requery(true);
                }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            filter.text.clear();
            filter.itemClass = -1;
            filter.subclass = -1;
            filter.quality = -1;
            filter.inventoryType = -1;
            filter.minItemLevel = 0;
            filter.maxItemLevel = 0;
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
    if (ImGui::BeginTable("##itemlist", 5, flags, tableSize))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 70.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Qual", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("iLvl", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableHeadersRow();

        // Header-click sort -> server-side re-query. Column index maps to
        // filter.sortColumn as: 0 entry, 1 name, 2 class, 4 Quality, 6 ItemLevel.
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
        {
            if (specs->SpecsDirty && specs->SpecsCount > 0)
            {
                static const int kColToSort[5] = {0, 1, 2, 4, 6};
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
        }

        const std::vector<EnumEntry>& classes = ItemClassValues();
        const std::vector<EnumEntry>& quals = ItemQualityValues();
        for (const ItemListEntry& e : entries)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::PushID(static_cast<int>(e.entry));
            char idlabel[32];
            std::snprintf(idlabel, sizeof(idlabel), "%u", e.entry);
            const bool selected = (e.entry == selectedId);
            if (ImGui::Selectable(idlabel, selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                selectedId = e.entry;
                if (cb.onOpen)
                    cb.onOpen(e.entry);
            }
            ImGui::PopID();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::TableNextColumn();
            if (const char* cl = LabelFor(classes, e.cls))
                ImGui::TextUnformatted(cl);
            else
                ImGui::Text("%u", e.cls);
            ImGui::TableNextColumn();
            if (const char* ql = LabelFor(quals, e.quality))
                ImGui::TextUnformatted(ql);
            else
                ImGui::Text("%u", e.quality);
            ImGui::TableNextColumn();
            ImGui::Text("%u", e.itemLevel);
        }
        ImGui::EndTable();
    }

    ImGui::End();

    if (doRefresh && connected && cb.onRefresh)
        cb.onRefresh(filter);
}
} // namespace we
