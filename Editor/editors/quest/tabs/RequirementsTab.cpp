// Layer E (ui) — Requirements tab: accept-gating / prerequisites to take a quest.

#include "editors/quest/Tabs.h"

#include "editors/quest/QuestEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Quest.h"
#include "data/LookupCache.h"

namespace we
{
void DrawRequirementsTab(QuestEditorContext& ctx)
{
    if (!ctx.quest)
    {
        ImGui::TextDisabled("No quest loaded.");
        return;
    }

    Quest& q = *ctx.quest;
    QuestTemplate&      t = q.tmpl;
    QuestTemplateAddon& a = q.addon;

    auto markTmpl = [&]() {
        ctx.MarkChanged();
        q.tmplDirty = true;
    };
    auto markAddon = [&]() {
        ctx.MarkChanged();
        q.addonDirty = true;
        a.present = true;
    };

    LookupCache& lk = *ctx.lookups;

    // --- Reputation to Accept (quest_template) -------------------------------
    ImGui::SeparatorText("Reputation to Accept");
    if (BeginFieldTable("qe_req_repaccept"))
    {
        FieldRow("Required Faction 1", "quest_template.RequiredFactionId1");
        if (IdNamePicker("##reqfac1", t.requiredFactionId1, lk, RefKind::Faction))
            markTmpl();

        FieldRow("Faction 1 Value", "quest_template.RequiredFactionValue1");
        if (InputI32("##reqfacval1", t.requiredFactionValue1))
            markTmpl();

        FieldRow("Required Faction 2", "quest_template.RequiredFactionId2");
        if (IdNamePicker("##reqfac2", t.requiredFactionId2, lk, RefKind::Faction))
            markTmpl();

        FieldRow("Faction 2 Value", "quest_template.RequiredFactionValue2");
        if (InputI32("##reqfacval2", t.requiredFactionValue2))
            markTmpl();

        EndFieldTable();
    }

    // --- Skill Requirement (quest_template_addon) ----------------------------
    ImGui::SeparatorText("Skill Requirement");
    if (BeginFieldTable("qe_req_skill"))
    {
        FieldRow("Skill ID", "quest_template_addon.RequiredSkillID");
        if (IdNamePicker("##reqskillid", a.requiredSkillID, lk, RefKind::Skill))
            markAddon();

        FieldRow("Skill Points", "quest_template_addon.RequiredSkillPoints");
        if (InputU16("##reqskillpts", a.requiredSkillPoints))
            markAddon();

        EndFieldTable();
    }

    // --- Min/Max Reputation Bounds (quest_template_addon) --------------------
    ImGui::SeparatorText("Rep Bounds");
    if (BeginFieldTable("qe_req_repbounds"))
    {
        FieldRow("Min Rep Faction", "quest_template_addon.RequiredMinRepFaction");
        if (IdNamePicker("##minrepfac", a.requiredMinRepFaction, lk, RefKind::Faction))
            markAddon();

        FieldRow("Min Rep Value", "quest_template_addon.RequiredMinRepValue");
        if (InputI32("##minrepval", a.requiredMinRepValue))
            markAddon();

        FieldRow("Max Rep Faction", "quest_template_addon.RequiredMaxRepFaction");
        if (IdNamePicker("##maxrepfac", a.requiredMaxRepFaction, lk, RefKind::Faction))
            markAddon();

        FieldRow("Max Rep Value", "quest_template_addon.RequiredMaxRepValue");
        if (InputI32("##maxrepval", a.requiredMaxRepValue))
            markAddon();

        EndFieldTable();
    }

    // --- Source Spell & Provided Item ----------------------------------------
    ImGui::SeparatorText("Source Spell & Provided Item");
    if (BeginFieldTable("qe_req_source"))
    {
        // SourceSpellID (addon): spell cast on the player when they accept.
        FieldRow("Source Spell", "quest_template_addon.SourceSpellID");
        if (IdNamePicker("##sourcespell", a.sourceSpellID, lk, RefKind::Spell))
            markAddon();

        // StartItem (quest_template): the item given when the quest is accepted.
        FieldRow("Start Item", "quest_template.StartItem");
        if (ctx.lookups)
        {
            if (IdNamePicker("##si", t.startItem, *ctx.lookups, RefKind::Item))
                markTmpl();
        }
        else
        {
            if (InputU32("##si", t.startItem))
                markTmpl();
        }

        // ProvidedItemCount (addon): count of StartItem provided on accept.
        FieldRow("Provided Item Count", "quest_template_addon.ProvidedItemCount");
        if (InputU8("##provideditemcount", a.providedItemCount))
            markAddon();

        EndFieldTable();
    }
}
} // namespace we
