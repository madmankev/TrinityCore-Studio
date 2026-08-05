// ConditionsModule — see ConditionsModule.h.

#include "editors/conditions/ConditionsModule.h"

#include <cstdio>

#include "imgui.h"

#include "app/EditorServices.h"
#include "editors/common/DbEditWidgets.h"
#include "ui/Widgets.h"
#include "ui/Enums.h"

namespace we
{
namespace
{
const char* SourceTypeName(int32_t type)
{
    if (type < 0)
        return "Reference";
    const char* n = LabelFor(ConditionSourceTypeValues(), static_cast<uint32_t>(type));
    return n ? n : "Unknown";
}

// Tooltip describing SourceGroup/SourceEntry for a source type (nullptr when unknown).
const char* SourceTypeMeaning(int32_t type)
{
    if (type < 0)
        return "Negative type = a conditions_reference set id.";
    for (const EnumEntry& e : ConditionSourceTypeValues())
        if (static_cast<int32_t>(e.value) == type)
            return e.tooltip;
    return nullptr;
}
} // namespace

std::vector<PanelDesc> ConditionsModule::Panels() const
{
    return {
        {"Condition Source Browser", DockSlot::Left, true},
        {"Condition Editor", DockSlot::Center, true},
    };
}

void ConditionsModule::OnDisconnected()
{
    list_.clear();
    listLoaded_ = false;
    loaded_ = false;
    present_ = false;
    dirty_ = false;
    rows_.clear();
}

// --- list / load / save ----------------------------------------------------
void ConditionsModule::RefreshList()
{
    list_.clear();
    listLoaded_ = true;
    if (svc_ && svc_->activeDb)
    {
        DbError e = repo_.ListSources(*svc_->activeDb, filterType_, search_, kListLimit, list_);
        if (!e.ok && svc_->setStatus)
            svc_->setStatus("List failed: " + e.message);
    }
}

void ConditionsModule::LoadSource(const ConditionSourceKey& key)
{
    if (!svc_ || !svc_->activeDb)
        return;
    DbError e = repo_.LoadSource(*svc_->activeDb, key, rows_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Load failed: " + e.message);
        return;
    }
    origKey_ = key;
    curKey_ = key;
    loaded_ = true;
    present_ = true;
    dirty_ = false;
}

void ConditionsModule::NewSource()
{
    if (!svc_ || !svc_->connected)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Connect a database first.");
        return;
    }
    curKey_ = ConditionSourceKey{19, 0, 0};  // default to a quest-available source
    origKey_ = curKey_;
    rows_.clear();
    loaded_ = true;
    present_ = false;
    dirty_ = true;
}

void ConditionsModule::Save()
{
    if (!loaded_)
        return;
    if (!svc_ || !svc_->activeDb)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Not connected — cannot save.");
        return;
    }
    DbError e = repo_.SaveSource(*svc_->activeDb, origKey_, curKey_, rows_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Save failed: " + e.message);
        return;
    }
    origKey_ = curKey_;
    present_ = true;
    dirty_ = false;
    if (svc_->setStatus)
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s %zu condition row(s) for %s (grp %u, entry %d)",
                      svc_->mode == WriteMode::SqlExport ? "Exported" : "Saved", rows_.size(),
                      SourceTypeName(curKey_.type), curKey_.group, curKey_.entry);
        svc_->setStatus(buf);
    }
    if (svc_->mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    RefreshList();
}

void ConditionsModule::DeleteCurrent()
{
    if (!loaded_ || !present_ || !svc_ || !svc_->activeDb)
        return;
    DbError e = repo_.DeleteSource(*svc_->activeDb, origKey_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Delete failed: " + e.message);
        return;
    }
    if (svc_->setStatus)
        svc_->setStatus("Deleted conditions for " + std::string(SourceTypeName(origKey_.type)));
    loaded_ = false;
    present_ = false;
    dirty_ = false;
    rows_.clear();
    RefreshList();
}

// --- menus / shortcuts -----------------------------------------------------
void ConditionsModule::DrawFileMenu()
{
    const bool connected = svc_ && svc_->connected;
    if (ImGui::MenuItem("New Condition Source", "Ctrl+N", false, connected))
        NewSource();
    if (ImGui::MenuItem("Delete Source", nullptr, false, loaded_ && present_ && connected))
        DeleteCurrent();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, loaded_ && connected))
        Save();
}

