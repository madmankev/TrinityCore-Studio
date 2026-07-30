// Layer E (ui) — Creature Addon tab: creature_template_addon (1:1 optional).

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "data/LookupCache.h"

namespace qe
{
void DrawCreatureAddonTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    Creature& c = *ctx.creature;
    CreatureAddon& a = c.addon;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); c.addonDirty = true; a.present = true; };

    { bool p = a.present; if (ImGui::Checkbox("Has addon row (creature_template_addon)", &p)) { a.present = p; ctx.MarkChanged(); c.addonDirty = true; } }
    ImGui::Spacing();

    if (BeginFieldTable("qe_cr_addon"))
    {
        FieldRow("path_id", "waypoint_data path id (with MovementType 2)");
        if (InputU32("##pathid", a.pathId)) md();
        FieldRow("mount", "mount display id (CreatureDisplayInfo.dbc)");
        if (InputU32("##mount", a.mount)) md();
        FieldRow("MountCreatureID", "mount as a creature entry");
        if (IdNamePicker("##mountcreature", a.mountCreatureId, lk, RefKind::Creature)) md();
        FieldRow("StandState", "UnitStandStateType (0 stand, 1 sit, ...)");
        if (InputU8("##standstate", a.standState)) md();
        FieldRow("AnimTier", "animation tier");
        if (InputU8("##animtier", a.animTier)) md();
        FieldRow("VisFlags", "UNIT_VIS_FLAGS bitmask");
        if (InputU8("##visflags", a.visFlags)) md();
        FieldRow("SheathState", "0 unarmed, 1 melee, 2 ranged");
        if (InputU8("##sheathstate", a.sheathState)) md();
        FieldRow("PvPFlags", "UNIT_BYTE2_FLAG bitmask");
        if (InputU8("##pvpflags", a.pvpFlags)) md();
        FieldRow("emote", "Emotes.dbc id shown while idle");
        if (InputU32("##emote", a.emote)) md();
        FieldRow("visibilityDistanceType", "0 normal, 1 tiny .. (VisibilityDistanceType)");
        if (InputU8("##visdist", a.visibilityDistanceType)) md();
        FieldRow("auras", "space-separated spell ids applied on spawn");
        if (InputTextString("##auras", a.auras)) md();
        EndFieldTable();
    }
}
} // namespace qe
