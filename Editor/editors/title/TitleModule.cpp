// TitleModule — see TitleModule.h.

#include "editors/title/TitleModule.h"

#include "imgui.h"

#include "editors/common/DbcEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
std::string TitleModule::RowLabel(uint32_t row) const
{
    return std::to_string(doc_.GetU32(row, title::Id)) + ": " +
           doc_.GetStr(row, title::NameMaleLoc0);
}

void TitleModule::OnRowSeeded(uint32_t row)
{
    // A new title needs a unique known-titles bit; default the name to a "%s" template.
    doc_.SetU32(row, title::BitIndex, doc_.NextFreeId(title::BitIndex));
    doc_.SetStr(row, title::NameMaleLoc0, "New Title %s");
    doc_.SetStr(row, title::NameFemaleLoc0, "New Title %s");
}

void TitleModule::DrawTab(int tab, uint32_t row)
{
    if (tab == 1)
    {
        DbcLangEditor("Name (male)", doc_, row, title::NameMaleLoc0);
        DbcLangEditor("Name (female)", doc_, row, title::NameFemaleLoc0);
        return;
    }

    if (BeginFieldTable("##titlegeneral"))
    {
        DbcU32Field("ID", doc_, row, title::Id);
        DbcU32Field("Bit index", doc_, row, title::BitIndex,
                    "Bit in the player's known-titles mask (must be unique)");
        DbcU32Field("Unk1", doc_, row, title::Unk1, "Unused/condition in TrinityCore (preserved)");
        DbcStrField("Name male (enUS)", doc_, row, title::NameMaleLoc0, "%s = player name");
        DbcStrField("Name female (enUS)", doc_, row, title::NameFemaleLoc0, "%s = player name");
        EndFieldTable();
    }
    ImGui::TextDisabled("Titles are granted via achievement_reward / quest RewardTitle.");
}
} // namespace we
