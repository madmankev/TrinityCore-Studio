// TalentModule — see TalentModule.h.

#include "editors/talent/TalentModule.h"

#include "imgui.h"

#include "app/EditorServices.h"
#include "data/LookupCache.h"
#include "editors/common/DbcEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
void TalentModule::Init(EditorServices* services)
{
    SimpleDbcEditorModule::Init(services);
    tabDoc_.Init(&TalentTabSchema(), "DBFilesClient\\TalentTab.dbc");
}

void TalentModule::RebuildTreeNames()
{
    treeNames_.clear();
    for (uint32_t r = 0; r < tabDoc_.RecordCount(); ++r)
        treeNames_[tabDoc_.GetU32(r, talenttab::Id)] = tabDoc_.GetStr(r, talenttab::Name);
}

void TalentModule::OnLoaded()
{
    treeSel_ = -1;
    RebuildTreeNames();
}

std::string TalentModule::TreeName(uint32_t tabId) const
{
    auto it = treeNames_.find(tabId);
    return it != treeNames_.end() ? it->second : std::string();
}

std::string TalentModule::RowLabel(uint32_t row) const
{
    uint32_t id = doc_.GetU32(row, talent::Id);
    uint32_t tab = doc_.GetU32(row, talent::TabID);
    uint32_t rank1 = doc_.GetU32(row, talent::SpellRank);
    std::string spell = (svc_ && svc_->lookups) ? svc_->lookups->NameOfSpell(rank1) : std::string();
    std::string label = std::to_string(id) + ": " + TreeName(tab);
    if (!spell.empty())
        label += " — " + spell;
    return label;
}

void TalentModule::OnRowSeeded(uint32_t row)
{
    doc_.SetU32(row, talent::TabID, 41);  // some tree
}

void TalentModule::DrawTab(int tab, uint32_t row)
{
    if (tab == 1)
        DrawTreesTab();
    else
        DrawGeneralTab(row);
}

void TalentModule::DrawGeneralTab(uint32_t row)
{
    LookupCache& cache = *svc_->lookups;
    if (BeginFieldTable("##talgen"))
    {
        DbcU32Field("ID", doc_, row, talent::Id);

        FieldRow("Talent tree", "TalentTab.dbc id");
        uint32_t tabId = doc_.GetU32(row, talent::TabID);
        if (InputU32Named("##tab", tabId, TreeName(tabId)))
            doc_.SetU32(row, talent::TabID, tabId);

        DbcU32Field("Row (tier)", doc_, row, talent::Row);
        DbcU32Field("Column", doc_, row, talent::Col);
        EndFieldTable();
    }

    ImGui::SeparatorText("Spell ranks (1 = base talent spell)");
    if (BeginFieldTable("##talranks"))
    {
        for (uint32_t i = 0; i < 9; ++i)
        {
            std::string label = "Rank " + std::to_string(i + 1);
            DbcIdNameField(label.c_str(), doc_, row, talent::SpellRank + i, cache, RefKind::Spell);
        }
        EndFieldTable();
    }

    ImGui::SeparatorText("Prerequisites / flags");
    if (BeginFieldTable("##talpre"))
    {
        for (uint32_t i = 0; i < 3; ++i)
        {
            std::string tl = "Prereq talent " + std::to_string(i + 1);
            std::string rk = "  rank " + std::to_string(i + 1);
            DbcU32Field(tl.c_str(), doc_, row, talent::PrereqTalent + i, "Talent.dbc id");
            DbcU32Field(rk.c_str(), doc_, row, talent::PrereqRank + i);
        }
        DbcU32Field("Flags", doc_, row, talent::Flags);
        DbcIdNameField("Required spell", doc_, row, talent::RequiredSpellID, cache, RefKind::Spell);
        DbcU32Field("Category mask 1", doc_, row, talent::CategoryMask, "Allow-for-pet");
        DbcU32Field("Category mask 2", doc_, row, talent::CategoryMask + 1);
        EndFieldTable();
    }
}

void TalentModule::DrawTreesTab()
{
    if (!tabDoc_.IsLoaded())
    {
        ImGui::TextWrapped("TalentTab.dbc not loaded.");
        return;
    }
    ImGui::TextDisabled("The 3 talent trees per class (Arms/Fury/Prot, ...).");

    const char* preview = "(select a tree)";
    std::string previewStr;
    if (treeSel_ >= 0 && static_cast<uint32_t>(treeSel_) < tabDoc_.RecordCount())
    {
        previewStr = std::to_string(tabDoc_.GetU32(static_cast<uint32_t>(treeSel_), talenttab::Id)) +
                     ": " + tabDoc_.GetStr(static_cast<uint32_t>(treeSel_), talenttab::Name);
        preview = previewStr.c_str();
    }
    ImGui::SetNextItemWidth(320);
    if (ImGui::BeginCombo("##treesel", preview))
    {
        for (uint32_t r = 0; r < tabDoc_.RecordCount(); ++r)
        {
            std::string label = std::to_string(tabDoc_.GetU32(r, talenttab::Id)) + ": " +
                                tabDoc_.GetStr(r, talenttab::Name) + "##" + std::to_string(r);
            if (ImGui::Selectable(label.c_str(), treeSel_ == static_cast<int>(r)))
                treeSel_ = static_cast<int>(r);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Add tree"))
    {
        uint32_t r = tabDoc_.AddRow();
        tabDoc_.SetU32(r, talenttab::Id, tabDoc_.NextFreeId(talenttab::Id));
        tabDoc_.SetStr(r, talenttab::Name, "New Tree");
        treeSel_ = static_cast<int>(r);
        RebuildTreeNames();
    }
    if (treeSel_ < 0 || static_cast<uint32_t>(treeSel_) >= tabDoc_.RecordCount())
        return;

    const uint32_t tr = static_cast<uint32_t>(treeSel_);
    if (BeginFieldTable("##treegen"))
    {
        DbcU32Field("ID", tabDoc_, tr, talenttab::Id);
        DbcU32Field("Spell icon", tabDoc_, tr, talenttab::SpellIconID, "SpellIcon.dbc id");
        DbcU32Field("Race mask", tabDoc_, tr, talenttab::RaceMask);
        DbcU32Field("Class mask", tabDoc_, tr, talenttab::ClassMask, "1 warrior 2 paladin 4 hunter 8 rogue 16 priest ...");
        DbcU32Field("Pet talent mask", tabDoc_, tr, talenttab::PetTalentMask);
        DbcU32Field("Order index", tabDoc_, tr, talenttab::OrderIndex, "0/1/2 = which of the 3 trees");
        DbcStrField("Background file", tabDoc_, tr, talenttab::BackgroundFile);
        EndFieldTable();
    }
    DbcLangEditor("Name", tabDoc_, tr, talenttab::Name);
    RebuildTreeNames();  // cheap (33 rows) — keep TabID resolution current
}

void TalentModule::SeedSampleExtra()
{
    if (tabDoc_.RecordCount() == 0)
    {
        tabDoc_.InitEmpty();
        uint32_t r = tabDoc_.AddRow();
        tabDoc_.SetU32(r, talenttab::Id, 41);
        tabDoc_.SetStr(r, talenttab::Name, "Fire");
        treeSel_ = 0;
        RebuildTreeNames();
    }
}
} // namespace we
