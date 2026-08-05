// Layer E (ui) — Item Stats tab: StatsCount + 10 stat_type/stat_value pairs, scaling.

#include "editors/item/Tabs.h"
#include "editors/item/ItemEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Item.h"
#include "ui/Enums.h"

#include <cfloat>
#include <cstdint>

namespace we
{
void DrawItemStatsTab(ItemEditorContext& ctx)
{
    if (!ctx.item)
    {
        ImGui::TextDisabled("No item loaded.");
        return;
    }
    ItemTemplate& t = ctx.item->tmpl;
    auto md = [&]() { ctx.MarkChanged(); ctx.item->tmplDirty = true; };

    if (BeginFieldTable("qe_item_statscount"))
    {
        FieldRow("StatsCount", "number of active stat entries (0-10)");
        if (InputU8("##statscount", t.statsCount)) md();
        EndFieldTable();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Stats:");
    if (ImGui::BeginTable("##itemstats", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("Stat Type", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 10; ++i)
        {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i + 1);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            { uint32_t v = t.statType[i]; if (EnumCombo("##type", v, ItemStatTypeValues())) { t.statType[i] = static_cast<uint8_t>(v); md(); } }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputI16("##value", t.statValue[i])) md();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Scaling (heirlooms)");
    if (BeginFieldTable("qe_item_scaling"))
    {
        FieldRow("ScalingStatDistribution", "ScalingStatDistribution.dbc");
        if (InputI16("##ssd", t.scalingStatDistribution)) md();

        FieldRow("ScalingStatValue", "ScalingStatValues.dbc column mask");
        if (InputU32("##ssv", t.scalingStatValue)) md();

        EndFieldTable();
    }
}
} // namespace we
