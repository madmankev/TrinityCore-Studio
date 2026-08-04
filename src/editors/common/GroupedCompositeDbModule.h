#pragma once

// GroupedCompositeDbModule — a reusable IEditorModule base for editing a GROUP of composite-PK
// world-DB tables from one rail entry (a dropdown selects the active table). The composite-key
// twin of GroupedDbEditorModule: each table is a CompositeDbTableSchema, a row is identified by
// its key-column tuple, and List/Load/Save/Delete go through CompositeDbRepository (delete-by-key
// + reinsert, so editing a key column is fine). The editor is a generic field dump. Needs a live
// DB connection to browse.

#include <cstdint>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "editors/common/CompositeDbRepository.h"
#include "editors/common/CompositeDbSchema.h"
#include "editors/common/DbDocument.h"

namespace we
{
class GroupedCompositeDbModule : public IEditorModule
{
public:
    static constexpr int kListLimit = 500;

    void Init(EditorServices* services) override { svc_ = services; }
    std::vector<PanelDesc> Panels() const override
    {
        return {{BrowserTitle(), DockSlot::Left, true}, {EditorTitle(), DockSlot::Center, true}};
    }
    void DrawPanels() override;
    void DrawModals() override {}
    void DrawFileMenu() override;
    void HandleShortcuts() override;
    void OnConnected() override { listLoaded_ = false; }
    void OnDisconnected() override;
    bool HasRecord() const override { return recordLoaded_; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void SelectTab(int tab) override { (void)tab; }
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

protected:
    virtual const std::vector<const CompositeDbTableSchema*>& Tables() const = 0;
    virtual const char* BrowserTitle() const = 0;
    virtual const char* EditorTitle() const = 0;

    EditorServices*        svc_ = nullptr;
    CompositeDbRepository  repo_;
    DbRecord               record_;
    bool                   recordLoaded_ = false;
    int                    active_ = 0;

    const CompositeDbTableSchema& ActiveSchema() const
    {
        return *Tables()[static_cast<size_t>(active_)];
    }
    std::vector<std::string> KeyOf(const DbRecord& rec) const;  // key cell values, in key order
    std::string KeyLabel(const DbRecord& rec) const;
    std::string RowLabel(const DbRecord& rec) const;
    void RefreshList();
    void LoadRow(const DbRecord& listRow);
    void Save();
    void NewRow();
    void DeleteSelected();
    void DrawBrowser();
    void DrawEditor();
    void DrawEditorBody();

    std::vector<DbRecord>    list_;
    bool                     listLoaded_ = false;
    char                     search_[128] = {0};
    std::vector<std::string> origKey_;   // key of the loaded row (delete target on save)
};
} // namespace we
