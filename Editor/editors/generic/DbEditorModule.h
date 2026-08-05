#pragma once

// DbEditorModule — the UNIVERSAL world-DB editor. One rail entry with a filterable picker over every
// table (SHOW TABLES); opening a table uses a curated schema when the registry has one, else a schema
// built live from SHOW COLUMNS. Single integer PK -> DbTableRepository (upsert); composite / no /
// non-integer PK -> CompositeDbRepository (delete-by-key + reinsert). The generic field-dump renderer
// and save paths are reused unchanged. Needs a live DB connection.

#include <cstdint>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "editors/common/CompositeDbRepository.h"
#include "editors/common/DbDocument.h"
#include "editors/common/DbTableRepository.h"
#include "editors/generic/RuntimeSchema.h"

namespace we
{
struct EditorServices;
struct DbTableSchema;
struct CompositeDbTableSchema;

class DbEditorModule final : public IEditorModule
{
public:
    const char* Id() const override { return "db"; }
    const char* DisplayName() const override { return "DB Editor"; }
    const char* RailGlyph() const override { return "DB"; }

    void Init(EditorServices* services) override { svc_ = services; }
    std::vector<PanelDesc> Panels() const override;
    void DrawPanels() override;
    void DrawModals() override {}

    void DrawFileMenu() override;
    void HandleShortcuts() override;

    void OnConnected() override { tablesLoaded_ = false; }
    void OnDisconnected() override;

    std::vector<std::string> ReloadCommands() const override;

    bool HasRecord() const override { return recordLoaded_; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

private:
    enum class Mode { None, Single, Composite };

    void DrawBrowser();
    void DrawEditor();
    void DrawEditorBody();

    void RefreshTables();
    void OpenTable(const std::string& table);
    void RefreshList();
    void LoadFromRow(const DbRecord& listRow);
    void Save();
    void NewRow();
    void DeleteCurrent();

    const std::vector<DbColumn>& ActiveCols() const;   // columns to render in the editor
    std::vector<std::string> KeyOf(const DbRecord& rec) const;  // composite key values in key order
    std::string RowLabel(const DbRecord& rec) const;

    static constexpr int kListLimit = 500;

    EditorServices*       svc_ = nullptr;
    DbTableRepository     singleRepo_;
    CompositeDbRepository compRepo_;

    // Table picker.
    std::vector<std::string> tables_;
    bool                     tablesLoaded_ = false;
    char                     tableFilter_[64] = {0};

    // Active table + its schema (curated pointer or the runtime-owned schema).
    std::string                   activeTable_;
    Mode                          mode_ = Mode::None;
    const DbTableSchema*          singleSchema_ = nullptr;
    const CompositeDbTableSchema* compSchema_ = nullptr;
    RuntimeTableSchema            runtime_;   // owns strings when introspected
    bool                          curated_ = false;

    // Row browser + loaded record.
    std::vector<DbRecord>    list_;
    bool                     listLoaded_ = false;
    char                     search_[128] = {0};
    DbRecord                 record_;
    bool                     recordLoaded_ = false;
    bool                     present_ = false;
    bool                     dirty_ = false;
    int                      selectedId_ = -1;   // Single mode
    std::vector<std::string> origKey_;           // Composite mode
};
} // namespace we
