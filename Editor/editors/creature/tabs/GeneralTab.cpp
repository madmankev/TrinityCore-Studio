// Layer E (ui) — Creature General tab: identity, gossip, models, scale.

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"

#include <cstdio>

namespace we
{
void DrawCreatureGeneralTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    CreatureTemplate& t = ctx.creature->tmpl;
    auto md = [&]() { ctx.MarkChanged(); ctx.creature->tmplDirty = true; };

    ImGui::SeparatorText("Identity");
    if (BeginFieldTable("qe_cr_identity"))
    {
        FieldRow("subname", "creature_template.subname (title, e.g. <Innkeeper>)");
        if (InputTextString("##subname", t.subname)) md();

        FieldRow("IconName", "cursor icon (e.g. Speak, Vendor, Trainer)");
        if (InputTextString("##iconname", t.iconName)) md();

        FieldRow("gossip_menu_id", "gossip_menu.MenuID (0 = default)");
        if (InputU32("##gossipmenu", t.gossipMenuId)) md();

        FieldRow("StringId", "server-side string id (optional)");
        if (InputTextString("##stringid", t.stringId)) md();

        FieldRow("scale", "display scale (1.0 = normal)");
        if (InputFloatField("##scale", t.scale)) md();

        EndFieldTable();
    }

    ImGui::SeparatorText("Models");
    if (BeginFieldTable("qe_cr_models"))
    {
        for (int i = 0; i < 4; ++i)
        {
            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "modelid%d", i + 1);
            FieldRow(lbl, "CreatureDisplayInfo.dbc id (0 = unused)");
            ImGui::PushID(i);
            if (InputU32("##model", t.modelId[i])) md();
            ImGui::PopID();
        }
        EndFieldTable();
    }
    ImGui::TextDisabled("A random non-zero modelid is chosen at spawn.");
}
} // namespace we