void ConditionsModule::HandleShortcuts()
{
    const bool connected = svc_ && svc_->connected;
    if (connected && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewSource();
    if (connected && loaded_ && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void ConditionsModule::DrawPanels()
{
    DrawBrowserPanel();
    DrawEditorPanel();
}

void ConditionsModule::DrawBrowserPanel()
{
    if (!ImGui::Begin("Condition Source Browser"))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to browse conditions (world DB).");
        ImGui::End();
        return;
    }
    if (!listLoaded_)
        RefreshList();

    // Source-type filter.
    ImGui::TextUnformatted("Source type");
    ImGui::SetNextItemWidth(-1);
    const char* cur = filterType_ < 0 ? "(all types)" : SourceTypeName(filterType_);
    if (ImGui::BeginCombo("##typefilter", cur))
    {
        if (ImGui::Selectable("(all types)", filterType_ < 0))
        {
            filterType_ = -1;
            RefreshList();
        }
        for (const EnumEntry& e : ConditionSourceTypeValues())
        {
            bool sel = filterType_ == static_cast<int>(e.value);
            char lbl[96];
            std::snprintf(lbl, sizeof(lbl), "%u  %s", e.value, e.label);
            if (ImGui::Selectable(lbl, sel))
            {
                filterType_ = static_cast<int>(e.value);
                RefreshList();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "entry or group id", search_, sizeof(search_)))
        RefreshList();

    ImGui::Text("%zu source%s (max %d)", list_.size(), list_.size() == 1 ? "" : "s", kListLimit);

    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    for (const ConditionSourceSummary& s : list_)
    {
        char label[160];
        std::snprintf(label, sizeof(label), "%s  grp %u  entry %d  (%u)##%d_%u_%d",
                      SourceTypeName(s.key.type), s.key.group, s.key.entry, s.count, s.key.type,
                      s.key.group, s.key.entry);
        bool sel = loaded_ && present_ && origKey_ == s.key;
        if (ImGui::Selectable(label, sel))
            LoadSource(s.key);
    }
    ImGui::EndChild();
    ImGui::End();
}

void ConditionsModule::DrawEditorPanel()
{
    if (!ImGui::Begin("Condition Editor"))
    {
        ImGui::End();
        return;
    }
    if (!loaded_)
    {
        ImGui::TextWrapped("Select a source in the browser, or File > New Condition Source.");
        ImGui::End();
        return;
    }
    DrawEditorBody();
    ImGui::End();
}

void ConditionsModule::DrawEditorBody()
{
    // --- source key header ---
    if (BeginFieldTable("##condsource"))
    {
        FieldRow("Source Type", "conditions.SourceTypeOrReferenceId. Pick a CONDITION_SOURCE_TYPE_*; "
                                "a negative value references a conditions_reference set (raw input).");
        if (curKey_.type >= 0)
        {
            uint32_t v = static_cast<uint32_t>(curKey_.type);
            if (EnumCombo("##srctype", v, ConditionSourceTypeValues()))
            {
                curKey_.type = static_cast<int32_t>(v);
                dirty_ = true;
            }
        }
        else if (InputI32("##srctyperaw", curKey_.type))
        {
            dirty_ = true;
        }

        FieldRow("Source Group", "Meaning depends on the source type (see hint below).");
        if (InputU32("##srcgroup", curKey_.group))
            dirty_ = true;

        FieldRow("Source Entry", "Meaning depends on the source type (see hint below).");
        if (InputI32("##srcentry", curKey_.entry))
            dirty_ = true;

        if (const char* meaning = SourceTypeMeaning(curKey_.type))
        {
            FieldRow("  identifies");
            ImGui::TextDisabled("%s", meaning);
        }
        EndFieldTable();
    }

    if (present_ && !(origKey_ == curKey_))
        ImGui::TextDisabled("Saving moves all %zu row(s) from the original source to the new key.",
                            rows_.size());

    ImGui::Separator();

    if (ImGui::Button("Add Condition"))
    {
        DbRecord r;
        for (const char* c : ConditionsRepository::Columns())
            r.cells[c] = "0";
        r.cells["ScriptName"] = "";
        r.cells["Comment"] = "";
        rows_.push_back(std::move(r));
        dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu condition%s (ElseGroups OR'd; rows within a group AND'd)",
                        rows_.size(), rows_.size() == 1 ? "" : "s");
    ImGui::SameLine();
    const bool connected = svc_ && svc_->connected;
    if (ImGui::Button(svc_ && svc_->mode == WriteMode::SqlExport ? "Export" : "Save") && connected)
        Save();

    ImGui::Separator();

    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
    {
        DbRecord& rec = rows_[i];
        ImGui::PushID(i);

        char hdr[48];
        std::snprintf(hdr, sizeof(hdr), "Condition #%d", i + 1);
        ImGui::SeparatorText(hdr);
        if (ImGui::SmallButton("Remove"))
            removeIndex = i;

        if (BeginFieldTable("##cond"))
        {
            dirty_ |= DbI32Field("SourceId", rec, "SourceId",
                                 "Secondary key column; usually 0 (used by a few source types).");
            dirty_ |= DbU32Field("ElseGroup", rec, "ElseGroup",
                                 "OR-group index: same ElseGroup = OR'd; different = AND'd.");

            // ConditionType: enum combo when non-negative, raw int when a reference.
            FieldRow("Condition Type",
                     "conditions.ConditionTypeOrReference. Pick a CONDITION_*; a negative value "
                     "references a conditions_reference set (raw input).");
            int32_t ct = rec.GetI32("ConditionTypeOrReference");
            if (ct >= 0)
            {
                uint32_t v = static_cast<uint32_t>(ct);
                if (EnumCombo("##condtype", v, ConditionTypeValues()))
                {
                    rec.Set("ConditionTypeOrReference", std::to_string(static_cast<int32_t>(v)));
                    dirty_ = true;
                }
            }
            else if (InputI32("##condtyperaw", ct))
            {
                rec.Set("ConditionTypeOrReference", std::to_string(ct));
                dirty_ = true;
            }
            // Live hint: what Value1/2/3 mean for the chosen type.
            for (const EnumEntry& e : ConditionTypeValues())
                if (static_cast<int32_t>(e.value) == ct && e.tooltip)
                {
                    FieldRow("  values mean");
                    ImGui::TextDisabled("%s", e.tooltip);
                    break;
                }

            dirty_ |= DbU32Field("Condition Target", rec, "ConditionTarget",
                                 "Which target the condition tests (usually 0 = player).");
            dirty_ |= DbU32Field("Value 1", rec, "ConditionValue1", "Meaning depends on Condition Type.");
            dirty_ |= DbU32Field("Value 2", rec, "ConditionValue2", "Second parameter (type-dependent).");
            dirty_ |= DbU32Field("Value 3", rec, "ConditionValue3", "Third parameter (type-dependent).");

            FieldRow("Negated", "If set, the condition passes when the test is FALSE.");
            bool neg = rec.GetU32("NegativeCondition") != 0;
            if (ImGui::Checkbox("##neg", &neg))
            {
                rec.Set("NegativeCondition", neg ? "1" : "0");
                dirty_ = true;
            }

            dirty_ |= DbU32Field("Error Type", rec, "ErrorType",
                                 "conditions.ErrorType — client error shown when blocked (0 = default).");
            dirty_ |= DbU32Field("Error Text Id", rec, "ErrorTextId",
                                 "broadcast_text id shown when the condition blocks the action (0 = none).");
            dirty_ |= DbTextField("Script Name", rec, "ScriptName",
                                  "C++ ConditionScript name (rare; usually empty).");
            dirty_ |= DbTextField("Comment", rec, "Comment", "Free-text note stored in conditions.Comment.");

            EndFieldTable();
        }
        ImGui::PopID();
    }

    if (removeIndex >= 0)
    {
        rows_.erase(rows_.begin() + removeIndex);
        dirty_ = true;
    }

    if (rows_.empty())
        ImGui::TextDisabled("No conditions for this source. Add one, or delete the source.");
}

// --- status ----------------------------------------------------------------
std::string ConditionsModule::RecordSummary() const
{
    if (!loaded_)
        return {};
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s grp %u entry %d (%zu)%s", SourceTypeName(curKey_.type),
                  curKey_.group, curKey_.entry, rows_.size(), dirty_ ? " *" : "");
    return buf;
}

// --- headless harness ------------------------------------------------------
void ConditionsModule::SeedSample(bool /*full*/)
{
    curKey_ = ConditionSourceKey{19, 0, 11223};
    origKey_ = curKey_;
    rows_.clear();
    for (int i = 0; i < 2; ++i)
    {
        DbRecord r;
        for (const char* c : ConditionsRepository::Columns())
            r.cells[c] = "0";
        r.cells["SourceTypeOrReferenceId"] = "19";
        r.cells["SourceEntry"] = "11223";
        r.cells["ConditionTypeOrReference"] = i == 0 ? "8" : "9";  // Quest Rewarded / Quest Taken
        r.cells["ConditionValue1"] = i == 0 ? "100" : "101";
        r.cells["Comment"] = i == 0 ? "sample: quest 100 rewarded" : "sample: quest 101 active";
        rows_.push_back(std::move(r));
    }
    loaded_ = true;
    present_ = false;
    dirty_ = false;
}

void ConditionsModule::DrawTabForCapture(int /*tab*/)
{
    if (!loaded_)
        SeedSample(true);
    DrawEditorBody();
}

void ConditionsModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
    DrawEditorBody();
}
} // namespace we
