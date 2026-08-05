// SpellModule — see SpellModule.h.

#include "editors/spell/SpellModule.h"

#include <string>

#include "imgui.h"

#include <cstdio>

#include "app/EditorServices.h"
#include "clientdata/ClientData.h"
#include "clientdata/DbcStore.h"
#include "editors/common/DbEditWidgets.h"
#include "editors/common/DbTableSchema.h"
#include "editors/common/DbcEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
std::string SpellModule::RowLabel(uint32_t row) const
{
    return std::to_string(doc_.GetU32(row, spell::Id)) + ": " + doc_.GetStr(row, spell::SpellName);
}

void SpellModule::OnRowSeeded(uint32_t row)
{
    doc_.SetStr(row, spell::SpellName, "New Spell");
}

void SpellModule::OnLoaded()
{
    durationById_.clear();
    castTimeById_.clear();
    rangeById_.clear();
    radiusById_.clear();
    iconById_.clear();
    if (!svc_ || !svc_->clientData)
        return;
    ClientData& cd = *svc_->clientData;

    auto loadInt = [&](const char* path, uint32_t valField, const char* unit,
                       std::unordered_map<uint32_t, std::string>& out) {
        Dbc d;
        if (!d.Load(cd.ReadFile(path)))
            return;
        for (uint32_t r = 0; r < d.RecordCount(); ++r)
            out[d.GetUInt(r, 0)] = std::to_string(d.GetUInt(r, valField)) + unit;
    };
    loadInt("DBFilesClient\\SpellDuration.dbc", 1, " ms", durationById_);
    loadInt("DBFilesClient\\SpellCastTimes.dbc", 1, " ms", castTimeById_);

    auto fmt = [](float f) { char b[32]; std::snprintf(b, sizeof(b), "%g", f); return std::string(b); };
    if (Dbc d; d.Load(cd.ReadFile("DBFilesClient\\SpellRange.dbc")))
        for (uint32_t r = 0; r < d.RecordCount(); ++r)
            rangeById_[d.GetUInt(r, 0)] = fmt(d.GetFloat(r, 1)) + "-" + fmt(d.GetFloat(r, 3)) + " yd";
    if (Dbc d; d.Load(cd.ReadFile("DBFilesClient\\SpellRadius.dbc")))
        for (uint32_t r = 0; r < d.RecordCount(); ++r)
            radiusById_[d.GetUInt(r, 0)] = fmt(d.GetFloat(r, 1)) + " yd";
    if (svc_->dbcStore)
        iconById_ = svc_->dbcStore->LoadSpellIconPaths(cd);
}

std::string SpellModule::ResolveIndex(const std::unordered_map<uint32_t, std::string>& m,
                                      uint32_t id) const
{
    auto it = m.find(id);
    return it != m.end() ? it->second : std::string();
}

void SpellModule::DrawIndexField(const char* label, uint32_t row, uint32_t col,
                                 const std::unordered_map<uint32_t, std::string>& m, const char* tip)
{
    FieldRow(label, tip);
    uint32_t v = doc_.GetU32(row, col);
    std::string name = ResolveIndex(m, v);
    std::string id = std::string("##") + label;
    if (InputU32Named(id.c_str(), v, name))
        doc_.SetU32(row, col, v);
}

const char* SpellModule::TabName(int tab) const
{
    switch (tab)
    {
    case 0: return "General";
    case 1: return "Attributes";
    case 2: return "Casting";
    case 3: return "Targeting";
    case 4: return "Effects";
    case 5: return "Text";
    case 6: return "Misc";
    case 7: return "Server: Core";
    case 8: return "Server: Links";
    case 9: return "Server: Advanced";
    default: return "Server: Scripts";
    }
}

void SpellModule::DrawTab(int tab, uint32_t row)
{
    const uint32_t spellId = doc_.GetU32(row, spell::Id);
    switch (tab)
    {
    case 0: DrawGeneralTab(row); break;
    case 1: DrawAttributesTab(row); break;
    case 2: DrawCastingTab(row); break;
    case 3: DrawTargetingTab(row); break;
    case 4: DrawEffectsTab(row); break;
    case 5: DrawTextTab(row); break;
    case 6: DrawMiscTab(row); break;
    case 7: DrawServerCoreTab(spellId); break;
    case 8: DrawServerLinksTab(spellId); break;
    case 9: DrawServerAdvancedTab(spellId); break;
    default: DrawServerScriptsTab(spellId); break;
    }
}

