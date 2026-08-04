#pragma once

// DbcEditorModule — the UNIVERSAL client-DBC editor. One rail entry with a filterable picker over
// every DBC (from the vendored WoWDBDefs definitions ∪ the client's own listfile). Opening a DBC
// resolves its column layout from the DbcDefRegistry (curated -> .dbd for build 12340), validates
// that layout against the file header (physical field count + byte-accurate record size), and on a
// mismatch/no-def falls back to a raw all-UInt32 schema when the record is a clean 4-byte grid, or
// reports the layout as unsupported (packed custom DBC with no definition). The generic field-dump
// renderer and loose-overlay save are reused from the DBC common code. Needs client data loaded.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "clientdata/DbcSchema.h"
#include "editors/common/DbcDocument.h"
#include "editors/generic/DbcDefRegistry.h"

namespace we
{
struct EditorServices;

class DbcEditorModule final : public IEditorModule
{
public:
    const char* Id() const override { return "dbc"; }
    const char* DisplayName() const override { return "DBC Editor"; }
    const char* RailGlyph() const override { return "DBC"; }

    void Init(EditorServices* services) override { svc_ = services; }
    std::vector<PanelDesc> Panels() const override;
    void DrawPanels() override;
    void DrawModals() override {}

    void DrawFileMenu() override;
    void HandleShortcuts() override;

    void OnClientDataLoaded() override { namesLoaded_ = false; }

    bool HasRecord() const override { return selectedRow_ >= 0; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

private:
    void RefreshNames();
    void OpenDbc(const std::string& baseName);
    void RebuildFilter();
    void Save();
    void NewRow();
    void CloneSelected();
    void DeleteSelected();

    void DrawBrowser();
    void DrawEditor();
    void DrawEditorBody();  // field dump (no Begin/End; also used for capture)
    std::string RowLabel(uint32_t row);

    // Build a raw all-UInt32 schema of `fields` columns (only valid when recordSize == fields*4).
    const DbcSchema* BuildRawSchema(uint32_t fields);

    EditorServices* svc_ = nullptr;
    DbcDefRegistry  registry_;

    // DBC picker.
    std::vector<std::string> names_;        // base names ("Item", "PowerDisplay")
    bool                     namesLoaded_ = false;
    char                     filter_[64] = {0};

    // Active DBC.
    std::string      activeName_;
    std::string      source_;       // "curated" | "dbd" | "raw"
    std::string      unsupported_;  // non-empty => can't represent this layout; message for the UI
    DbcDocument      doc_;
    int              selectedRow_ = -1;
    char             search_[128] = {0};
    std::vector<uint32_t> filtered_;

    // Backing storage for a raw schema's field-name strings (const char* stability).
    std::deque<std::string> rawNames_;
    DbcSchema               rawSchema_;
};
} // namespace we
