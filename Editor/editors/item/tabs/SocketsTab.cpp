// Layer E (ui) — Item Sockets tab: 3 sockets + bonus/gem/disenchant.

#include "editors/item/Tabs.h"
#include "editors/item/ItemEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Item.h"
#include "ui/Enums.h"

#include <cstdint>
#include <cstdio>

namespace we
{
void DrawItemSocketsTab(ItemEditorContext& ctx)
{
    if (!ctx.item)
    {
        ImGui::TextDisabled("No item loaded.");
        return;
    }
    ItemTemplate& t = ctx.item->tmpl;
    auto md = [&]() { ctx.MarkChanged(); ctx.item->tmplDirty = true; };

    ImGui::SeparatorText("Sockets");
    if (BeginFieldTable("qe_item_sockets"))
    {
        for (int i = 0; i < 3; ++i)
        {
            ImGui::PushID(i);
            char colL[24], conL[24];
            std::snprintf(colL, sizeof(colL), "socketColor_%d", i + 1);
            std::snprintf(conL, sizeof(conL), "socketContent_%d", i + 1);

            FieldRow(colL, "socket color");
            { uint32_t v = t.socketColor[i]; if (EnumCombo("##color", v, SocketColorValues())) { t.socketColor[i] = static_cast<uint8_t>(v); md(); } }

            FieldRow(conL, "socket content");
            if (InputI32("##content", t.socketContent[i])) md();
            ImGui::PopID();
        }
        EndFieldTable();
    }

    ImGui::SeparatorText("Bonus / Gem / Disenchant");
    if (BeginFieldTable("qe_item_socketmisc"))
    {
        FieldRow("socketBonus", "SpellItemEnchantment.dbc (color-match bonus)");
        if (InputI32("##socketbonus", t.socketBonus)) md();

        FieldRow("GemProperties", "GemProperties.dbc (if item is a gem)");
        if (InputI32("##gemprops", t.gemProperties)) md();

        FieldRow("RequiredDisenchantSkill", "enchanting skill to disenchant (-1 = cannot)");
        if (InputI16("##reqdisenchant", t.requiredDisenchantSkill)) md();

        FieldRow("DisenchantID", "disenchant_loot_template id");
        if (InputU32("##disenchantid", t.disenchantID)) md();

        EndFieldTable();
    }
}
} // namespace we