std::vector<std::string> SpellModule::ReloadCommands() const
{
    return {".reload spell_proc", ".reload spell_bonus_data", ".reload spell_threat",
            ".reload spell_area", ".reload spell_linked_spell", ".reload spell_target_position"};
}

void SpellModule::OnAfterSave()
{
    if (!svc_ || !svc_->activeDb || selectedRow_ < 0)
        return;
    DbError e = spellRepo_.SaveSpellDbc(*svc_->activeDb, doc_, static_cast<uint32_t>(selectedRow_));
    if (svc_->setStatus)
        svc_->setStatus(e.ok ? "Also projected to server spell_dbc." : "spell_dbc save failed: " + e.message);
    serverForId_ = kNoServer;  // force reload of the server tabs
}

// --- server tabs -----------------------------------------------------------
bool SpellModule::RequireDbAndLoad(uint32_t spellId)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to edit server-side spell_* tables.");
        return false;
    }
    if (serverForId_ == spellId)
        return true;

    IDatabase& db = *svc_->activeDb;
    const std::string idStr = std::to_string(spellId);
    dbRepo_.Load(db, SpellRepository::ProcSchema(), spellId, proc_);
    dbRepo_.Load(db, SpellRepository::BonusSchema(), spellId, bonus_);
    dbRepo_.Load(db, SpellRepository::ThreatSchema(), spellId, threat_);
    dbRepo_.Load(db, SpellRepository::CustomAttrSchema(), spellId, customAttr_);
    dbRepo_.Load(db, SpellRepository::DifficultySchema(), spellId, difficulty_);

    auto loadChild = [&](const SpellChildSpec& s, std::vector<DbRecord>& out) {
        dbRepo_.ListChildren(db, s.table, s.parentCol, idStr, s.cols, out);
    };
    loadChild(SpellRepository::Required(), required_);
    loadChild(SpellRepository::LearnSpell(), learn_);
    loadChild(SpellRepository::LinkedSpell(), linked_);
    loadChild(SpellRepository::Ranks(), ranks_);
    loadChild(SpellRepository::Area(), area_);
    loadChild(SpellRepository::TargetPosition(), targetPos_);
    loadChild(SpellRepository::PetAuras(), petAuras_);
    loadChild(SpellRepository::ScriptNames(), scriptNames_);
    loadChild(SpellRepository::Scripts(), scripts_);
    loadChild(SpellRepository::LootTemplate(), loot_);
    loadChild(SpellRepository::GroupMembership(), groups_);
    serverForId_ = spellId;
    return true;
}

namespace
{
// Draw one DB field bound to a DbRecord cell, by column type.
void DrawDbField(const DbColumn& c, DbRecord& rec)
{
    switch (c.type)
    {
    case DbColType::I32: DbI32Field(c.label, rec, c.name, c.tip); break;
    case DbColType::Float: DbFloatField(c.label, rec, c.name, c.tip); break;
    case DbColType::Text: DbTextField(c.label, rec, c.name, c.tip); break;
    case DbColType::Multiline: DbMultilineField(c.label, rec, c.name); break;
    default: DbU32Field(c.label, rec, c.name, c.tip); break;
    }
}
} // namespace

void SpellModule::Draw1to1(const char* title, const DbTableSchema& schema, DbRecord& rec,
                           uint32_t spellId)
{
    ImGui::PushID(title);
    ImGui::SeparatorText(title);
    ImGui::Text("%s", rec.present ? "(row exists)" : "(no row)");
    ImGui::SameLine();
    if (ImGui::SmallButton(svc_->mode == WriteMode::SqlExport ? "Export" : "Save"))
    {
        rec.id = spellId;
        rec.cells[schema.pk] = std::to_string(spellId);
        DbError e = dbRepo_.Save(*svc_->activeDb, schema, rec);
        if (svc_->setStatus)
            svc_->setStatus(e.ok ? std::string("Saved ") + schema.table
                                 : std::string(schema.table) + " save failed: " + e.message);
        if (e.ok)
            rec.present = true;
    }
    if (BeginFieldTable((std::string("##t") + title).c_str()))
    {
        for (const DbColumn& c : schema.cols)
            DrawDbField(c, rec);
        EndFieldTable();
    }
    ImGui::PopID();
}

