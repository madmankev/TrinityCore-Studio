// Layer E (ui) — Creature Movement tab: creature_template motion fields plus the
// optional creature_template_movement row (nullable fields; -1 = default/NULL).

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "ui/Enums.h"

#include <cfloat>
#include <cstdint>
#include <string>

namespace we
{
void DrawCreatureMovementTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    Creature& c = *ctx.creature;
    CreatureTemplate& t = c.tmpl;
    auto md = [&]() { ctx.MarkChanged(); c.tmplDirty = true; };
    auto mm = [&]() { ctx.MarkChanged(); c.movementDirty = true; c.movement.present = true; };

    ImGui::SeparatorText("Movement (creature_template)");
    if (BeginFieldTable("qe_cr_move"))
    {
        FieldRow("MovementType", "idle motion generator");
        { uint32_t v = t.movementType; if (EnumCombo("##movetype", v, MovementTypeValues())) { t.movementType = static_cast<uint8_t>(v); md(); } }
        FieldRow("speed_walk", "walk speed multiplier");
        if (InputFloatField("##speedwalk", t.speedWalk)) md();
        FieldRow("speed_run", "run speed multiplier");
        if (InputFloatField("##speedrun", t.speedRun)) md();
        FieldRow("HoverHeight", "hover height (1 = default)");
        if (InputFloatField("##hover", t.hoverHeight)) md();
        FieldRow("movementId", "creature_movement_override / waypoint path id");
        if (InputU32("##moveid", t.movementId)) md();
        EndFieldTable();
    }

    ImGui::SeparatorText("Movement Overrides (creature_template_movement)");
    { bool p = c.movement.present; if (ImGui::Checkbox("Has movement override row", &p)) { c.movement.present = p; ctx.MarkChanged(); c.movementDirty = true; } }
    ImGui::TextDisabled("(default) leaves the column NULL — the core uses its built-in default.");

    // Combo showing "(default)" for -1, else the enum label.
    auto moveCombo = [&](const char* id, int& field, const std::vector<EnumEntry>& tbl) {
        std::string preview = "(default)";
        if (field >= 0)
        {
            const char* l = LabelFor(tbl, static_cast<uint32_t>(field));
            preview = l ? l : std::to_string(field);
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo(id, preview.c_str()))
        {
            if (ImGui::Selectable("(default)", field < 0)) { field = -1; mm(); }
            for (const EnumEntry& e : tbl)
                if (ImGui::Selectable(e.label, field == static_cast<int>(e.value))) { field = static_cast<int>(e.value); mm(); }
            ImGui::EndCombo();
        }
    };
    // Tri-state bool: (default)/No/Yes.
    auto moveBool = [&](const char* id, int& field) {
        const char* preview = field < 0 ? "(default)" : (field == 0 ? "No" : "Yes");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo(id, preview))
        {
            if (ImGui::Selectable("(default)", field < 0)) { field = -1; mm(); }
            if (ImGui::Selectable("No", field == 0)) { field = 0; mm(); }
            if (ImGui::Selectable("Yes", field == 1)) { field = 1; mm(); }
            ImGui::EndCombo();
        }
    };

    if (BeginFieldTable("qe_cr_moveovr"))
    {
        FieldRow("Ground");  moveCombo("##ground", c.movement.ground, MoveGroundValues());
        FieldRow("Swim");    moveBool("##swim", c.movement.swim);
        FieldRow("Flight");  moveCombo("##flight", c.movement.flight, MoveFlightValues());
        FieldRow("Rooted");  moveBool("##rooted", c.movement.rooted);
        FieldRow("Chase");   moveCombo("##chase", c.movement.chase, MoveChaseValues());
        FieldRow("Random");  moveCombo("##random", c.movement.random, MoveRandomValues());
        FieldRow("InteractionPauseTimer", "ms; -1 = default");
        {
            int v = static_cast<int>(c.movement.interactionPauseTimer);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputInt("##ipt", &v, 0)) { c.movement.interactionPauseTimer = v < -1 ? -1 : v; mm(); }
        }
        EndFieldTable();
    }
}
} // namespace we
