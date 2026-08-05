#pragma once

// GroupedDbEditorModule — a reusable IEditorModule base for editing a GROUP of simple
// single-PK world-DB tables from one rail entry (a dropdown selects the active table). The DB
// twin of GroupedDbcModule. Each table is a DbTableSchema; the editor is a generic field dump
// driven by the schema's columns, and List/Load/Save/Delete go through the generic
// DbTableRepository. Needs a live DB connection to browse.

#include <cstdint>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "editors/common/DbDocument.h"
#include "editors/common/DbTableRepository.h"
#include "editors/common/DbTableSchema.h"

namespace we
{
class GroupedDbEditorModule : public IEditorModule
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
    virtual const std::vector<const DbTableSchema*>& Tables() const = 0;
    virtual const char* BrowserTitle() const = 0;
    virtual const char* EditorTitle() const = 0;

    EditorServices*   svc_ = nullptr;
    DbTableRepository repo_;
    DbRecord          record_;
    bool              recordLoaded_ = false;
    int               active_ = 0;

    const DbTableSchema& ActiveSchema() const { return *Tables()[static_cast<size_t>(active_)]; }
    std::string RowLabel(const DbRecord& rec) const;
    void RefreshList();
    void LoadRecord(uint32_t id);
    void Save();
    void NewRow();
    void CloneSelected();
    void DeleteSelected();
    void DrawBrowser();
    void DrawEditor();
    void DrawEditorBody();

    std::vector<DbRecord> list_;
    bool                  listLoaded_ = false;
    char                  search_[128] = {0};
    int                   selectedId_ = -1;
};
} // namespace we
