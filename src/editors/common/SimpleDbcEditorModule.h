#pragma once

// SimpleDbcEditorModule — a reusable IEditorModule base for editors of a single primary
// DBC (CharTitles, SpellItemEnchantment, ...). It provides the whole browse + tabbed-edit
// + loose-save skeleton; a concrete editor supplies only its schema, archive path, a row
// label, and its tab set. A subclass may also own EXTRA related tables (returned from
// ExtraDocs()) that are loaded and saved alongside the primary one — the achievement
// editor does this for its Criteria/Category DBCs.
//
// The base owns: the primary DbcDocument, selection/search state, both dock panels, the
// File menu (New/Clone/Delete/Save), Ctrl+N / Ctrl+S, client-data load, overlay save of
// every dirty document, the status-bar summary, and the headless harness hooks.

#include <cstdint>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "clientdata/DbcSchema.h"
#include "editors/common/DbcDocument.h"

namespace we
{
class SimpleDbcEditorModule : public IEditorModule
{
public:
    void Init(EditorServices* services) override { svc_ = services; }

    std::vector<PanelDesc> Panels() const override
    {
        return {{BrowserTitle(), DockSlot::Left, true}, {EditorTitle(), DockSlot::Center, true}};
    }
    void DrawPanels() override;
    void DrawModals() override {}  // subclasses with popups/windows override this
    void DrawFileMenu() override;
    void HandleShortcuts() override;
    void OnClientDataLoaded() override { LoadTable(); }

    bool HasRecord() const override { return selectedRow_ >= 0; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void SelectTab(int tab) override { curTab_ = tab; }
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

protected:
    // --- required per-editor identity/content ---
    virtual const DbcSchema& Schema() const = 0;
    virtual const char* ArchivePath() const = 0;     // "DBFilesClient\\CharTitles.dbc"
    virtual const char* BrowserTitle() const = 0;    // globally-unique panel titles
    virtual const char* EditorTitle() const = 0;
    virtual std::string RowLabel(uint32_t row) const = 0;  // browser/summary label ("id: name")
    virtual int TabCount() const = 0;
    virtual const char* TabName(int tab) const = 0;
    virtual void DrawTab(int tab, uint32_t row) = 0;       // draws one tab's BODY (no BeginTabItem)

    // --- optional hooks ---
    virtual uint32_t IdColumn() const { return 0; }
    virtual const char* NounSingular() const { return "record"; }
    virtual const char* NounPlural() const { return "records"; }
    virtual std::vector<DbcDocument*> ExtraDocs() { return {}; }  // extra tables to load + save
    virtual void OnRowSeeded(uint32_t /*row*/) {}                 // defaults for a freshly-added row
    virtual void OnLoaded() {}                                    // after doc_ + ExtraDocs() loaded
    virtual void SeedSampleExtra() {}                            // harness: seed extra tables
    virtual void OnAfterSave() {}  // after the overlay save (e.g. project to a server mirror)

    // --- shared services for subclasses ---
    EditorServices* svc_ = nullptr;
    DbcDocument     doc_;
    int             selectedRow_ = -1;
    int             curTab_ = 0;

    void LoadTable();
    void RebuildFilter();
    void Save();
    void NewRow();
    void CloneSelected();
    void DeleteSelected();
    bool AnyDirty() const;

    void DrawBrowserPanel();
    void DrawEditorPanel();

    std::string           loadError_;
    char                  search_[128] = {0};
    std::vector<uint32_t> filtered_;  // primary-table row indices matching search_
};
} // namespace we