void SpellModule::DrawChildList(const char* title, const SpellChildSpec& spec,
                                std::vector<DbRecord>& rows, uint32_t spellId)
{
    ImGui::PushID(title);
    ImGui::SeparatorText(title);
    ImGui::Text("%zu rows", rows.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Add"))
    {
        DbRecord r;
        r.cells[spec.parentCol] = std::to_string(spellId);
        rows.push_back(std::move(r));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(svc_->mode == WriteMode::SqlExport ? "Export all" : "Save all"))
    {
        for (DbRecord& r : rows)
            r.cells[spec.parentCol] = std::to_string(spellId);
        DbError e = dbRepo_.ReplaceChildren(*svc_->activeDb, spec.table, spec.parentCol,
                                            std::to_string(spellId), spec.cols, rows);
        if (svc_->setStatus)
            svc_->setStatus(e.ok ? std::string("Saved ") + spec.table
                                 : std::string(spec.table) + " save failed: " + e.message);
    }

    int deleteIdx = -1;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        if (BeginFieldTable("##ct"))
        {
            for (const DbColumn& c : spec.cols)
                if (std::string(c.name) != spec.parentCol)  // parent col is fixed = spell id
                    DrawDbField(c, rows[i]);
            EndFieldTable();
        }
        if (ImGui::SmallButton("Remove"))
            deleteIdx = static_cast<int>(i);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteIdx >= 0)
        rows.erase(rows.begin() + deleteIdx);
    ImGui::PopID();
}

void SpellModule::DrawServerCoreTab(uint32_t spellId)
{
    if (!RequireDbAndLoad(spellId))
        return;
    Draw1to1("spell_proc", SpellRepository::ProcSchema(), proc_, spellId);
    Draw1to1("spell_bonus_data", SpellRepository::BonusSchema(), bonus_, spellId);
    Draw1to1("spell_threat", SpellRepository::ThreatSchema(), threat_, spellId);
    Draw1to1("spell_custom_attr", SpellRepository::CustomAttrSchema(), customAttr_, spellId);
}

void SpellModule::DrawServerLinksTab(uint32_t spellId)
{
    if (!RequireDbAndLoad(spellId))
        return;
    DrawChildList("spell_required", SpellRepository::Required(), required_, spellId);
    DrawChildList("spell_learn_spell", SpellRepository::LearnSpell(), learn_, spellId);
    DrawChildList("spell_linked_spell (trigger)", SpellRepository::LinkedSpell(), linked_, spellId);
    DrawChildList("spell_ranks (chain from this spell)", SpellRepository::Ranks(), ranks_, spellId);
}

void SpellModule::DrawServerAdvancedTab(uint32_t spellId)
{
    if (!RequireDbAndLoad(spellId))
        return;
    Draw1to1("spelldifficulty_dbc", SpellRepository::DifficultySchema(), difficulty_, spellId);
    DrawChildList("spell_area", SpellRepository::Area(), area_, spellId);
    DrawChildList("spell_target_position", SpellRepository::TargetPosition(), targetPos_, spellId);
    DrawChildList("spell_pet_auras", SpellRepository::PetAuras(), petAuras_, spellId);
}

void SpellModule::DrawServerScriptsTab(uint32_t spellId)
{
    if (!RequireDbAndLoad(spellId))
        return;
    DrawChildList("spell_script_names", SpellRepository::ScriptNames(), scriptNames_, spellId);
    DrawChildList("spell_scripts", SpellRepository::Scripts(), scripts_, spellId);
    DrawChildList("spell_loot_template", SpellRepository::LootTemplate(), loot_, spellId);
    DrawChildList("spell_group (memberships)", SpellRepository::GroupMembership(), groups_, spellId);
}

