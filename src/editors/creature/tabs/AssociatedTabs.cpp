// Layer E (ui) — Creature associated-system tabs: Vendor / Trainer / Loot / Spawns.
// Each edits the relational data keyed by the creature entry.

#include "editors/creature/Tabs.h"
#include "editors/creature/CreatureEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Creature.h"
#include "util/Enums.h"
#include "data/LookupCache.h"

#include <cfloat>
#include <cstdint>

namespace qe
{
// ---------------------------------------------------------------------------
void DrawCreatureVendorTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature) { ImGui::TextDisabled("No creature loaded."); return; }
    Creature& c = *ctx.creature;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); c.vendorDirty = true; };

    ImGui::TextDisabled("Items this creature sells (npc_vendor). Set the VENDOR npcflag on the Flags tab.");
    if (ImGui::Button("Add item"))
    {
        c.vendorItems.push_back(VendorItem{});
        md();
    }

    int rm = -1;
    if (ImGui::BeginTable("##vendor", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollX))
    {
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("MaxCount", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("IncrTime", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("ExtCost", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(c.vendorItems.size()); ++i)
        {
            ImGui::PushID(i);
            VendorItem& vi = c.vendorItems[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (IdNamePicker("##item", vi.item, lk, RefKind::Item)) md();
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputU8("##max", vi.maxCount)) md();
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputU32("##incr", vi.incrTime)) md();
            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputU32("##ext", vi.extendedCost)) md();
            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InputI16("##slot", vi.slot)) md();
            ImGui::TableSetColumnIndex(5);
            if (ImGui::Button("X")) rm = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rm >= 0) { c.vendorItems.erase(c.vendorItems.begin() + rm); md(); }
}

