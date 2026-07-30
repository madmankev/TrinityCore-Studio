// Layer E (ui) — Conditions tab: edits the `conditions` rows that gate whether
// this quest can be taken (SourceTypeOrReferenceId = 19, CONDITION_SOURCE_TYPE_
// QUEST_AVAILABLE; SourceEntry = quest id). Other condition source types are not
// edited here.

#include "editors/quest/Tabs.h"
#include "editors/quest/QuestEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"
#include "schema/Quest.h"

#include <cstdio>

namespace qe
{
void DrawConditionsTab(QuestEditorContext& ctx)
{
    if (!ctx.quest)
    {
        ImGui::TextDisabled("No quest loaded.");
        return;
    }
    Quest& q = *ctx.quest;

    auto touch = [&]()
    {
        ctx.MarkChanged();
        q.conditionsDirty = true;
    };

    ImGui::TextWrapped(
        "Quest-availability conditions (conditions.SourceTypeOrReferenceId = 19). All rows here "
        "gate whether this quest can be accepted. SourceType and SourceEntry are managed for you.");
    ImGui::Spacing();

    if (ImGui::Button("Add Condition"))
    {
        QuestCondition c;
        c.sourceTypeOrReferenceId = kConditionSourceQuestAvailable;
        c.sourceEntry = static_cast<int32_t>(q.tmpl.id);
        q.conditions.push_back(c);
        touch();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu condition%s", q.conditions.size(),
                        q.conditions.size() == 1 ? "" : "s");

    ImGui::Separator();

    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(q.conditions.size()); ++i)
    {
        QuestCondition& c = q.conditions[i];
        ImGui::PushID(i);

        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "Condition #%d", i + 1);
        ImGui::SeparatorText(hdr);
        if (ImGui::SmallButton("Remove"))
            removeIndex = i;

        if (BeginFieldTable("qe_cond"))
        {
            FieldRow("ElseGroup", "OR-group index: conditions with the same ElseGroup are OR'd; "
                                  "different ElseGroups are AND'd.");
            if (InputU32("##elsegroup", c.elseGroup))
                touch();

            FieldRow("ConditionType",
                     "conditions.ConditionTypeOrReference. Pick a CONDITION_* type; a negative "
                     "value would reference a conditions_reference set (edit as raw below).");
            if (c.conditionTypeOrReference >= 0)
            {
                uint32_t v = static_cast<uint32_t>(c.conditionTypeOrReference);
                if (EnumCombo("##condtype", v, ConditionTypeValues()))
                {
                    c.conditionTypeOrReference = static_cast<int32_t>(v);
                    touch();
                }
            }
            else
            {
                if (InputI32("##condtyperaw", c.conditionTypeOrReference))
                    touch();
            }
            // Live hint: what Value1/2/3 mean for the selected type.
            if (const char* meaning = LabelFor(ConditionTypeValues(),
                                               static_cast<uint32_t>(c.conditionTypeOrReference >= 0
                                                                         ? c.conditionTypeOrReference
                                                                         : 0)))
            {
                (void)meaning;
                for (const EnumEntry& e : ConditionTypeValues())
                    if (static_cast<int32_t>(e.value) == c.conditionTypeOrReference && e.tooltip)
                    {
                        FieldRow("  values mean");
                        ImGui::TextDisabled("%s", e.tooltip);
                        break;
                    }
            }

            FieldRow("ConditionTarget", "Which target the condition tests (usually 0 = player).");
            if (InputU8("##condtarget", c.conditionTarget))
                touch();

            FieldRow("ConditionValue1", "Meaning depends on ConditionType (e.g. spell/item/faction id).");
            if (InputU32("##cv1", c.conditionValue1))
                touch();
            FieldRow("ConditionValue2", "Second parameter (type-dependent).");
            if (InputU32("##cv2", c.conditionValue2))
                touch();
            FieldRow("ConditionValue3", "Third parameter (type-dependent).");
            if (InputU32("##cv3", c.conditionValue3))
                touch();

            FieldRow("Negated", "If set, the condition passes when the test is FALSE.");
            bool neg = c.negativeCondition != 0;
            if (ImGui::Checkbox("##neg", &neg))
            {
                c.negativeCondition = neg ? 1u : 0u;
                touch();
            }

            FieldRow("ErrorTextId", "Optional broadcast_text id shown when the condition blocks the action (0 = none).");
            if (InputU32("##errtext", c.errorTextId))
                touch();

            FieldRow("Comment", "Free-text note stored in conditions.Comment.");
            if (InputTextString("##comment", c.comment))
                touch();

            EndFieldTable();
        }

        ImGui::PopID();
    }

    if (removeIndex >= 0)
    {
        q.conditions.erase(q.conditions.begin() + removeIndex);
        touch();
    }

    if (q.conditions.empty())
        ImGui::TextDisabled("No conditions. This quest is available to anyone who meets the "
                            "quest_template requirements.");
}
} // namespace qe