void SpellModule::DrawGeneralTab(uint32_t row)
{
    if (BeginFieldTable("##spgeneral"))
    {
        DbcU32Field("ID", doc_, row, spell::Id);
        DbcU32Field("Category", doc_, row, spell::Category, "SpellCategory.dbc id");
        DbcU32Field("Dispel type", doc_, row, spell::DispelType, "0 none / 1 magic / 2 curse / 3 disease / 4 poison");
        DbcU32Field("Mechanic", doc_, row, spell::Mechanic, "SpellMechanic.dbc id");
        DbcU32Field("School mask", doc_, row, spell::SchoolMask, "1 phys 2 holy 4 fire 8 nature 16 frost 32 shadow 64 arcane");
        DbcU32Field("Power type", doc_, row, spell::PowerType, "0 mana 1 rage 3 energy 6 runic power ...");
        DbcU32Field("Base level", doc_, row, spell::BaseLevel);
        DbcU32Field("Spell level", doc_, row, spell::SpellLevel);
        DbcU32Field("Max level", doc_, row, spell::MaxLevel);
        DrawIndexField("Icon", row, spell::SpellIconID, iconById_, "SpellIcon.dbc id");
        DbcU32Field("Active icon", doc_, row, spell::ActiveIconID);
        DbcU32Field("Spell visual 1", doc_, row, spell::SpellVisual);
        DbcU32Field("Spell visual 2", doc_, row, spell::SpellVisual + 1);
        DbcU32Field("Priority", doc_, row, spell::SpellPriority);
        DbcU32Field("Spell family", doc_, row, spell::SpellClassSet, "SpellFamilyName");
        DbcU32Field("Family mask 1", doc_, row, spell::SpellClassMask);
        DbcU32Field("Family mask 2", doc_, row, spell::SpellClassMask + 1);
        DbcU32Field("Family mask 3", doc_, row, spell::SpellClassMask + 2);
        EndFieldTable();
    }
}

void SpellModule::DrawAttributesTab(uint32_t row)
{
    ImGui::TextDisabled("Raw attribute bitmasks (hex/decimal). Bit meanings: SpellAttr0..Ex7.");
    if (BeginFieldTable("##spattr"))
    {
        DbcU32Field("Attributes", doc_, row, spell::Attributes);
        for (uint32_t i = 1; i <= 7; ++i)
        {
            std::string label = "AttributesEx" + std::to_string(i);
            DbcU32Field(label.c_str(), doc_, row, spell::Attributes + i);
        }
        EndFieldTable();
    }
    ImGui::SeparatorText("Stances");
    if (BeginFieldTable("##spstances"))
    {
        DbcU32Field("Stances (lo)", doc_, row, spell::Stances);
        DbcU32Field("Stances (hi)", doc_, row, spell::Stances + 1);
        DbcU32Field("StancesNot (lo)", doc_, row, spell::StancesNot);
        DbcU32Field("StancesNot (hi)", doc_, row, spell::StancesNot + 1);
        EndFieldTable();
    }
}

