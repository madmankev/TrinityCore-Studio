#pragma once

// TalentModule — edits Talent.dbc (the individual talents) with TalentTab.dbc (the trees) as
// an ExtraDoc saved alongside. Two tabs: General (tree/tier/column, the 9 spell ranks as spell
// pickers, prereqs, flags) and Trees (pick + edit a TalentTab row). TabID resolves to the tree
// name; spell ranks resolve via the LookupCache spell names.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "editors/common/DbcDocument.h"
#include "editors/common/SimpleDbcEditorModule.h"
#include "editors/talent/TalentSchema.h"

namespace we
{
class TalentModule final : public SimpleDbcEditorModule
{
public:
    const char* Id() const override { return "talent"; }
    const char* DisplayName() const override { return "Talent"; }
    const char* RailGlyph() const override { return "L"; }
    void Init(EditorServices* services) override;

protected:
    const DbcSchema& Schema() const override { return TalentSchema(); }
    const char* ArchivePath() const override { return "DBFilesClient\\Talent.dbc"; }
    const char* BrowserTitle() const override { return "Talent Browser"; }
    const char* EditorTitle() const override { return "Talent Editor"; }
    const char* NounSingular() const override { return "talent"; }
    const char* NounPlural() const override { return "talents"; }
    std::string RowLabel(uint32_t row) const override;
    int TabCount() const override { return 2; }
    const char* TabName(int tab) const override { return tab == 1 ? "Trees (TalentTab)" : "General"; }
    void DrawTab(int tab, uint32_t row) override;
    void OnRowSeeded(uint32_t row) override;
    void OnLoaded() override;
    void SeedSampleExtra() override;
    std::vector<DbcDocument*> ExtraDocs() override { return {&tabDoc_}; }

private:
    void DrawGeneralTab(uint32_t row);
    void DrawTreesTab();
    std::string TreeName(uint32_t tabId) const;
    void RebuildTreeNames();

    DbcDocument tabDoc_;  // TalentTab.dbc
    std::unordered_map<uint32_t, std::string> treeNames_;  // TabID -> enUS tree name
    int treeSel_ = -1;    // selected row in tabDoc_ (Trees tab)
};
} // namespace we
