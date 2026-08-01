// Layer E (ui) — General tab: quest_template / quest_template_addon core fields.

#include "editors/quest/Tabs.h"
#include "editors/quest/QuestEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Quest.h"
#include "util/Enums.h"
#include "data/LookupCache.h"

#include <cfloat>
#include <cstdint>

namespace we
{
namespace
{
// Tooltip shown when the preceding item (usually the row label) is hovered.
void HelpTooltip(const char* text)
{
    if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}
} // namespace

void DrawGeneralTab(QuestEditorContext& ctx)
{
    if (!ctx.quest)
    {
        ImGui::TextDisabled("No quest loaded.");
        return;
    }

    Quest&              q     = *ctx.quest;
    QuestTemplate&      t     = q.tmpl;
    QuestTemplateAddon& addon = q.addon;

    auto tmplChanged = [&]()
    {
        ctx.MarkChanged();
        q.tmplDirty = true;
    };
    auto addonChanged = [&]()
    {
        ctx.MarkChanged();
        q.addonDirty  = true;
        addon.present = true;
    };

    // Begin a scalar 2-column table; caller must EndTable() when a matching
    // begin succeeds. Returns whether the table was opened.
    auto beginScalars = [&](const char* id) -> bool
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
            return false;
        ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    };

    // Lay out one row: label (with optional hover tooltip) in col 0, and prime
    // col 1 for a full-width widget. Caller draws the widget right after.
    auto rowLabel = [&](const char* name, const char* tip)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name);
        HelpTooltip(tip);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };

    // -------------------------------------------------------------------- Identity
    ImGui::SeparatorText("Identity");
    if (beginScalars("qe_general_identity"))
    {
        rowLabel("QuestType", "quest_template.QuestType");
        {
            uint32_t v = t.questType;
            if (EnumCombo("##QuestType", v, QuestTypeValues()))
            {
                t.questType = static_cast<uint8_t>(v);
                tmplChanged();
            }
        }

        rowLabel("QuestInfoID", "quest_template.QuestInfoID (QuestInfo.dbc category)");
        {
            uint32_t v = t.questInfoID;
            if (EnumCombo("##QuestInfoID", v, QuestInfoIDValues()))
            {
                t.questInfoID = static_cast<uint16_t>(v);
                tmplChanged();
            }
        }

        rowLabel("QuestSortID", "Positive = zone (AreaTable id); negative = QuestSort category.");
        {
            std::string zone;
            if (ctx.lookups && t.questSortID > 0)
                zone = ctx.lookups->NameOfArea(static_cast<uint32_t>(t.questSortID));
            if (InputI16Named("##QuestSortID", t.questSortID, zone))
                tmplChanged();
        }

        ImGui::EndTable();
    }

    // --------------------------------------------------------------- Level & Timing
    ImGui::SeparatorText("Level & Timing");
    if (beginScalars("qe_general_leveltiming"))
    {
        rowLabel("QuestLevel", "-1 = scales to the player's level.");
        if (InputI16("##QuestLevel", t.questLevel))
            tmplChanged();

        rowLabel("MinLevel", "quest_template.MinLevel");
        if (InputU8("##MinLevel", t.minLevel))
            tmplChanged();

        rowLabel("MaxLevel", "quest_template_addon.MaxLevel (0 = no cap).");
        if (InputU8("##MaxLevel", addon.maxLevel))
            addonChanged();

        rowLabel("SuggestedGroupNum", "Suggested number of players (0 = solo).");
        if (InputU8("##SuggestedGroupNum", t.suggestedGroupNum))
            tmplChanged();

        rowLabel("TimeAllowed", "Seconds to complete once accepted (0 = no timer).");
        if (InputU32("##TimeAllowed", t.timeAllowed))
            tmplChanged();

        ImGui::EndTable();
    }

    // ------------------------------------------------------------------------- Flags
    ImGui::SeparatorText("Flags");
    if (FlagCheckboxGrid("##Flags", t.flags, QuestFlagsBits()))
        tmplChanged();

    if (beginScalars("qe_general_specialflags"))
    {
        rowLabel("SpecialFlags",
                 "TrinityCore custom bitmask (quest_template_addon.SpecialFlags):\n"
                 "bit0 = repeatable, bit1 = explored-triggers-complete,\n"
                 "bit2 = auto-complete-at-accept, ... (raw value).");
        if (InputU8("##SpecialFlags", addon.specialFlags))
            addonChanged();

        ImGui::EndTable();
    }

    // ------------------------------------------------------- Allowable Races/Classes
    ImGui::SeparatorText("Allowable Races/Classes");
    ImGui::TextUnformatted("AllowableRaces (quest_template):");
    if (FlagCheckboxGrid("##AllowableRaces", t.allowableRaces, RaceMaskBits()))
        tmplChanged();

    ImGui::Spacing();
    ImGui::TextUnformatted("AllowableClasses (quest_template_addon):");
    if (FlagCheckboxGrid("##AllowableClasses", addon.allowableClasses, ClassMaskBits()))
        addonChanged();
}
} // namespace we