void SpellModule::DrawCastingTab(uint32_t row)
{
    if (BeginFieldTable("##spcast"))
    {
        DrawIndexField("Cast time index", row, spell::CastingTimeIndex, castTimeById_, "SpellCastTimes.dbc id");
        DbcU32Field("Recovery (cooldown ms)", doc_, row, spell::RecoveryTime);
        DbcU32Field("Category recovery (ms)", doc_, row, spell::CategoryRecoveryTime);
        DbcU32Field("GCD category", doc_, row, spell::StartRecoveryCategory);
        DbcU32Field("GCD time (ms)", doc_, row, spell::StartRecoveryTime);
        DrawIndexField("Duration index", row, spell::DurationIndex, durationById_, "SpellDuration.dbc id");
        DrawIndexField("Range index", row, spell::RangeIndex, rangeById_, "SpellRange.dbc id");
        DbcF32Field("Speed", doc_, row, spell::Speed, "Projectile speed");
        DbcU32Field("Stack amount", doc_, row, spell::StackAmount);
        DbcU32Field("Rune cost id", doc_, row, spell::RuneCostID, "SpellRuneCost.dbc id (DK)");
        DbcU32Field("Power display id", doc_, row, spell::PowerDisplayID);
        EndFieldTable();
    }

    ImGui::SeparatorText("Cost");
    if (BeginFieldTable("##spcost"))
    {
        DbcU32Field("Mana cost", doc_, row, spell::ManaCost);
        DbcU32Field("Mana cost per level", doc_, row, spell::ManaCostPerLevel);
        DbcU32Field("Mana per second", doc_, row, spell::ManaPerSecond);
        DbcU32Field("Mana per second/level", doc_, row, spell::ManaPerSecondPerLevel);
        DbcU32Field("Mana cost %", doc_, row, spell::ManaCostPct);
        EndFieldTable();
    }

    ImGui::SeparatorText("Proc / interrupts");
    if (BeginFieldTable("##spproc"))
    {
        DbcU32Field("Proc flags", doc_, row, spell::ProcFlags);
        DbcU32Field("Proc chance", doc_, row, spell::ProcChance);
        DbcU32Field("Proc charges", doc_, row, spell::ProcCharges);
        DbcU32Field("Interrupt flags", doc_, row, spell::InterruptFlags);
        DbcU32Field("Aura interrupt flags", doc_, row, spell::AuraInterruptFlags);
        DbcU32Field("Channel interrupt flags", doc_, row, spell::ChannelInterruptFlags);
        EndFieldTable();
    }

    ImGui::SeparatorText("Reagents / equipped item");
    if (BeginFieldTable("##spreagent"))
    {
        LookupCache& cache = *svc_->lookups;
        for (uint32_t i = 0; i < 8; ++i)
        {
            std::string rl = "Reagent " + std::to_string(i + 1);
            std::string cl = "  count " + std::to_string(i + 1);
            DbcIdNameField(rl.c_str(), doc_, row, spell::Reagent + i, cache, RefKind::Item);
            DbcU32Field(cl.c_str(), doc_, row, spell::ReagentCount + i);
        }
        DbcU32Field("Totem 1", doc_, row, spell::Totem);
        DbcU32Field("Totem 2", doc_, row, spell::Totem + 1);
        DbcU32Field("Req totem category 1", doc_, row, spell::RequiredTotemCategoryID);
        DbcU32Field("Req totem category 2", doc_, row, spell::RequiredTotemCategoryID + 1);
        DbcI32Field("Equipped item class", doc_, row, spell::EquippedItemClass);
        DbcI32Field("Equipped item subclass mask", doc_, row, spell::EquippedItemSubclass);
        DbcI32Field("Equipped item inv-type mask", doc_, row, spell::EquippedItemInvTypes);
        EndFieldTable();
    }
}

void SpellModule::DrawTargetingTab(uint32_t row)
{
    LookupCache& cache = *svc_->lookups;
    if (BeginFieldTable("##sptarget"))
    {
        DbcU32Field("Targets", doc_, row, spell::Targets, "Target type bitmask");
        DbcU32Field("Target creature type", doc_, row, spell::TargetCreatureType, "Creature-type mask");
        DbcU32Field("Requires spell focus", doc_, row, spell::RequiresSpellFocus, "SpellFocusObject.dbc id");
        DbcU32Field("Facing caster flags", doc_, row, spell::FacingCasterFlags);
        DbcU32Field("Max affected targets", doc_, row, spell::MaxAffectedTargets);
        DbcU32Field("Max target level", doc_, row, spell::MaxTargetLevel);
        EndFieldTable();
    }
    ImGui::SeparatorText("Aura requirements");
    if (BeginFieldTable("##spaura"))
    {
        DbcU32Field("Caster aura state", doc_, row, spell::CasterAuraState);
        DbcU32Field("Target aura state", doc_, row, spell::TargetAuraState);
        DbcU32Field("Exclude caster aura state", doc_, row, spell::ExcludeCasterAuraState);
        DbcU32Field("Exclude target aura state", doc_, row, spell::ExcludeTargetAuraState);
        DbcIdNameField("Caster aura spell", doc_, row, spell::CasterAuraSpell, cache, RefKind::Spell);
        DbcIdNameField("Target aura spell", doc_, row, spell::TargetAuraSpell, cache, RefKind::Spell);
        DbcIdNameField("Excl. caster aura spell", doc_, row, spell::ExcludeCasterAuraSpell, cache, RefKind::Spell);
        DbcIdNameField("Excl. target aura spell", doc_, row, spell::ExcludeTargetAuraSpell, cache, RefKind::Spell);
        EndFieldTable();
    }
}

