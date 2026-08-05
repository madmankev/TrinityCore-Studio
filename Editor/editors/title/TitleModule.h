#pragma once

// TitleModule — edits CharTitles.dbc (player titles). A thin SimpleDbcEditorModule: it
// supplies the schema, archive path, row label, and its two tabs (General + Locales for
// the male & female name forms). Save writes the loose overlay file; the base handles the
// browser, editor shell, File menu, Ctrl+N/S, and harness.

#include <cstdint>
#include <string>

#include "editors/common/SimpleDbcEditorModule.h"
#include "editors/title/CharTitlesSchema.h"

namespace we
{
class TitleModule final : public SimpleDbcEditorModule
{
public:
    const char* Id() const override { return "title"; }
    const char* DisplayName() const override { return "Title"; }
    const char* RailGlyph() const override { return "T"; }

protected:
    const DbcSchema& Schema() const override { return CharTitlesSchema(); }
    const char* ArchivePath() const override { return "DBFilesClient\\CharTitles.dbc"; }
    const char* BrowserTitle() const override { return "Title Browser"; }
    const char* EditorTitle() const override { return "Title Editor"; }
    const char* NounSingular() const override { return "title"; }
    const char* NounPlural() const override { return "titles"; }
    std::string RowLabel(uint32_t row) const override;
    int TabCount() const override { return 2; }
    const char* TabName(int tab) const override { return tab == 1 ? "Locales" : "General"; }
    void DrawTab(int tab, uint32_t row) override;
    void OnRowSeeded(uint32_t row) override;
};
} // namespace we
