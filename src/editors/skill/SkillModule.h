#pragma once

// SkillModule — edits SkillLineAbility.dbc (which spells belong to which skill line, for
// which races/classes, learn ranks) with SkillLine.dbc (the skill definitions) as an ExtraDoc.
// The companion to the Talent editor. Two tabs: General (skill line + spell pickers, masks,
// ranks) and Skills (pick + edit a SkillLine row). Mirrors TalentModule.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "editors/common/DbcDocument.h"
#include "editors/common/SimpleDbcEditorModule.h"
#include "editors/skill/SkillSchema.h"

namespace we
{
class SkillModule final : public SimpleDbcEditorModule
{
public:
    const char* Id() const override { return "skillline"; }
    const char* DisplayName() const override { return "Skill Line"; }
    const char* RailGlyph() const override { return "K"; }
    void Init(EditorServices* services) override;

protected:
    const DbcSchema& Schema() const override { return SkillLineAbilitySchema(); }
    const char* ArchivePath() const override { return "DBFilesClient\\SkillLineAbility.dbc"; }
    const char* BrowserTitle() const override { return "Skill Ability Browser"; }
    const char* EditorTitle() const override { return "Skill Ability Editor"; }
    const char* NounSingular() const override { return "skill ability"; }
    const char* NounPlural() const override { return "skill abilities"; }
    std::string RowLabel(uint32_t row) const override;
    int TabCount() const override { return 2; }
    const char* TabName(int tab) const override { return tab == 1 ? "Skill Lines" : "General"; }
    void DrawTab(int tab, uint32_t row) override;
    void OnLoaded() override;
    void SeedSampleExtra() override;
    std::vector<DbcDocument*> ExtraDocs() override { return {&lineDoc_}; }

private:
    void DrawGeneralTab(uint32_t row);
    void DrawLinesTab();
    std::string SkillName(uint32_t skillId) const;
    void RebuildSkillNames();

    DbcDocument lineDoc_;  // SkillLine.dbc
    std::unordered_map<uint32_t, std::string> skillNames_;  // SkillLine id -> enUS name
    int lineSel_ = -1;     // selected row in lineDoc_ (Skills tab)
};
} // namespace we
