#pragma once

// CreatureTextModule — edits creature_text (+ creature_text_locale): the lines a creature says/
// yells/emotes, grouped into interchangeable sets and fired by SmartAI/scripts (the TALK action).
// PK (CreatureID, GroupID, ID) with a locale child, so this is a bespoke IEditorModule scoped by
// CreatureID: a creature's full set of lines is edited as a list and saved via
// DbTableRepository::ReplaceChildren (delete-by-CreatureID + reinsert) for both the base table and
// the locale table. Each line has its 8 translations inline. Needs a live DB to browse.

#include <cstdint>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "editors/common/DbDocument.h"
#include "editors/common/DbTableRepository.h"
#include "editors/common/DbTableSchema.h"

namespace we
{
struct EditorServices;

// Shared column sets, exposed for the --emit-creaturetext-sql harness.
const std::vector<DbColumn>& CreatureTextColumns();
const std::vector<DbColumn>& CreatureTextLocaleColumns();

class CreatureTextModule final : public IEditorModule
{
public:
    const char* Id() const override { return "creaturetext"; }
    const char* DisplayName() const override { return "Creature Text"; }
    const char* RailGlyph() const override { return "H"; }

    void Init(EditorServices* services) override { svc_ = services; }
    std::vector<PanelDesc> Panels() const override;
    void DrawPanels() override;
    void DrawModals() override {}

    void DrawFileMenu() override;
    void HandleShortcuts() override;

    void OnConnected() override { listLoaded_ = false; }
    void OnDisconnected() override;

    std::vector<std::string> ReloadCommands() const override { return {".reload creature_text"}; }

    bool HasRecord() const override { return loaded_; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

private:
    void DrawBrowser();
    void DrawEditor();
    void DrawEditorBody();
    void DrawLineLocales(DbRecord& line);

    void RefreshList();
    void LoadCreature(uint32_t creatureId);
    void NewCreature();
    void Save();
    void DeleteCurrent();

    static constexpr int kListLimit = 500;

    EditorServices*   svc_ = nullptr;
    DbTableRepository repo_;

    struct CreatureSummary { uint32_t creatureId; uint32_t count; };
    std::vector<CreatureSummary> list_;
    bool                         listLoaded_ = false;
    char                         search_[64] = {0};

    bool                  loaded_ = false;
    bool                  present_ = false;
    bool                  dirty_ = false;
    uint32_t              creatureId_ = 0;
    std::vector<DbRecord> lines_;  // creature_text rows; each carries its locales in .locales
};
} // namespace we
