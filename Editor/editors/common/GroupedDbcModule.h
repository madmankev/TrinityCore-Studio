#pragma once

// GroupedDbcModule — a reusable IEditorModule base for editing a GROUP of related client
// DBCs from one rail entry (a dropdown selects the active table). Each table keeps its own
// edit state (one DbcDocument per table), and Save writes every dirty one to the overlay.
// The editor is a GENERIC field dump driven by each table's DbcSchema (scalars in a field
// table, LangStrings as locale blocks). Use for small reference tables (SpellDuration,
// SpellRange, ...) that are too trivial to each warrant their own rail entry.

#include <cstdint>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "clientdata/DbcSchema.h"
#include "editors/common/DbcDocument.h"

namespace we
{
struct DbcTableDef
{
    const char*      name;         // dropdown label
    const char*      archivePath;  // "DBFilesClient\\SpellDuration.dbc"
    const DbcSchema* schema;
    std::vector<uint32_t> labelCols;  // physical columns shown in the browser after the id
};

class GroupedDbcModule : public IEditorModule
{
public:
    void Init(EditorServices* services) override { svc_ = services; }
    std::vector<PanelDesc> Panels() const override
    {
        return {{BrowserTitle(), DockSlot::Left, true}, {EditorTitle(), DockSlot::Center, true}};
    }
    void DrawPanels() override;
    void DrawModals() override {}
    void DrawFileMenu() override;
    void HandleShortcuts() override;
    void OnClientDataLoaded() override { LoadAll(); }
    bool HasRecord() const override { return selectedRow_ >= 0; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void SelectTab(int tab) override { (void)tab; }
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

protected:
    virtual const std::vector<DbcTableDef>& Tables() const = 0;
    virtual const char* BrowserTitle() const = 0;
    virtual const char* EditorTitle() const = 0;

    EditorServices* svc_ = nullptr;
    std::vector<DbcDocument> docs_;   // one per table (index = table index)
    int  active_ = 0;
    int  selectedRow_ = -1;
    char search_[128] = {0};
    std::vector<uint32_t> filtered_;
    std::string loadError_;

    void LoadAll();
    void RebuildFilter();
    void Save();
    void NewRow();
    void CloneSelected();
    void DeleteSelected();
    void DrawBrowser();
    void DrawEditor();
    void DrawEditorBody();  // field dump (no Begin/End; also used for capture)
    DbcDocument& ActiveDoc() { return docs_[static_cast<size_t>(active_)]; }
    const DbcSchema& ActiveSchema() const { return *Tables()[static_cast<size_t>(active_)].schema; }
    std::string RowLabel(uint32_t row);
};
} // namespace we
