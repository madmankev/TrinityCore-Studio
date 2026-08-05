// Layer E (ui) — GameObject Addon tab: gameobject_template_addon (1:1 optional).

#include "editors/gameobject/Tabs.h"
#include "editors/gameobject/GameObjectEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/GameObject.h"
#include "ui/Enums.h"
#include "data/LookupCache.h"

#include <cstdio>

namespace we
{
void DrawGameObjectAddonTab(GameObjectEditorContext& ctx)
{
    if (!ctx.go)
    {
        ImGui::TextDisabled("No gameobject loaded.");
        return;
    }
    GameObject& go = *ctx.go;
    GameObjectAddon& a = go.addon;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); go.addonDirty = true; a.present = true; };

    { bool p = a.present; if (ImGui::Checkbox("Has addon row (gameobject_template_addon)", &p)) { a.present = p; ctx.MarkChanged(); go.addonDirty = true; } }
    ImGui::Spacing();

    if (BeginFieldTable("qe_go_addon"))
    {
        FieldRow("faction", "FactionTemplate.dbc id (0 = none)");
        if (IdNamePicker("##faction", a.faction, lk, RefKind::FactionTemplate)) md();

        FieldRow("mingold", "min copper (money-container GOs)");
        if (InputU32("##mingold", a.minGold)) md();
        FieldRow("maxgold", "max copper");
        if (InputU32("##maxgold", a.maxGold)) md();

        for (int i = 0; i < 4; ++i)
        {
            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "artkit%d", i);
            FieldRow(lbl, "GameObjectArtKit.dbc id");
            ImGui::PushID(i);
            if (InputI32("##artkit", a.artKit[i])) md();
            ImGui::PopID();
        }
        EndFieldTable();
    }

    ImGui::SeparatorText("flags (GameObjectFlags)");
    if (FlagCheckboxGrid("##flags", a.flags, GameObjectFlagBits(), 2)) md();
}
} // namespace we
