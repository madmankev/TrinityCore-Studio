// Layer E (ui) — GameObject General tab: type, display, captions, script.

#include "editors/gameobject/Tabs.h"
#include "editors/gameobject/GameObjectEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/GameObject.h"
#include "util/Enums.h"

#include <cstdint>

namespace qe
{
void DrawGameObjectGeneralTab(GameObjectEditorContext& ctx)
{
    if (!ctx.go)
    {
        ImGui::TextDisabled("No gameobject loaded.");
        return;
    }
    GameObjectTemplate& t = ctx.go->tmpl;
    auto md = [&]() { ctx.MarkChanged(); ctx.go->tmplDirty = true; };

    ImGui::SeparatorText("Identity");
    if (BeginFieldTable("qe_go_general"))
    {
        FieldRow("type", "gameobject_template.type — relabels the Data tab");
        { uint32_t v = t.type; if (EnumCombo("##type", v, GameObjectTypeValues())) { t.type = static_cast<uint8_t>(v); md(); } }

        FieldRow("displayId", "GameObjectDisplayInfo.dbc id (model)");
        if (InputU32("##displayid", t.displayId)) md();

        FieldRow("IconName", "cursor icon (e.g. Speak, Interact)");
        if (InputTextString("##iconname", t.iconName)) md();

        FieldRow("castBarCaption", "text on the interaction cast bar");
        if (InputTextString("##castbar", t.castBarCaption)) md();

        FieldRow("unk1", "unused string column");
        if (InputTextString("##unk1", t.unk1)) md();

        FieldRow("size", "display scale (1.0 = normal)");
        if (InputFloatField("##size", t.size)) md();

        EndFieldTable();
    }

    ImGui::SeparatorText("Scripting");
    if (BeginFieldTable("qe_go_script"))
    {
        FieldRow("AIName", "e.g. SmartGameObjectAI (blank = default)");
        if (InputTextString("##ainame", t.aiName)) md();
        FieldRow("ScriptName", "attached C++ script (blank = none)");
        if (InputTextString("##scriptname", t.scriptName)) md();
        FieldRow("StringId", "server-side string id (optional)");
        if (InputTextString("##stringid", t.stringId)) md();
        EndFieldTable();
    }
    ImGui::TextDisabled("The type-specific Data0..23 fields are on the Data tab.");
}
} // namespace qe
