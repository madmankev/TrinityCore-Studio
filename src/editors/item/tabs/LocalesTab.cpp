// Layer E (ui) — Item Locales tab: item_template_locale Name/Description per locale.

#include "editors/item/Tabs.h"
#include "editors/item/ItemEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Item.h"
#include "schema/Types.h"

#include <string>

namespace qe
{
void DrawItemLocalesTab(ItemEditorContext& ctx)
{
    if (!ctx.item)
    {
        ImGui::TextDisabled("No item loaded.");
        return;
    }
    Item& item = *ctx.item;
    auto md = [&]() { ctx.MarkChanged(); item.localesDirty = true; };

    ImGui::TextDisabled("Base enUS name/description live on the General/Text tabs. "
                        "Localized overrides go here.");
    ImGui::Spacing();

    static int selected = 0;
    if (selected < 0 || selected >= kLocaleCount)
        selected = 0;

    auto hasData = [&](const char* code) -> bool
    {
        auto it = item.locales.find(code);
        return it != item.locales.end() &&
               (!it->second.name.empty() || !it->second.description.empty());
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
    ItemLocale& l = item.locales[code];
    l.locale = code;

    ImGui::Spacing();
    if (BeginFieldTable("qe_item_locale"))
    {
        FieldRow("Name", "item_template_locale.Name");
        if (InputTextString("##locname", l.name)) md();
        EndFieldTable();
    }

    ImGui::TextUnformatted("Description:");
    if (InputMultiline("##locdesc", l.description, 60.0f)) md();
}
} // namespace qe
