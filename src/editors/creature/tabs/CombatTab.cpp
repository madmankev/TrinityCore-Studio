// Layer E (ui) — Creature Combat tab: faction, attack timing, immunities,
// resistances (creature_template_resistance) and spells (creature_template_spell).

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "util/Enums.h"
#include "data/LookupCache.h"

#include <cstdint>
#include <cstdio>

namespace qe
{
void DrawCreatureCombatTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    Creature& c = *ctx.creature;
    CreatureTemplate& t = c.tmpl;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); c.tmplDirty = true; };

    ImGui::SeparatorText("Faction & Attack");
    if (BeginFieldTable("qe_cr_combat"))
    {
        FieldRow("faction", "FactionTemplate.dbc id");
        if (IdNamePicker("##faction", t.faction, lk, RefKind::FactionTemplate)) md();

        FieldRow("BaseAttackTime", "melee swing time (ms)");
        if (InputU32("##baseatk", t.baseAttackTime)) md();
        FieldRow("RangeAttackTime", "ranged attack time (ms)");
        if (InputU32("##rangeatk", t.rangeAttackTime)) md();
        FieldRow("BaseVariance", "melee damage variance");
        if (InputFloatField("##basevar", t.baseVariance)) md();
        FieldRow("RangeVariance", "ranged damage variance");
        if (InputFloatField("##rangevar", t.rangeVariance)) md();

        FieldRow("dmgschool", "melee damage school");
        { uint32_t v = static_cast<uint32_t>(t.dmgSchool); if (EnumCombo("##dmgschool", v, DamageSchoolValues())) { t.dmgSchool = static_cast<int8_t>(v); md(); } }

        FieldRow("mechanic_immune_mask", "Mechanics.dbc bitmask");
        if (InputU32("##mechimmune", t.mechanicImmuneMask)) md();
        FieldRow("spell_school_immune_mask", "spell school immunity bitmask");
        if (InputU32("##schoolimmune", t.spellSchoolImmuneMask)) md();

        EndFieldTable();
    }

    ImGui::SeparatorText("Resistances (creature_template_resistance)");
    {
        static const char* kSchools[6] = {"Holy", "Fire", "Nature", "Frost", "Shadow", "Arcane"};
        if (BeginFieldTable("qe_cr_resist"))
        {
            for (int i = 0; i < 6; ++i)
            {
                FieldRow(kSchools[i], nullptr);
                ImGui::PushID(i);
                if (InputI16("##res", c.resistances[i])) { ctx.MarkChanged(); c.resistDirty = true; }
                ImGui::PopID();
            }
            EndFieldTable();
        }
    }

    ImGui::SeparatorText("Spells (creature_template_spell)");
    if (BeginFieldTable("qe_cr_spells"))
    {
        for (int i = 0; i < 8; ++i)
        {
            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "spell %d", i);
            FieldRow(lbl, "Spell.dbc id (0 = none)");
            ImGui::PushID(i);
            if (IdNamePicker("##spell", c.spells[i], lk, RefKind::Spell)) { ctx.MarkChanged(); c.spellsDirty = true; }
            ImGui::PopID();
        }
        EndFieldTable();
    }
}
} // namespace qe