// ---------------------------------------------------------------------------
void DrawCreatureTrainerTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature) { ImGui::TextDisabled("No creature loaded."); return; }
    Creature& c = *ctx.creature;
    TrainerData& tr = c.trainer;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); c.trainerDirty = true; };

    { bool p = tr.present; if (ImGui::Checkbox("This creature is a trainer", &p)) { tr.present = p; md(); } }
    if (!tr.present)
    {
        ImGui::TextDisabled("Enable to bind a trainer (creature_default_trainer) and edit its spells.");
        return;
    }
    ImGui::TextDisabled("Editing trainer spells edits the shared trainer (Id %u); other creatures "
                        "bound to it are affected.", tr.trainerId);

    if (BeginFieldTable("qe_cr_trainer"))
    {
        FieldRow("Trainer Type", "trainer.Type");
        { uint32_t v = tr.type; if (EnumCombo("##ttype", v, TrainerTypeValues())) { tr.type = static_cast<uint8_t>(v); md(); } }
        FieldRow("Requirement", "class/race/spell required (by Type)");
        if (InputU32("##treq", tr.requirement)) md();
        EndFieldTable();
    }
    ImGui::TextUnformatted("Greeting:");
    if (InputMultiline("##tgreet", tr.greeting, 50.0f)) md();

    ImGui::Spacing();
    if (ImGui::Button("Add trained spell")) { tr.spells.push_back(TrainerSpell{}); md(); }

    int rm = -1;
    if (ImGui::BeginTable("##trainerspells", 9,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollX))
    {
        ImGui::TableSetupColumn("Spell", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("ReqSkill", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("ReqRank", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("ReqLvl", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("ReqAbil1", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("ReqAbil2", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("ReqAbil3", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(tr.spells.size()); ++i)
        {
            ImGui::PushID(i);
            TrainerSpell& ts = tr.spells[i];
            ImGui::TableNextRow();
            int col = 0;
            ImGui::TableSetColumnIndex(col++);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (IdNamePicker("##spell", ts.spellId, lk, RefKind::Spell)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU32("##cost", ts.moneyCost)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU32("##skill", ts.reqSkillLine)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU32("##rank", ts.reqSkillRank)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU8("##lvl", ts.reqLevel)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU32("##a1", ts.reqAbility[0])) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU32("##a2", ts.reqAbility[1])) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU32("##a3", ts.reqAbility[2])) md();
            ImGui::TableSetColumnIndex(col++); if (ImGui::Button("X")) rm = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rm >= 0) { tr.spells.erase(tr.spells.begin() + rm); md(); }
}

// ---------------------------------------------------------------------------
namespace
{
void DrawLootSection(CreatureEditorContext& ctx, const char* title, uint32_t& lootId,
                     std::vector<LootItem>& items, LookupCache& lk)
{
    Creature& c = *ctx.creature;
    auto md = [&]() { ctx.MarkChanged(); c.lootDirty = true; };
    ImGui::PushID(title);
    ImGui::SeparatorText(title);
    ImGui::Text("Loot template id:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (InputU32("##lootid", lootId)) { ctx.MarkChanged(); c.tmplDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Add loot"))
    {
        if (lootId == 0) { lootId = c.tmpl.entry; ctx.MarkChanged(); c.tmplDirty = true; }
        items.push_back(LootItem{});
        md();
    }

    int rm = -1;
    if (ImGui::BeginTable("##loot", 10,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollX))
    {
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ref", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Chance", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Quest", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Comment", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            ImGui::PushID(i);
            LootItem& l = items[i];
            ImGui::TableNextRow();
            int col = 0;
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (IdNamePicker("##item", l.item, lk, RefKind::Item)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU32("##ref", l.reference)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##chance", l.chance)) md();
            ImGui::TableSetColumnIndex(col++); { bool b = l.questRequired != 0; if (ImGui::Checkbox("##quest", &b)) { l.questRequired = b ? 1 : 0; md(); } }
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU16("##mode", l.lootMode)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU8("##group", l.groupId)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU8("##min", l.minCount)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU8("##max", l.maxCount)) md();
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputTextString("##comment", l.comment)) md();
            ImGui::TableSetColumnIndex(col++); if (ImGui::Button("X")) rm = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rm >= 0) { items.erase(items.begin() + rm); md(); }
    ImGui::PopID();
}
} // namespace

void DrawCreatureLootTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature) { ImGui::TextDisabled("No creature loaded."); return; }
    Creature& c = *ctx.creature;
    LookupCache& lk = *ctx.lookups;
    ImGui::TextDisabled("Editing a loot template affects every creature that shares its id.");
    DrawLootSection(ctx, "Creature Loot (creature_loot_template)", c.tmpl.lootId, c.creatureLoot, lk);
    DrawLootSection(ctx, "Pickpocket Loot (pickpocketing_loot_template)", c.tmpl.pickpocketLoot, c.pickpocketLoot, lk);
    DrawLootSection(ctx, "Skinning Loot (skinning_loot_template)", c.tmpl.skinLoot, c.skinLoot, lk);
}

// ---------------------------------------------------------------------------
void DrawCreatureSpawnsTab(CreatureEditorContext& ctx)
{
    if (!ctx.creature) { ImGui::TextDisabled("No creature loaded."); return; }
    Creature& c = *ctx.creature;
    auto touch = [&](CreatureSpawn& s) { if (!s.isNewRow) s.rowDirty = true; ctx.MarkChanged(); c.spawnsDirty = true; };

    ImGui::TextDisabled("World spawns of this creature (creature table). Edits apply per-guid on save.");
    if (ImGui::Button("Add spawn"))
    {
        CreatureSpawn s;
        s.isNewRow = true;
        c.spawns.push_back(s);
        ctx.MarkChanged();
        c.spawnsDirty = true;
    }

    int pendingDel = 0;
    for (const CreatureSpawn& s : c.spawns)
        if (s.deleted)
            ++pendingDel;
    if (pendingDel > 0)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.6f, 1.0f), "%d marked for deletion", pendingDel);
    }

    int rm = -1;
    if (ImGui::BeginTable("##spawns", 10,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollX))
    {
        ImGui::TableSetupColumn("guid", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("map", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("zone", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("O", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("SpawnT", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Wander", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(c.spawns.size()); ++i)
        {
            CreatureSpawn& s = c.spawns[i];
            if (s.deleted)
                continue;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            int col = 0;
            ImGui::TableSetColumnIndex(col++);
            ImGui::AlignTextToFramePadding();
            if (s.isNewRow) ImGui::TextDisabled("new"); else ImGui::Text("%u", s.guid);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU16("##map", s.map)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU16("##zone", s.zoneId)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##x", s.x)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##y", s.y)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##z", s.z)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##o", s.o)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputU32("##st", s.spawnTimeSecs)) touch(s);
            ImGui::TableSetColumnIndex(col++); ImGui::SetNextItemWidth(-FLT_MIN); if (InputFloatField("##wd", s.wanderDistance)) touch(s);
            ImGui::TableSetColumnIndex(col++); if (ImGui::Button("X")) rm = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (rm >= 0)
    {
        if (c.spawns[rm].isNewRow)
            c.spawns.erase(c.spawns.begin() + rm);
        else
            c.spawns[rm].deleted = true;
        ctx.MarkChanged();
        c.spawnsDirty = true;
    }
}
} // namespace qe
