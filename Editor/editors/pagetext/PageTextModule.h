#pragma once

// PageTextModule — edits page_text (+ page_text_locale): the book/scroll/letter pages an item or
// gameobject shows (item_template.PageText / gameobject page chains). A thin SimpleDbEditorModule:
// single integer PK, a NextPageID chain, and a localized Text child. The base handles browse,
// load, transactional save, and the harness.

#include <string>
#include <vector>

#include "editors/common/SimpleDbEditorModule.h"

namespace we
{
struct DbTableSchema;
const DbTableSchema& PageTextSchema();  // exposed for --emit-pagepoi-sql

class PageTextModule final : public SimpleDbEditorModule
{
public:
    const char* Id() const override { return "pagetext"; }
    const char* DisplayName() const override { return "Page Text"; }
    const char* RailGlyph() const override { return "F"; }
    std::vector<std::string> ReloadCommands() const override { return {".reload page_text"}; }

protected:
    const DbTableSchema& Schema() const override;
    const char* BrowserTitle() const override { return "Page Text Browser"; }
    const char* EditorTitle() const override { return "Page Text Editor"; }
    const char* NounSingular() const override { return "page"; }
    const char* NounPlural() const override { return "pages"; }
    std::string RowLabel(const DbRecord& rec) const override;
    int TabCount() const override { return 1; }
    const char* TabName(int tab) const override { return "Page"; }
    void DrawTab(int tab, DbRecord& rec) override;
};
} // namespace we
