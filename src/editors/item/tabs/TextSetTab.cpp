// Layer E (ui) — Item Text & Set tab: description, page/book, set, loot, scripting.

#include "editors/item/Tabs.h"
#include "editors/item/ItemEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Item.h"

#include <cstdint>

namespace we
{
void DrawItemTextSetTab(ItemEditorContext& ctx)
{
    if (!ctx.item)
    {
        ImGui::TextDisabled("No item loaded.");
        return;
    }
    ItemTemplate& t = ctx.item->tmpl;
    auto md = [&]() { ctx.MarkChanged(); ctx.item->tmplDirty = true; };

    ImGui::SeparatorText("Description");
    if (InputMultiline("##description", t.description, 60.0f)) md();
    ImGui::TextDisabled("Yellow flavor text (item_template.description).");

    ImGui::SeparatorText("Readable / Page");
    if (BeginFieldTable("qe_item_page"))
    {
        FieldRow("PageText", "page_text id (readable books)");
        if (InputU32("##pagetext", t.pageText)) md();

        FieldRow("LanguageID", "Languages.dbc");
        if (InputU8("##langid", t.languageID)) md();

        FieldRow("PageMaterial", "book page background");
        if (InputU8("##pagematerial", t.pageMaterial)) md();

        EndFieldTable();
    }

    ImGui::SeparatorText("Set / Random / Category");
    if (BeginFieldTable("qe_item_set"))
    {
        FieldRow("itemset", "ItemSet.dbc");
        if (InputU32Named("##itemset", t.itemSet, ItemEditorContext::Name(ctx.itemSetNames, t.itemSet)))
            md();

        FieldRow("RandomProperty", "ItemRandomProperties.dbc");
        if (InputI32Named("##randomprop", t.randomProperty,
                          ItemEditorContext::Name(ctx.randomPropNames,
                                                  static_cast<uint32_t>(t.randomProperty))))
            md();

        FieldRow("RandomSuffix", "ItemRandomSuffix.dbc");
        if (InputU32Named("##randomsuffix", t.randomSuffix,
                          ItemEditorContext::Name(ctx.randomSuffixNames, t.randomSuffix)))
            md();

        FieldRow("TotemCategory", "TotemCategory.dbc");
        if (InputI32("##totemcat", t.totemCategory)) md();

        FieldRow("ItemLimitCategory", "ItemLimitCategory.dbc");
        if (InputI16Named("##limitcat", t.itemLimitCategory,
                          ItemEditorContext::Name(ctx.limitCategoryNames,
                                                  static_cast<uint32_t>(t.itemLimitCategory))))
            md();

        FieldRow("HolidayId", "Holidays.dbc gating");
        if (InputU32("##holidayid", t.holidayId)) md();

        EndFieldTable();
    }

    ImGui::SeparatorText("Loot / Scripting");
    if (BeginFieldTable("qe_item_loot"))
    {
        FieldRow("minMoneyLoot", "min money when item is a money container");
        if (InputU32("##minmoney", t.minMoneyLoot)) md();

        FieldRow("maxMoneyLoot", "max money loot");
        if (InputU32("##maxmoney", t.maxMoneyLoot)) md();

        FieldRow("ScriptName", "attached C++ script");
        if (InputTextString("##scriptname", t.scriptName)) md();

        FieldRow("VerifiedBuild", "client build stamp (carry-through)");
        if (InputI32("##verifiedbuild", t.verifiedBuild)) md();

        EndFieldTable();
    }
}
} // namespace we
