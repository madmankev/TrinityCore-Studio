// Layer E (ui) — Creature Stats tab: level range, expansion, class, stat modifiers.

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "util/Enums.h"

#include <cstdint>

namespace we
{
void DrawCreatureStatsTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    CreatureTemplate& t = ctx.creature->tmpl;
    auto md = [&]() { ctx.MarkChanged(); ctx.creature->tmplDirty = true; };

    ImGui::SeparatorText("Level & Class");
    if (BeginFieldTable("qe_cr_level"))
    {
        FieldRow("minlevel", "creature_template.minlevel");
        if (InputU8("##minlevel", t.minLevel)) md();

        FieldRow("maxlevel", "creature_template.maxlevel");
        if (InputU8("##maxlevel", t.maxLevel)) md();

        FieldRow("exp (expansion)", "0 Classic / 1 TBC / 2 WotLK (selects base health/damage)");
        { uint32_t v = static_cast<uint32_t>(t.exp); if (EnumCombo("##exp", v, CreatureExpansionValues())) { t.exp = static_cast<int16_t>(v); md(); } }

        FieldRow("unit_class", "power/stat class (1 Warrior, 2 Paladin, 4 Rogue, 8 Mage)");
        { uint32_t v = t.unitClass; if (EnumCombo("##unitclass", v, UnitClassValues())) { t.unitClass = static_cast<uint8_t>(v); md(); } }

        FieldRow("RegenHealth", "regenerate health out of combat");
        { bool b = t.regenHealth != 0; if (ImGui::Checkbox("##regenhealth", &b)) { t.regenHealth = b ? 1 : 0; md(); } }

        EndFieldTable();
    }

    ImGui::SeparatorText("Stat Modifiers");
    if (BeginFieldTable("qe_cr_mods"))
    {
        FieldRow("HealthModifier", "multiplier on base health");
        if (InputFloatField("##hpmod", t.healthModifier)) md();
        FieldRow("ManaModifier", "multiplier on base mana");
        if (InputFloatField("##manamod", t.manaModifier)) md();
        FieldRow("ArmorModifier", "multiplier on base armor");
        if (InputFloatField("##armormod", t.armorModifier)) md();
        FieldRow("DamageModifier", "multiplier on base damage");
        if (InputFloatField("##dmgmod", t.damageModifier)) md();
        FieldRow("ExperienceModifier", "multiplier on kill XP");
        if (InputFloatField("##xpmod", t.experienceModifier)) md();
        EndFieldTable();
    }
}
} // namespace we
