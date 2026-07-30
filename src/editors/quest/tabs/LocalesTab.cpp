// Layer E (ui) — Locales tab. Edits the per-locale localized strings gathered
// across the four *_locale tables (quest_template_locale, quest_offer_reward_locale,
// quest_request_items_locale, quest_greeting_locale) for one quest.

#include "editors/quest/Tabs.h"
#include "editors/quest/QuestEditorContext.h"
#include "ui/Widgets.h"

#include "schema/Quest.h"
#include "schema/QuestLocale.h"
#include "schema/Types.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace qe
{
void DrawLocalesTab(QuestEditorContext& ctx)
{
    if (!ctx.quest)
    {
        ImGui::TextDisabled("No quest loaded.");
        return;
    }
    Quest& q = *ctx.quest;

    // Selected locale index, clamped each frame in case kLocaleCount ever shifts.
    static int selected = 0;
    if (selected < 0)
        selected = 0;
    if (selected >= kLocaleCount)
        selected = kLocaleCount - 1;

    // Combo preview marks locales that already carry data with a trailing '*'.
    auto hasData = [&q](const char* code) -> bool {
        auto it = q.locales.find(code);
        return it != q.locales.end() &&
               (it->second.templatePresent || it->second.offerRewardPresent ||
                it->second.requestItemsPresent || it->second.greetingCreaturePresent ||
                it->second.greetingGameObjectPresent);
    };

    std::string preview = kLocales[selected];
    if (hasData(kLocales[selected]))
        preview += " *";

    // Locale selector at top, in a labeled field row so the combo fills the panel.
    if (BeginFieldTable("qe_loc_select"))
    {
        FieldRow("Locale", "Which client locale these strings apply to.");
        if (ImGui::BeginCombo("##Locale", preview.c_str()))
        {
            for (int i = 0; i < kLocaleCount; ++i)
            {
                std::string label = kLocales[i];
                if (hasData(kLocales[i]))
                    label += " *";
                const bool isSel = (i == selected);
                if (ImGui::Selectable(label.c_str(), isSel))
                    selected = i;
                if (isSel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        EndFieldTable();
    }

    const char* code = kLocales[selected];

    // Get-or-create the map entry. Creating it here is harmless — presence flags
    // stay false until the user actually edits a field, so empty locales are not
    // written back on save (save keys off the per-source `present` flags).
    QuestLocale& loc = q.locales[code];
    loc.locale = code;

    ImGui::Separator();

    // Label + multiline value on the row below it (multiline widgets fill the panel).
    auto multilineField = [](const char* label, std::string& value) -> bool {
        ImGui::TextUnformatted(label);
        return InputMultiline((std::string("##") + label).c_str(), value);
    };

    // --- quest_template_locale ------------------------------------------------
    if (ImGui::CollapsingHeader("Quest Text", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool changed = false;

        // Single-line strings share one labeled field table.
        if (BeginFieldTable("qe_loc_qt_single"))
        {
            FieldRow("Title");
            changed |= InputTextString("##Title", loc.title);

            FieldRow("EndText");
            changed |= InputTextString("##EndText", loc.endText);

            for (int i = 0; i < 4; ++i)
            {
                char lbl[24];
                std::snprintf(lbl, sizeof(lbl), "ObjectiveText%d", i + 1);
                FieldRow(lbl);

                char id[28];
                std::snprintf(id, sizeof(id), "##ObjectiveText%d", i + 1);
                changed |= InputTextString(id, loc.objectiveText[i]);
            }

            EndFieldTable();
        }

        ImGui::Spacing();

        // Multiline strings: label above, full-width editor below.
        changed |= multilineField("Details", loc.details);
        changed |= multilineField("Objectives", loc.objectives);
        changed |= multilineField("CompletedText", loc.completedText);

        if (changed)
        {
            loc.templatePresent = true;
            q.localesDirty = true;
            ctx.MarkChanged();
        }
    }

    // --- quest_offer_reward_locale --------------------------------------------
    if (ImGui::CollapsingHeader("Offer Reward Text", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool changed = false;
        changed |= multilineField("RewardText", loc.rewardText);
        if (changed)
        {
            loc.offerRewardPresent = true;
            q.localesDirty = true;
            ctx.MarkChanged();
        }
    }

    // --- quest_request_items_locale -------------------------------------------
    if (ImGui::CollapsingHeader("Request Items Text", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool changed = false;
        changed |= multilineField("CompletionText", loc.completionText);
        if (changed)
        {
            loc.requestItemsPresent = true;
            q.localesDirty = true;
            ctx.MarkChanged();
        }
    }

    // --- quest_greeting_locale ------------------------------------------------
    if (ImGui::CollapsingHeader("Greetings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (multilineField("Creature Greeting", loc.greetingCreature))
        {
            loc.greetingCreaturePresent = true;
            q.localesDirty = true;
            ctx.MarkChanged();
        }
        if (multilineField("GameObject Greeting", loc.greetingGameObject))
        {
            loc.greetingGameObjectPresent = true;
            q.localesDirty = true;
            ctx.MarkChanged();
        }
    }
}
} // namespace qe
