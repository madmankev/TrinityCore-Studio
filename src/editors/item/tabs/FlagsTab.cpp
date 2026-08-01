// Layer E (ui) — Item Flags tab: Flags / FlagsExtra / flagsCustom / BagFamily.

#include "editors/item/Tabs.h"
#include "editors/item/ItemEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Item.h"
#include "util/Enums.h"

#include <cstdint>

namespace we
{
void DrawItemFlagsTab(ItemEditorContext& ctx)
{
    if (!ctx.item)
    {
        ImGui::TextDisabled("No item loaded.");
        return;
    }
    ItemTemplate& t = ctx.item->tmpl;
    auto md = [&]() { ctx.MarkChanged(); ctx.item->tmplDirty = true; };

    ImGui::SeparatorText("Flags (ItemFlags)");
    if (FlagCheckboxGrid("##flags", t.flags, ItemFlagsBits(), 2))
        md();

    ImGui::SeparatorText("FlagsExtra (ItemFlags2)");
    if (FlagCheckboxGrid("##flagsextra", t.flagsExtra, ItemFlagsExtraBits(), 2))
        md();

    ImGui::SeparatorText("flagsCustom (TrinityCore)");
    if (FlagCheckboxGrid("##flagscustom", t.flagsCustom, ItemFlagsCustomBits(), 1))
        md();

    ImGui::SeparatorText("BagFamily");
    {
        uint32_t v = static_cast<uint32_t>(t.bagFamily);
        if (FlagCheckboxGrid("##bagfamily", v, BagFamilyBits(), 2))
        {
            t.bagFamily = static_cast<int32_t>(v);
            md();
        }
    }
}
} // namespace we