void SpellModule::DrawEffectsTab(uint32_t row)
{
    LookupCache& cache = *svc_->lookups;
    for (uint32_t e = 0; e < 3; ++e)
    {
        ImGui::PushID(static_cast<int>(e));
        std::string header = "Effect " + std::to_string(e + 1) + " (type " +
                             std::to_string(doc_.GetU32(row, spell::Effect + e)) + ")###eff";
        if (ImGui::CollapsingHeader(header.c_str(), e == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0))
        {
            if (BeginFieldTable("##speff"))
            {
                DbcU32Field("Effect type", doc_, row, spell::Effect + e, "SPELL_EFFECT_*");
                DbcU32Field("Aura type", doc_, row, spell::EffectAura + e, "SPELL_AURA_* (if effect is APPLY_AURA)");
                DbcI32Field("Base points", doc_, row, spell::EffectBasePoints + e, "Value = BasePoints + 1 (DieSides)");
                DbcI32Field("Die sides", doc_, row, spell::EffectDieSides + e);
                DbcF32Field("Real points/level", doc_, row, spell::EffectRealPointsPerLevel + e);
                DbcU32Field("Aura period (ms)", doc_, row, spell::EffectAuraPeriod + e);
                DbcF32Field("Amplitude/multiple", doc_, row, spell::EffectAmplitude + e);
                DbcU32Field("Mechanic", doc_, row, spell::EffectMechanic + e);
                DrawIndexField("Radius index", row, spell::EffectRadiusIndex + e, radiusById_, "SpellRadius.dbc id");
                DbcU32Field("Implicit target A", doc_, row, spell::EffectImplicitTargetA + e);
                DbcU32Field("Implicit target B", doc_, row, spell::EffectImplicitTargetB + e);
                DbcU32Field("Chain targets", doc_, row, spell::EffectChainTargets + e);
                DbcIdNameField("Item type", doc_, row, spell::EffectItemType + e, cache, RefKind::Item);
                DbcI32Field("Misc value", doc_, row, spell::EffectMiscValue + e);
                DbcI32Field("Misc value B", doc_, row, spell::EffectMiscValueB + e);
                DbcIdNameField("Trigger spell", doc_, row, spell::EffectTriggerSpell + e, cache, RefKind::Spell);
                DbcF32Field("Points per combo", doc_, row, spell::EffectPointsPerCombo + e);
                DbcF32Field("Damage multiplier", doc_, row, spell::DmgMultiplier + e);
                DbcF32Field("Bonus coefficient", doc_, row, spell::EffectBonusCoefficient + e);
                DbcU32Field("Class mask A", doc_, row, spell::EffectSpellClassMask + e * 3 + 0);
                DbcU32Field("Class mask B", doc_, row, spell::EffectSpellClassMask + e * 3 + 1);
                DbcU32Field("Class mask C", doc_, row, spell::EffectSpellClassMask + e * 3 + 2);
                EndFieldTable();
            }
        }
        ImGui::PopID();
    }
}

void SpellModule::DrawTextTab(uint32_t row)
{
    DbcLangEditor("Name", doc_, row, spell::SpellName);
    DbcLangEditor("Rank", doc_, row, spell::Rank);
    DbcLangEditor("Description", doc_, row, spell::Description);
    DbcLangEditor("Tooltip", doc_, row, spell::ToolTip);
}

void SpellModule::DrawMiscTab(uint32_t row)
{
    if (BeginFieldTable("##spmisc"))
    {
        DbcU32Field("Damage class", doc_, row, spell::DmgClass, "0 none 1 magic 2 melee 3 ranged");
        DbcU32Field("Prevention type", doc_, row, spell::PreventionType);
        DbcU32Field("Stance bar order", doc_, row, spell::StanceBarOrder);
        DbcI32Field("Area group id", doc_, row, spell::AreaGroupId, "AreaGroup.dbc id");
        DbcU32Field("Min faction id", doc_, row, spell::MinFactionID);
        DbcU32Field("Min reputation", doc_, row, spell::MinReputation);
        DbcU32Field("Required aura vision", doc_, row, spell::RequiredAuraVision);
        DbcU32Field("Difficulty", doc_, row, spell::Difficulty, "SpellDifficulty.dbc id");
        DbcU32Field("Description variables id", doc_, row, spell::DescriptionVariablesID);
        DbcU32Field("Spell missile id", doc_, row, spell::SpellMissileID);
        DbcU32Field("Modal next spell", doc_, row, spell::ModalNextSpell);
        EndFieldTable();
    }
}
} // namespace we
