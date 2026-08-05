// SkillModule — see SkillModule.h.

#include "editors/skill/SkillModule.h"

#include "imgui.h"

#include "app/EditorServices.h"
#include "data/LookupCache.h"
#include "editors/common/DbcEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
void SkillModule::Init(EditorServices* services)
{
    SimpleDbcEditorModule::Init(services);
    lineDoc_.Init(&SkillLineSchema(), "DBFilesClient\\SkillLine.dbc");
}

void SkillModule::RebuildSkillNames()
{
    skillNames_.clear();
    for (uint32_t r = 0; r < lineDoc_.RecordCount(); ++r)
        skillNames_[lineDoc_.GetU32(r, skillline::Id)] = lineDoc_.GetStr(r, skillline::Name);
}

void SkillModule::OnLoaded()
{
    lineSel_ = -1;
    RebuildSkillNames();
}

std::string SkillModule::SkillName(uint32_t skillId) const
{
    auto it = skillNames_.find(skillId);
    return it != skillNames_.end() ? it->second : std::string();
}

std::string SkillModule::RowLabel(uint32_t row) const
{
    uint32_t id = doc_.GetU32(row, skillability::Id);
    uint32_t skill = doc_.GetU32(row, skillability::SkillLine);
    uint32_t spell = doc_.GetU32(row, skillability::Spell);
    std::string spellName = (svc_ && svc_->lookups) ? svc_->lookups->NameOfSpell(spell) : std::string();
    std::string label = std::to_string(id) + ": " + SkillName(skill);
    if (!spellName.empty())
        label += " — " + spellName;
    return label;
}

void SkillModule::DrawTab(int tab, uint32_t row)
{
    if (tab == 1)
        DrawLinesTab();
    else
        DrawGeneralTab(row);
}

void SkillModule::DrawGeneralTab(uint32_t row)
{
    LookupCache& cache = *svc_->lookups;
    if (BeginFieldTable("##skgen"))
    {
        DbcU32Field("ID", doc_, row, skillability::Id);

        FieldRow("Skill line", "SkillLine.dbc id");
        uint32_t skill = doc_.GetU32(row, skillability::SkillLine);
        if (InputU32Named("##skill", skill, SkillName(skill)))
            doc_.SetU32(row, skillability::SkillLine, skill);

        DbcIdNameField("Spell", doc_, row, skillability::Spell, cache, RefKind::Spell);
        DbcU32Field("Race mask", doc_, row, skillability::RaceMask, "0 = all races");
        DbcU32Field("Class mask", doc_, row, skillability::ClassMask, "0 = all classes; 128 = Mage ...");
        DbcU32Field("Race mask forbidden", doc_, row, skillability::RaceMaskForbidden);
        DbcU32Field("Class mask forbidden", doc_, row, skillability::ClassMaskForbidden);
        DbcU32Field("Min skill rank", doc_, row, skillability::MinSkillLineRank);
        DbcIdNameField("Superceded by", doc_, row, skillability::SupercededBySpell, cache, RefKind::Spell);
        DbcU32Field("Acquire method", doc_, row, skillability::AcquireMethod, "0 default / 1 on-get-skill / 2 auto");
        DbcU32Field("Trivial rank high", doc_, row, skillability::TrivialHigh, "Skill rank where it turns grey");
        DbcU32Field("Trivial rank low", doc_, row, skillability::TrivialLow);
        DbcU32Field("Character points 1", doc_, row, skillability::CharacterPoints1);
        DbcU32Field("Character points 2", doc_, row, skillability::CharacterPoints2);
        EndFieldTable();
    }
}

void SkillModule::DrawLinesTab()
{
    if (!lineDoc_.IsLoaded())
    {
        ImGui::TextWrapped("SkillLine.dbc not loaded.");
        return;
    }
    ImGui::TextDisabled("The skill definitions (professions, weapon/class skills, ...).");

    std::string previewStr = "(select a skill line)";
    if (lineSel_ >= 0 && static_cast<uint32_t>(lineSel_) < lineDoc_.RecordCount())
        previewStr = std::to_string(lineDoc_.GetU32(static_cast<uint32_t>(lineSel_), skillline::Id)) +
                     ": " + lineDoc_.GetStr(static_cast<uint32_t>(lineSel_), skillline::Name);
    ImGui::SetNextItemWidth(320);
    if (ImGui::BeginCombo("##linesel", previewStr.c_str()))
    {
        for (uint32_t r = 0; r < lineDoc_.RecordCount(); ++r)
        {
            std::string label = std::to_string(lineDoc_.GetU32(r, skillline::Id)) + ": " +
                                lineDoc_.GetStr(r, skillline::Name) + "##" + std::to_string(r);
            if (ImGui::Selectable(label.c_str(), lineSel_ == static_cast<int>(r)))
                lineSel_ = static_cast<int>(r);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Add skill line"))
    {
        uint32_t r = lineDoc_.AddRow();
        lineDoc_.SetU32(r, skillline::Id, lineDoc_.NextFreeId(skillline::Id));
        lineDoc_.SetStr(r, skillline::Name, "New Skill");
        lineSel_ = static_cast<int>(r);
        RebuildSkillNames();
    }
    if (lineSel_ < 0 || static_cast<uint32_t>(lineSel_) >= lineDoc_.RecordCount())
        return;

    const uint32_t lr = static_cast<uint32_t>(lineSel_);
    if (BeginFieldTable("##linegen"))
    {
        DbcU32Field("ID", lineDoc_, lr, skillline::Id);
        DbcU32Field("Category", lineDoc_, lr, skillline::CategoryID, "SkillLineCategory.dbc id");
        DbcU32Field("Skill costs id", lineDoc_, lr, skillline::SkillCostsID);
        DbcU32Field("Spell icon", lineDoc_, lr, skillline::SpellIconID, "SpellIcon.dbc id");
        DbcU32Field("Can link", lineDoc_, lr, skillline::CanLink, "1 = tradeskill window link");
        EndFieldTable();
    }
    DbcLangEditor("Name", lineDoc_, lr, skillline::Name);
    DbcLangEditor("Description", lineDoc_, lr, skillline::Description);
    DbcLangEditor("Alternate verb", lineDoc_, lr, skillline::AlternateVerb);
    RebuildSkillNames();  // cheap (150 rows) — keep the SkillLine resolution current
}

void SkillModule::SeedSampleExtra()
{
    if (lineDoc_.RecordCount() == 0)
    {
        lineDoc_.InitEmpty();
        uint32_t r = lineDoc_.AddRow();
        lineDoc_.SetU32(r, skillline::Id, 6);
        lineDoc_.SetStr(r, skillline::Name, "Frost");
        lineSel_ = 0;
        RebuildSkillNames();
    }
}
} // namespace we
