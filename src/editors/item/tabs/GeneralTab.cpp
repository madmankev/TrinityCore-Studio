// Layer E (ui) — Item General tab: identity, display, quality, vendor, stacking.

#include "editors/item/Tabs.h"
#include "editors/item/ItemEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Item.h"
#include "util/Enums.h"
#include "clientdata/ClientAssets.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace we
{
namespace
{
std::string MoneyStr(int64_t copper)
{
    const long long c = copper < 0 ? -copper : copper;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%lldg %llds %lldc", copper < 0 ? "-" : "",
                  c / 10000, (c / 100) % 100, c % 100);
    return buf;
}
} // namespace

void DrawItemGeneralTab(ItemEditorContext& ctx)
{
    if (!ctx.item)
    {
        ImGui::TextDisabled("No item loaded.");
        return;
    }
    ItemTemplate& t = ctx.item->tmpl;
    auto md = [&]() { ctx.MarkChanged(); ctx.item->tmplDirty = true; };

    // Icon preview (from client data, if loaded and the entry exists in the DB).
    if (ClientAssets* a = Assets())
        if (a->Ready())
            if (ImTextureID tex = a->ItemIcon(t.entry))
            {
                ImGui::Image(tex, ImVec2(40.0f, 40.0f));
                ImGui::SameLine();
            }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("displayid drives the icon/model.");

    ImGui::SeparatorText("Classification");
    if (BeginFieldTable("qe_item_class"))
    {
        FieldRow("Class", "item_template.class (ItemClass)");
        { uint32_t v = t.cls; if (EnumCombo("##class", v, ItemClassValues())) { t.cls = static_cast<uint8_t>(v); t.subclass = 0; md(); } }

        FieldRow("Subclass", "item_template.subclass (depends on class)");
        { uint32_t v = t.subclass; if (EnumCombo("##subclass", v, ItemSubclassValues(t.cls))) { t.subclass = static_cast<uint8_t>(v); md(); } }

        FieldRow("SoundOverrideSubclass", "-1 = none; overrides weapon sound subclass");
        { int32_t v = t.soundOverrideSubclass; if (InputI32("##soundovr", v)) { t.soundOverrideSubclass = static_cast<int8_t>(v); md(); } }

        FieldRow("displayid", "ItemDisplayInfo.dbc id (icon/model)");
        if (InputU32("##displayid", t.displayId)) md();

        FieldRow("Quality", "item_template.Quality");
        { uint32_t v = t.quality; if (EnumCombo("##quality", v, ItemQualityValues())) { t.quality = static_cast<uint8_t>(v); md(); } }

        FieldRow("InventoryType", "equip slot");
        { uint32_t v = t.inventoryType; if (EnumCombo("##invtype", v, InventoryTypeValues())) { t.inventoryType = static_cast<uint8_t>(v); md(); } }

        FieldRow("Bonding", "item_template.bonding");
        { uint32_t v = t.bonding; if (EnumCombo("##bonding", v, ItemBondingValues())) { t.bonding = static_cast<uint8_t>(v); md(); } }

        FieldRow("Sheath", "sheathe display type");
        { uint32_t v = t.sheath; if (EnumCombo("##sheath", v, SheatheValues())) { t.sheath = static_cast<uint8_t>(v); md(); } }

        FieldRow("Material", "Material.dbc (sound on hit); -1..N");
        { int32_t v = t.material; if (InputI32("##material", v)) { t.material = static_cast<int8_t>(v); md(); } }

        EndFieldTable();
    }

    ImGui::SeparatorText("Vendor");
    if (BeginFieldTable("qe_item_vendor"))
    {
        FieldRow("BuyCount", "stack size sold by a vendor");
        if (InputU8("##buycount", t.buyCount)) md();

        FieldRow("BuyPrice", "copper");
        {
            if (ImGui::InputScalar("##buyprice", ImGuiDataType_S64, &t.buyPrice)) md();
            ImGui::TextDisabled("%s", MoneyStr(t.buyPrice).c_str());
        }

        FieldRow("SellPrice", "copper");
        {
            if (InputU32("##sellprice", t.sellPrice)) md();
            ImGui::TextDisabled("%s", MoneyStr(static_cast<int64_t>(t.sellPrice)).c_str());
        }

        EndFieldTable();
    }

    ImGui::SeparatorText("Stacking & Misc");
    if (BeginFieldTable("qe_item_stack"))
    {
        FieldRow("maxcount", "max owned (<=0 = unlimited)");
        if (InputI32("##maxcount", t.maxCount)) md();

        FieldRow("stackable", "max stack (-1/2147483647 = unlimited)");
        if (InputI32("##stackable", t.stackable)) md();

        FieldRow("ContainerSlots", "bag slot count");
        if (InputU8("##containerslots", t.containerSlots)) md();

        FieldRow("duration", "existence timer in seconds (0 = none)");
        if (InputU32("##duration", t.duration)) md();

        FieldRow("FoodType", "pet food category");
        if (InputU8("##foodtype", t.foodType)) md();

        EndFieldTable();
    }
}
} // namespace we
