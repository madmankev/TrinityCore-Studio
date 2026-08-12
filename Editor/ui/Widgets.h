#pragma once

// Layer E (ui) — reusable ImGui widgets for the quest editor. Every widget
// returns bool == "value changed this frame" so tabs can OR the results into
// QuestEditorContext::changed. Widgets are ID-safe (PushID/PopID internally) and
// contain no raw SQL/mysql — id/name resolution goes through LookupCache.

#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"
#include "ui/Enums.h"

namespace we
{
class LookupCache;

// std::string-backed InputText (imgui_stdlib is not vendored into the build, so
// we provide our own CallbackResize wrapper here).
bool InputTextString(const char* label, std::string& v, ImGuiInputTextFlags flags = 0);
bool InputMultiline(const char* label, std::string& v, float height = 80.0f);

// Fixed-width integer fields. InputScalar with the matching ImGuiDataType keeps
// the stored value clamped to the type's range automatically.
bool InputU32(const char* label, uint32_t& v);
bool InputI32(const char* label, int32_t& v);
bool InputU16(const char* label, uint16_t& v);
bool InputU8(const char* label, uint8_t& v);
bool InputI16(const char* label, int16_t& v);
bool InputFloatField(const char* label, float& v);

// Id inputs that also show a resolved name to the right (dimmed). `name` is the
// looked-up display string ("" shows nothing). The `id` is used only for the
// widget's ImGui ID. Returns true if the value changed this frame.
bool InputU32Named(const char* id, uint32_t& v, const std::string& name);
bool InputU16Named(const char* id, uint16_t& v, const std::string& name);
bool InputU8Named(const char* id, uint8_t& v, const std::string& name);
bool InputI16Named(const char* id, int16_t& v, const std::string& name);
bool InputI32Named(const char* id, int32_t& v, const std::string& name);

// Combo over an EnumEntry table. Values not present in the table are shown as a
// raw "<n> (custom)" preview so out-of-table data round-trips.
bool EnumCombo(const char* label, uint32_t& value, const std::vector<EnumEntry>& entries);

// Checkbox grid over a FlagEntry bitmask table.
bool FlagCheckboxGrid(const char* label, uint32_t& bits, const std::vector<FlagEntry>& entries,
                      int columns = 4);

// Copper money editor: gold/silver/copper fields plus a MoneyToString preview.
bool MoneyEditor(const char* label, int32_t& copper);

// Emote combo backed by EmoteValues().
bool EmoteCombo(const char* label, uint16_t& value);

// Which lookup table a picker resolves against.
enum class RefKind
{
    Item,
    Creature,
    GameObject,
    Faction,   // Faction.dbc (client data)
    Skill,     // SkillLine.dbc
    Spell,     // Spell.dbc
    Title,     // CharTitles.dbc
    Area,      // AreaTable.dbc (zones)
    Quest,     // quest_template titles (world DB)
    FactionTemplate, // FactionTemplate.dbc -> Faction.dbc name (creature_template.faction)
    MailTemplate     // MailTemplate.dbc subject (quest_template_addon.RewardMailTemplateID)
};

// Id field + resolved label + "..." search popup. Works with an unloaded cache
// (then it simply shows the number and "not found"). Overloads adapt narrower
// id fields (uint16/uint8) used by faction/skill/title columns.
bool IdNamePicker(const char* label, uint32_t& id, LookupCache& cache, RefKind kind);
bool IdNamePicker(const char* label, uint16_t& id, LookupCache& cache, RefKind kind);
bool IdNamePicker(const char* label, uint8_t& id, LookupCache& cache, RefKind kind);
bool IdNamePicker(const char* label, int32_t& id, LookupCache& cache, RefKind kind);

// Signed RequiredNpcOrGo picker: value > 0 == creature, < 0 == gameobject.
bool IdNamePickerSigned(const char* label, int32_t& value, LookupCache& cache);

// ---------------------------------------------------------------------------
// Shared field layout — a 2-column (label | full-width widget) grid. Using this
// everywhere keeps tabs visually consistent and guarantees the value widget
// stretches to fit (never runs off the right edge). Pattern:
//
//   if (BeginFieldTable("mytable")) {
//       FieldRow("QuestLevel", "tooltip");   InputI16("##ql", t.questLevel);
//       FieldRow("MinLevel");                InputU8 ("##ml", t.minLevel);
//       EndFieldTable();
//   }
//
// The widget drawn immediately after FieldRow lands in the value column and is
// pre-sized to the full column width (pass a "##" hidden label to it).
bool BeginFieldTable(const char* id, float labelWidth = 190.0f);
void FieldRow(const char* label, const char* tooltip = nullptr);
void EndFieldTable();

// ---------------------------------------------------------------------------
// Studio workbench primitives. These deliberately use only ImDrawList + ImGui
// state so every module can share a polished visual hierarchy without becoming
// coupled to a particular editor/document implementation.
enum class StudioButtonTone { Primary, Secondary, Quiet, Danger };

// Buttons with consistent hierarchy for primary/destructive/quiet actions.
bool StudioButton(const char* label, StudioButtonTone tone = StudioButtonTone::Secondary,
                  const ImVec2& size = ImVec2(0, 0));

// A compact status badge that participates in normal ImGui layout.
void StudioPill(const char* label, const ImVec4& background, const ImVec4& foreground);

// A card-like section header for browsers, inspectors, and document panels.
void StudioPanelHeader(const char* eyebrow, const char* title, const char* description = nullptr,
                       const char* badge = nullptr, const ImVec4* badgeColor = nullptr);

// A non-interactive empty state with a readable next action. Use this instead
// of leaving a blank dock surface when no record/client data is available.
void StudioEmptyState(const char* glyph, const char* title, const char* description);
} // namespace we
