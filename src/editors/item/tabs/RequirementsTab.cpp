// Layer E (ui) — Item Requirements tab: level/skill/spell/reputation/quest gating.

#include "editors/item/Tabs.h"
#include "editors/item/ItemEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Item.h"
#include "util/Enums.h"
#include "data/LookupCache.h"

#include <cstdint>

namespace qe
{
void DrawItemRequirementsTab(ItemEditorContext& ctx)
{
    if (!ctx.item)
    {
        ImGui::TextDisabled("No item loaded.");
        return;
    }
    ItemTemplate& t = ctx.item->tmpl;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); ctx.item->tmplDirty = true; };

    ImGui::SeparatorText("Level & Skill");
    if (BeginFieldTable("qe_item_req_level"))
    {
        FieldRow("RequiredLevel", "min player level to use");
        if (InputU8("##reqlevel", t.requiredLevel)) md();

        FieldRow("ItemLevel", "item level");
        if (InputU16("##ilevel", t.itemLevel)) md();

        FieldRow("RequiredSkill", "SkillLine.dbc");
        if (IdNamePicker("##reqskill", t.requiredSkill, lk, RefKind::Skill)) md();

        FieldRow("RequiredSkillRank", "skill points required");
        if (InputU16("##reqskillrank", t.requiredSkillRank)) md();

        FieldRow("requiredspell", "spell that must be known");
        if (IdNamePicker("##reqspell", t.requiredSpell, lk, RefKind::Spell)) md();

        EndFieldTable();
    }

    ImGui::SeparatorText("Reputation & Rank");
    if (BeginFieldTable("qe_item_req_rep"))
    {
        FieldRow("RequiredReputationFaction", "Faction.dbc");
        if (IdNamePicker("##reqrepfac", t.requiredReputationFaction, lk, RefKind::Faction)) md();

        FieldRow("RequiredReputationRank", "rep rank required");
        if (InputU16("##reqreprank", t.requiredReputationRank)) md();

        FieldRow("RequiredCityRank", "legacy");
        if (InputU32("##reqcityrank", t.requiredCityRank)) md();

        FieldRow("requiredhonorrank", "legacy honor rank");
        if (InputU32("##reqhonorrank", t.requiredHonorRank)) md();

        EndFieldTable();
    }

    ImGui::SeparatorText("Location / Quest / Lock");
    if (BeginFieldTable("qe_item_req_misc"))
    {
        FieldRow("area", "AreaTable.dbc restriction (0 = none)");
        if (IdNamePicker("##area", t.area, lk, RefKind::Area)) md();

        FieldRow("Map", "Map.dbc restriction (0 = none)");
        if (InputI16("##map", t.map)) md();

        FieldRow("startquest", "quest this item starts");
        if (IdNamePicker("##startquest", t.startQuest, lk, RefKind::Quest)) md();

        FieldRow("lockid", "Lock.dbc (locked items / keys)");
        if (InputU32("##lockid", t.lockId)) md();

        EndFieldTable();
    }

    ImGui::SeparatorText("Allowable Classes");
    {
        uint32_t v = static_cast<uint32_t>(t.allowableClass);
        if (FlagCheckboxGrid("##allowclass", v, ClassMaskBits()))
        {
            t.allowableClass = static_cast<int32_t>(v);
            md();
        }
        ImGui::TextDisabled("(-1 / all bits = usable by every class)");
    }

    ImGui::SeparatorText("Allowable Races");
    {
        uint32_t v = static_cast<uint32_t>(t.allowableRace);
        if (FlagCheckboxGrid("##allowrace", v, RaceMaskBits()))
        {
            t.allowableRace = static_cast<int32_t>(v);
            md();
        }
        ImGui::TextDisabled("(-1 / all bits = usable by every race)");
    }
}
} // namespace qe
