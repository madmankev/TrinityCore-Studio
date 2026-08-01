// Layer E (ui) — Item Weapon/Armor tab: damage, speed, armor, resistances.

#include "editors/item/Tabs.h"
#include "editors/item/ItemEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Item.h"
#include "util/Enums.h"

#include <cstdint>

namespace we
{
void DrawItemWeaponArmorTab(ItemEditorContext& ctx)
{
    if (!ctx.item)
    {
        ImGui::TextDisabled("No item loaded.");
        return;
    }
    ItemTemplate& t = ctx.item->tmpl;
    auto md = [&]() { ctx.MarkChanged(); ctx.item->tmplDirty = true; };

    ImGui::SeparatorText("Weapon Damage");
    if (BeginFieldTable("qe_item_damage"))
    {
        for (int i = 0; i < 2; ++i)
        {
            ImGui::PushID(i);
            const char* minL = i == 0 ? "dmg_min1" : "dmg_min2";
            const char* maxL = i == 0 ? "dmg_max1" : "dmg_max2";
            const char* typL = i == 0 ? "dmg_type1" : "dmg_type2";

            FieldRow(minL, "minimum damage");
            if (InputFloatField("##min", t.dmgMin[i])) md();

            FieldRow(maxL, "maximum damage");
            if (InputFloatField("##max", t.dmgMax[i])) md();

            FieldRow(typL, "damage school");
            { uint32_t v = t.dmgType[i]; if (EnumCombo("##type", v, DamageSchoolValues())) { t.dmgType[i] = static_cast<uint8_t>(v); md(); } }
            ImGui::PopID();
        }
        EndFieldTable();
    }

    ImGui::SeparatorText("Speed & Ranged");
    if (BeginFieldTable("qe_item_speed"))
    {
        FieldRow("delay", "weapon swing speed (ms)");
        if (InputU16("##delay", t.delay)) md();

        FieldRow("ammo_type", "arrow / bullet");
        { uint32_t v = t.ammoType; if (EnumCombo("##ammo", v, AmmoTypeValues())) { t.ammoType = static_cast<uint8_t>(v); md(); } }

        FieldRow("RangedModRange", "ranged weapon range mod");
        if (InputFloatField("##rangedmod", t.rangedModRange)) md();

        EndFieldTable();
    }

    ImGui::SeparatorText("Armor & Durability");
    if (BeginFieldTable("qe_item_armor"))
    {
        FieldRow("armor", "armor value");
        if (InputU16("##armor", t.armor)) md();

        FieldRow("block", "shield block value");
        if (InputU32("##block", t.block)) md();

        FieldRow("MaxDurability", "max durability");
        if (InputU16("##maxdur", t.maxDurability)) md();

        FieldRow("ArmorDamageModifier", "armor damage modifier");
        if (InputFloatField("##armormod", t.armorDamageModifier)) md();

        EndFieldTable();
    }

    ImGui::SeparatorText("Resistances");
    if (BeginFieldTable("qe_item_resist"))
    {
        FieldRow("holy_res"); if (InputU8("##holy", t.holyRes)) md();
        FieldRow("fire_res"); if (InputU8("##fire", t.fireRes)) md();
        FieldRow("nature_res"); if (InputU8("##nature", t.natureRes)) md();
        FieldRow("frost_res"); if (InputU8("##frost", t.frostRes)) md();
        FieldRow("shadow_res"); if (InputU8("##shadow", t.shadowRes)) md();
        FieldRow("arcane_res"); if (InputU8("##arcane", t.arcaneRes)) md();
        EndFieldTable();
    }
}
} // namespace we
