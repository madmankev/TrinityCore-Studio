// Layer E (ui) — Creature Flags tab: npcflag / unit_flags / unit_flags2 /
// dynamicflags / type_flags / flags_extra.

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "util/Enums.h"

namespace we
{
void DrawCreatureFlagsTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    CreatureTemplate& t = ctx.creature->tmpl;
    auto md = [&]() { ctx.MarkChanged(); ctx.creature->tmplDirty = true; };

    ImGui::SeparatorText("npcflag (NPC roles)");
    if (FlagCheckboxGrid("##npcflag", t.npcflag, CreatureNpcFlagBits(), 2)) md();

    ImGui::SeparatorText("unit_flags");
    if (FlagCheckboxGrid("##unitflags", t.unitFlags, UnitFlagBits(), 2)) md();

    ImGui::SeparatorText("unit_flags2");
    if (FlagCheckboxGrid("##unitflags2", t.unitFlags2, UnitFlags2Bits(), 2)) md();

    ImGui::SeparatorText("type_flags");
    if (FlagCheckboxGrid("##typeflags", t.typeFlags, CreatureTypeFlagBits(), 2)) md();

    ImGui::SeparatorText("flags_extra (TrinityCore)");
    if (FlagCheckboxGrid("##flagsextra", t.flagsExtra, CreatureFlagsExtraBits(), 2)) md();

    ImGui::SeparatorText("dynamicflags");
    if (BeginFieldTable("qe_cr_dynflags"))
    {
        FieldRow("dynamicflags", "UNIT_DYNFLAG_* bitmask (raw value)");
        if (InputU32("##dynflags", t.dynamicFlags)) md();
        EndFieldTable();
    }
}
} // namespace we
