// Layer E (ui) — Creature Scripting tab: AI, script, vehicle, pet, difficulty links.

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "data/LookupCache.h"

#include <cstdio>

namespace qe
{
void DrawCreatureScriptingTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    CreatureTemplate& t = ctx.creature->tmpl;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); ctx.creature->tmplDirty = true; };

    ImGui::SeparatorText("AI & Script");
    if (BeginFieldTable("qe_cr_script"))
    {
        FieldRow("AIName", "e.g. SmartAI, AggressorAI (blank = default)");
        if (InputTextString("##ainame", t.aiName)) md();
        FieldRow("ScriptName", "attached C++ script (blank = none)");
        if (InputTextString("##scriptname", t.scriptName)) md();
        FieldRow("VehicleId", "vehicle.dbc id (0 = not a vehicle)");
        if (InputU32("##vehicleid", t.vehicleId)) md();
        FieldRow("PetSpellDataId", "CreatureSpellData.dbc (pet action bar)");
        if (InputU32("##petspell", t.petSpellDataId)) md();
        EndFieldTable();
    }

    ImGui::SeparatorText("Difficulty Entries (heroic/other modes)");
    if (BeginFieldTable("qe_cr_difficulty"))
    {
        for (int i = 0; i < 3; ++i)
        {
            char lbl[24];
            std::snprintf(lbl, sizeof(lbl), "difficulty_entry_%d", i + 1);
            FieldRow(lbl, "creature entry used in a harder difficulty (0 = none)");
            ImGui::PushID(i);
            if (IdNamePicker("##diff", t.difficultyEntry[i], lk, RefKind::Creature)) md();
            ImGui::PopID();
        }
        EndFieldTable();
    }
}
} // namespace qe
