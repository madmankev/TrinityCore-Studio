// Layer E (ui) — Creature Equipment tab: creature_equip_template sets (main/off/ranged).

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "data/LookupCache.h"

#include <cfloat>

namespace we
{
void DrawCreatureEquipmentTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    Creature& c = *ctx.creature;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); c.equipsDirty = true; };

    ImGui::TextDisabled("A spawn picks one equipment set by ID (creature.equipment_id).");
    if (ImGui::Button("Add equipment set"))
    {
        CreatureEquip e;
        e.id = static_cast<uint8_t>(c.equips.size() + 1);
        c.equips.push_back(e);
        md();
    }

    int removeIdx = -1;
    for (int i = 0; i < static_cast<int>(c.equips.size()); ++i)
    {
        ImGui::PushID(i);
        CreatureEquip& e = c.equips[i];
        ImGui::Spacing();
        ImGui::SeparatorText("");
        ImGui::SameLine();
        if (ImGui::Button("Remove set"))
            removeIdx = i;

        if (BeginFieldTable("qe_cr_equipset"))
        {
            FieldRow("ID", "equipment set number (creature.equipment_id)");
            if (InputU8("##setid", e.id)) md();
            FieldRow("Main Hand (ItemID1)", "item entry");
            if (IdNamePicker("##item1", e.itemId[0], lk, RefKind::Item)) md();
            FieldRow("Off Hand (ItemID2)", "item entry");
            if (IdNamePicker("##item2", e.itemId[1], lk, RefKind::Item)) md();
            FieldRow("Ranged (ItemID3)", "item entry");
            if (IdNamePicker("##item3", e.itemId[2], lk, RefKind::Item)) md();
            EndFieldTable();
        }
        ImGui::PopID();
    }
    if (removeIdx >= 0)
    {
        c.equips.erase(c.equips.begin() + removeIdx);
        md();
    }
}
} // namespace we
