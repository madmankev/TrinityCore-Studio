// Layer E (ui) — Creature Locales tab: creature_template_locale Name/Title per locale.

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "schema/Types.h"

#include <string>

namespace we
{
void DrawCreatureLocalesTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature)
    {
        ImGui::TextDisabled("No creature loaded.");
        return;
    }
    Creature& c = *ctx.creature;
    auto md = [&]() { ctx.MarkChanged(); c.localesDirty = true; };

    ImGui::TextDisabled("Base enUS name/subname are on the General tab. Localized overrides here.");
    ImGui::Spacing();

    static int selected = 0;
    if (selected < 0 || selected >= kLocaleCount)
        selected = 0;

    auto hasData = [&](const char* code) -> bool
    {
        auto it = c.locales.find(code);
        return it != c.locales.end() && (!it->second.name.empty() || !it->second.title.empty());
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
    CreatureLocale& l = c.locales[code];
    l.locale = code;

    ImGui::Spacing();
    if (BeginFieldTable("qe_cr_locale"))
    {
        FieldRow("Name", "creature_template_locale.Name");
        if (InputTextString("##locname", l.name)) md();
        FieldRow("Title", "creature_template_locale.Title (localized subname)");
        if (InputTextString("##loctitle", l.title)) md();
        EndFieldTable();
    }
}
} // namespace we
