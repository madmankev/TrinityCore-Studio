#pragma once

// SimpleDbEditorModule — a reusable IEditorModule base for editors of a single world-DB table
// (+ optional locale child), the DB twin of SimpleDbcEditorModule. It provides the browser +
// tabbed editor + File menu (New/Clone/Delete/Save) + transactional save (Live/SqlExport) +
// status + harness; a concrete editor supplies only its DbTableSchema, identity, a row label,
// and its tabs. Reuses the generic DbTableRepository (over IDatabase + we::sql helpers).

#include <cstdint>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "editors/common/DbDocument.h"
#include "editors/common/DbTableRepository.h"
#include "editors/common/DbTableSchema.h"

namespace we
{
class SimpleDbEditorModule : public IEditorModule
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
    void SelectTab(int tab) override { curTab_ = tab; }
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

protected:
    virtual const DbTableSchema& Schema() const = 0;
    virtual const char* BrowserTitle() const = 0;
    virtual const char* EditorTitle() const = 0;
    virtual std::string RowLabel(const DbRecord& rec) const = 0;
    virtual int TabCount() const = 0;
    virtual const char* TabName(int tab) const = 0;
    virtual void DrawTab(int tab, DbRecord& rec) = 0;

    virtual const char* NounSingular() const { return "record"; }
    virtual const char* NounPlural() const { return "records"; }
    virtual void OnRecordSeeded(DbRecord& /*rec*/) {}  // defaults for a freshly-created record

    EditorServices*   svc_ = nullptr;
    DbTableRepository repo_;
    DbRecord          record_;
    bool              recordLoaded_ = false;
    int               curTab_ = 0;

    void RefreshList();
    void LoadRecord(uint32_t id);
    void Save();
    void NewRow();
    void CloneSelected();
    void DeleteSelected();

    void DrawBrowserPanel();
    void DrawEditorPanel();

    std::vector<DbRecord> list_;
    bool                  listLoaded_ = false;
    char                  search_[128] = {0};
    int                   selectedId_ = -1;
};
} // namespace we
