// Layer E (ui) — GameObject Locales tab: gameobject_template_locale name/castBarCaption.

#include "editors/gameobject/Tabs.h"
#include "editors/gameobject/GameObjectEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/GameObject.h"
#include "schema/Types.h"

#include <string>

namespace qe
{
void DrawGameObjectLocalesTab(GameObjectEditorContext& ctx)
{
    if (!ctx.go)
    {
        ImGui::TextDisabled("No gameobject loaded.");
        return;
    }
    GameObject& go = *ctx.go;
    auto md = [&]() { ctx.MarkChanged(); go.localesDirty = true; };

    ImGui::TextDisabled("Base enUS name/castBarCaption are on the General tab. Localized overrides here.");
    ImGui::Spacing();

    static int selected = 0;
    if (selected < 0 || selected >= kLocaleCount)
        selected = 0;
    auto hasData = [&](const char* code) -> bool
    {
        auto it = go.locales.find(code);
        return it != go.locales.end() && (!it->second.name.empty() || !it->second.castBarCaption.empty());
    };

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("Locale", kLocales[selected]))
    {
        for (int i = 0; i < kLocaleCount; ++i)
        {
            std::string label = kLocales[i];
            if (hasData(kLocales[i]))
                label += " *";
            if (ImGui::Selectable(label.c_str(), selected == i))
                selected = i;
        }
        ImGui::EndCombo();
    }

    const char* code = kLocales[selected];
    GameObjectLocale& l = go.locales[code];
    l.locale = code;

    ImGui::Spacing();
    if (BeginFieldTable("qe_go_locale"))
    {
        FieldRow("name", "gameobject_template_locale.name");
        if (InputTextString("##locname", l.name)) md();
        FieldRow("castBarCaption", "localized cast bar caption");
        if (InputTextString("##loccast", l.castBarCaption)) md();
        EndFieldTable();
    }
}
} // namespace qe
