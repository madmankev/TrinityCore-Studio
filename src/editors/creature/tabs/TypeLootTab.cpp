// Layer E (ui) — Creature Type & Loot tab: type/family/rank, loot ids, gold, credits.

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "util/Enums.h"
#include "data/LookupCache.h"

#include <cstdint>

namespace we
{
void DrawCreatureTypeLootTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    CreatureTemplate& t = ctx.creature->tmpl;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); ctx.creature->tmplDirty = true; };

    ImGui::SeparatorText("Type");
    if (BeginFieldTable("qe_cr_type"))
    {
        FieldRow("type", "creature_template.type");
        { uint32_t v = t.type; if (EnumCombo("##type", v, CreatureTypeValues())) { t.type = static_cast<uint8_t>(v); md(); } }

        FieldRow("family", "beast family (for tameable pets)");
        { uint32_t v = static_cast<uint32_t>(t.family); if (EnumCombo("##family", v, CreatureFamilyValues())) { t.family = static_cast<int8_t>(v); md(); } }

        FieldRow("rank", "normal / elite / rare / boss");
        { uint32_t v = t.rank; if (EnumCombo("##rank", v, CreatureRankValues())) { t.rank = static_cast<uint8_t>(v); md(); } }

        FieldRow("RacialLeader", "counts as a racial leader (city attack bonus)");
        { bool b = t.racialLeader != 0; if (ImGui::Checkbox("##racialleader", &b)) { t.racialLeader = b ? 1 : 0; md(); } }

        EndFieldTable();
    }

    ImGui::SeparatorText("Loot & Economy");
    if (BeginFieldTable("qe_cr_loot"))
    {
        FieldRow("lootid", "creature_loot_template.Entry (0 = none). Edit rows on the Loot tab.");
        if (InputU32("##lootid", t.lootId)) md();
        FieldRow("pickpocketloot", "pickpocketing_loot_template.Entry");
        if (InputU32("##pickpocketloot", t.pickpocketLoot)) md();
        FieldRow("skinloot", "skinning_loot_template.Entry");
        if (InputU32("##skinloot", t.skinLoot)) md();

        FieldRow("mingold", "min copper dropped");
        if (InputU32("##mingold", t.minGold)) md();
        FieldRow("maxgold", "max copper dropped");
        if (InputU32("##maxgold", t.maxGold)) md();

        FieldRow("KillCredit1", "credit given for a linked creature (creature entry)");
        if (IdNamePicker("##killcredit1", t.killCredit[0], lk, RefKind::Creature)) md();
        FieldRow("KillCredit2", "second kill-credit creature");
        if (IdNamePicker("##killcredit2", t.killCredit[1], lk, RefKind::Creature)) md();

        EndFieldTable();
    }
}
} // namespace we
