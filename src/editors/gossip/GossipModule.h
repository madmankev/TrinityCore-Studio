#pragma once

// GossipModule — edits an NPC gossip MENU: gossip_menu (the menu -> npc_text links) plus
// gossip_menu_option (+ _locale) (the clickable options). All three key on MenuID, so this is a
// bespoke IEditorModule scoped by MenuID: a menu's text links and options are edited as lists and
// saved via DbTableRepository::ReplaceChildren (delete-by-MenuID + reinsert) for each table. Each
// option carries its 8 translations (OptionText / BoxText) inline. Option icon/type render as
// enum combos; ActionMenuID chains to the next menu. Needs a live DB to browse. (The wide npc_text
// greeting table and page_text / points_of_interest are separate editors.)

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

// Shared column sets, exposed for the --emit-gossip-sql harness.
const std::vector<DbColumn>& GossipMenuColumns();
const std::vector<DbColumn>& GossipOptionColumns();
const std::vector<DbColumn>& GossipOptionLocaleColumns();

class GossipModule final : public IEditorModule
{
public:
    const char* Id() const override { return "gossip"; }
    const char* DisplayName() const override { return "Gossip"; }
    const char* RailGlyph() const override { return "D"; }

    void Init(EditorServices* services) override { svc_ = services; }
    std::vector<PanelDesc> Panels() const override;
    void DrawPanels() override;
    void DrawModals() override {}

    void DrawFileMenu() override;
    void HandleShortcuts() override;

    void OnConnected() override { listLoaded_ = false; }
    void OnDisconnected() override;

    std::vector<std::string> ReloadCommands() const override
    {
        return {".reload gossip_menu", ".reload gossip_menu_option"};
    }

    bool HasRecord() const override { return loaded_; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

private:
    void DrawBrowser();
    void DrawEditor();
    void DrawEditorBody();
    void DrawOptionLocales(DbRecord& option);

    void RefreshList();
    void LoadMenu(uint32_t menuId);
    void NewMenu();
    void Save();
    void DeleteCurrent();

    static constexpr int kListLimit = 500;

    EditorServices*   svc_ = nullptr;
    DbTableRepository repo_;

    struct MenuSummary { uint32_t menuId; uint32_t optionCount; };
    std::vector<MenuSummary> list_;
    bool                     listLoaded_ = false;
    char                     search_[64] = {0};

    bool                  loaded_ = false;
    bool                  present_ = false;
    bool                  dirty_ = false;
    uint32_t              menuId_ = 0;
    std::vector<DbRecord> textLinks_;  // gossip_menu rows (MenuID, TextID)
    std::vector<DbRecord> options_;    // gossip_menu_option rows; locales in each .locales
};
} // namespace we
