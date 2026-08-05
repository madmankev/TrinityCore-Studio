#pragma once

// SpellModule — edits the client Spell.dbc (49,839 rows × 234 fields) with a bespoke 7-tab
// UI on the SimpleDbcEditorModule base. Phase 1 = the client DBC only (loose overlay save);
// later phases project the definition to the server spell_dbc and add server spell_* tabs.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "editors/common/DbDocument.h"
#include "editors/common/DbTableRepository.h"
#include "editors/common/SimpleDbcEditorModule.h"
#include "editors/spell/SpellRepository.h"
#include "editors/spell/SpellSchema.h"

namespace we
{
struct DbTableSchema;
struct SpellChildSpec;

class SpellModule final : public SimpleDbcEditorModule
{
public:
    const char* Id() const override { return "spell"; }
    const char* DisplayName() const override { return "Spell"; }
    const char* RailGlyph() const override { return "S"; }
    void OnConnected() override { serverForId_ = kNoServer; }
    void OnDisconnected() override { serverForId_ = kNoServer; }
    std::vector<std::string> ReloadCommands() const override;

protected:
    const DbcSchema& Schema() const override { return SpellSchema(); }
    const char* ArchivePath() const override { return "DBFilesClient\\Spell.dbc"; }
    const char* BrowserTitle() const override { return "Spell Browser"; }
    const char* EditorTitle() const override { return "Spell Editor"; }
    const char* NounSingular() const override { return "spell"; }
    const char* NounPlural() const override { return "spells"; }
    std::string RowLabel(uint32_t row) const override;
    int TabCount() const override { return 11; }
    const char* TabName(int tab) const override;
    void DrawTab(int tab, uint32_t row) override;
    void OnRowSeeded(uint32_t row) override;
    void OnLoaded() override;      // load index-resolution maps (duration/range/icon/...)
    void OnAfterSave() override;  // project the DBC row to server spell_dbc

private:
    // DBC tabs (client Spell.dbc).
    void DrawGeneralTab(uint32_t row);
    void DrawAttributesTab(uint32_t row);
    void DrawCastingTab(uint32_t row);
    void DrawTargetingTab(uint32_t row);
    void DrawEffectsTab(uint32_t row);
    void DrawTextTab(uint32_t row);
    void DrawMiscTab(uint32_t row);

    // Server tabs (world DB, loaded lazily per selected spell).
    void DrawServerCoreTab(uint32_t spellId);   // proc / bonus / threat / custom_attr
    void DrawServerLinksTab(uint32_t spellId);  // required / learn / linked / ranks
    void DrawServerAdvancedTab(uint32_t spellId); // difficulty / area / target_position / pet_auras
    void DrawServerScriptsTab(uint32_t spellId);  // script_names / scripts / loot / groups
    bool RequireDbAndLoad(uint32_t spellId);      // false + message if not connected
    void Draw1to1(const char* title, const DbTableSchema& schema, DbRecord& rec, uint32_t spellId);
    void DrawChildList(const char* title, const SpellChildSpec& spec, std::vector<DbRecord>& rows,
                       uint32_t spellId);

    // Inline resolution of Spell.dbc index fields into the shared reference tables.
    std::string ResolveIndex(const std::unordered_map<uint32_t, std::string>& m, uint32_t id) const;
    void DrawIndexField(const char* label, uint32_t row, uint32_t col,
                        const std::unordered_map<uint32_t, std::string>& m, const char* tip = nullptr);
    std::unordered_map<uint32_t, std::string> durationById_, castTimeById_, rangeById_,
        radiusById_, iconById_;

    static constexpr uint32_t kNoServer = 0xFFFFFFFFu;

    SpellRepository   spellRepo_;
    DbTableRepository dbRepo_;
    uint32_t          serverForId_ = kNoServer;  // spell id the server data below was loaded for

    DbRecord proc_, bonus_, threat_, customAttr_, difficulty_;
    std::vector<DbRecord> required_, learn_, linked_, ranks_, area_, targetPos_, petAuras_,
        scriptNames_, scripts_, loot_, groups_;
};
} // namespace we
