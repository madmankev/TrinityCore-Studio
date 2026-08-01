// Layer E (ui) — Item Spells tab: 5 spell slots (item_template spellid_1..5 groups).

#include "editors/item/Tabs.h"
#include "editors/item/ItemEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Item.h"
#include "util/Enums.h"
#include "data/LookupCache.h"

#include <cstdint>
#include <cstdio>

namespace we
{
void DrawItemSpellsTab(ItemEditorContext& ctx)
{
    if (!ctx.item)
    {
        ImGui::TextDisabled("No item loaded.");
        return;
    }
    ItemTemplate& t = ctx.item->tmpl;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); ctx.item->tmplDirty = true; };

    for (int i = 0; i < 5; ++i)
    {
        ImGui::PushID(i);
        char hdr[32];
        std::snprintf(hdr, sizeof(hdr), "Spell slot %d", i + 1);
        ImGui::SeparatorText(hdr);

        if (BeginFieldTable("qe_item_spellslot"))
        {
            FieldRow("spellid", "Spell.dbc");
            if (IdNamePicker("##spellid", t.spellId[i], lk, RefKind::Spell)) md();

            FieldRow("spelltrigger", "ItemSpelltriggerType");
            { uint32_t v = t.spellTrigger[i]; if (EnumCombo("##trigger", v, SpellTriggerValues())) { t.spellTrigger[i] = static_cast<uint8_t>(v); md(); } }

            FieldRow("spellcharges", "charges (negative = consumed on use)");
            if (InputI16("##charges", t.spellCharges[i])) md();

            FieldRow("spellppmRate", "procs per minute");
            if (InputFloatField("##ppm", t.spellPpmRate[i])) md();

            FieldRow("spellcooldown", "ms (-1 = none)");
            if (InputI32("##cooldown", t.spellCooldown[i])) md();

            FieldRow("spellcategory", "SpellCategory.dbc");
            if (InputU16("##category", t.spellCategory[i])) md();

            FieldRow("spellcategorycooldown", "ms (-1 = none)");
            if (InputI32("##catcooldown", t.spellCategoryCooldown[i])) md();

            EndFieldTable();
        }
        ImGui::PopID();
    }
}
} // namespace we
