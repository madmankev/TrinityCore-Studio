// Layer E (ui) — Questgivers tab. Edits the creature/gameobject quest-starter and
// quest-ender links plus the quest_greeting rows (creature + gameobject).

#include "editors/quest/Tabs.h"
#include "editors/quest/QuestEditorContext.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/Quest.h"
#include "schema/QuestGreeting.h"
#include "schema/Types.h"
#include "data/LookupCache.h"

#include <cfloat>

namespace we
{
namespace
{
// One editable id-list section. Rows live in a 3-column table that always fits
// the available width: [# fixed | Entry stretch | Remove fixed]. The Entry cell
// hosts an IdNamePicker (its resolved-name text is clipped to the cell, so it
// never runs off the right edge). An Add button below appends a fresh 0 entry.
// Returns true when the vector was modified this frame.
bool DrawIdList(const char* title, const char* tableId, std::vector<uint32_t>& ids,
                LookupCache& cache, RefKind kind)
{
    bool changed = false;

    // Scope every widget in this section (the "Add" button especially) so the four
    // sections don't collide on the same ImGui ID.
    ImGui::PushID(tableId);
    ImGui::SeparatorText(title);

    int removeIndex = -1;
    if (ImGui::BeginTable(tableId, 3,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX |
                              ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed, 360.0f);
        ImGui::TableSetupColumn("##rm", ImGuiTableColumnFlags_WidthFixed, 60.0f);

        for (size_t i = 0; i < ids.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", static_cast<int>(i) + 1);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (IdNamePicker("##e", ids[i], cache, kind))
                changed = true;

            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("x##rm"))
                removeIndex = static_cast<int>(i);

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (removeIndex >= 0)
    {
        ids.erase(ids.begin() + removeIndex);
        changed = true;
    }

    if (ImGui::Button("Add"))
    {
        ids.push_back(0);
        changed = true;
    }

    ImGui::PopID();
    return changed;
}

// Find the greeting of the given type, or nullptr if absent.
QuestGreeting* FindGreeting(Quest& q, uint8_t type)
{
    for (QuestGreeting& g : q.greetings)
        if (g.type == type)
            return &g;
    return nullptr;
}

// Find-or-create the greeting of the given type, seeding id/type on creation.
QuestGreeting& EnsureGreeting(Quest& q, uint8_t type)
{
    if (QuestGreeting* existing = FindGreeting(q, type))
        return *existing;

    QuestGreeting g;
    g.id = q.tmpl.id;
    g.type = type;
    q.greetings.push_back(g);
    return q.greetings.back();
}

// One greeting block (creature or gameobject). Returns true when modified.
bool DrawGreetingBlock(const char* title, Quest& q, uint8_t type)
{
    bool changed = false;

    // Scope by greeting type so the two blocks' "Add greeting" buttons don't collide.
    ImGui::PushID(static_cast<int>(type));
    ImGui::SeparatorText(title);

    QuestGreeting* g = FindGreeting(q, type);
    if (!g)
    {
        if (ImGui::Button("Add greeting"))
        {
            EnsureGreeting(q, type);
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    ImGui::TextUnformatted("Greeting");
    if (InputMultiline("##g", g->greeting))
        changed = true;

    if (BeginFieldTable("qe_greeting_fields"))
    {
        FieldRow("Greet Emote");
        if (EmoteCombo("##ge", g->greetEmoteType))
            changed = true;

        FieldRow("Emote Delay (ms)");
        if (InputU32("##gd", g->greetEmoteDelay))
            changed = true;

        EndFieldTable();
    }

    ImGui::PopID();
    return changed;
}
} // namespace

void DrawQuestgiversTab(QuestEditorContext& ctx)
{
    if (!ctx.quest)
    {
        ImGui::TextDisabled("No quest loaded.");
        return;
    }

    Quest& q = *ctx.quest;
    LookupCache& cache = *ctx.lookups;

    // --- Questgiver id-lists ----------------------------------------------
    bool questgiversChanged = false;
    questgiversChanged |= DrawIdList("Creature Quest Starters", "##creatureStarters",
                                     q.creatureStarters, cache, RefKind::Creature);
    questgiversChanged |= DrawIdList("Creature Quest Enders", "##creatureEnders",
                                     q.creatureEnders, cache, RefKind::Creature);
    questgiversChanged |= DrawIdList("GameObject Quest Starters", "##goStarters",
                                     q.goStarters, cache, RefKind::GameObject);
    questgiversChanged |= DrawIdList("GameObject Quest Enders", "##goEnders",
                                     q.goEnders, cache, RefKind::GameObject);

    if (questgiversChanged)
    {
        q.questgiversDirty = true;
        ctx.MarkChanged();
    }

    ImGui::Spacing();

    // --- Greetings (quest_greeting) ---------------------------------------
    ImGui::SeparatorText("Greetings (quest_greeting)");

    bool greetingsChanged = false;
    greetingsChanged |= DrawGreetingBlock("Creature Greeting",
                                          q, static_cast<uint8_t>(GreetingType::Creature));
    greetingsChanged |= DrawGreetingBlock("GameObject Greeting",
                                          q, static_cast<uint8_t>(GreetingType::GameObject));

    if (greetingsChanged)
    {
        q.greetingsDirty = true;
        ctx.MarkChanged();
    }

    // --- Gossip cross-reference (read-only) -------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Gossip cross-reference");
    ImGui::TextDisabled(
        "For each creature questgiver: its gossip_menu_id from creature_template. 0 = default "
        "gossip (the quest is offered automatically). A non-zero menu means the NPC uses a custom "
        "gossip_menu, which must include a quest option for this quest to appear.");

    auto gossipRows = [&](const char* label, const std::vector<uint32_t>& ids)
    {
        if (ids.empty())
            return;
        ImGui::TextUnformatted(label);
        if (ImGui::BeginTable(label, 2,
                              ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Creature", ImGuiTableColumnFlags_WidthFixed, 360.0f);
            ImGui::TableSetupColumn("gossip_menu_id", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            for (uint32_t id : ids)
            {
                if (id == 0)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cache.LabelCreature(id).c_str());
                ImGui::TableNextColumn();
                const uint32_t gm = cache.GossipMenuOfCreature(id);
                if (gm == 0)
                    ImGui::TextDisabled("0 (default)");
                else
                    ImGui::Text("%u (custom)", gm);
            }
            ImGui::EndTable();
        }
    };
    gossipRows("Starters:", q.creatureStarters);
    gossipRows("Enders:", q.creatureEnders);
    if (q.creatureStarters.empty() && q.creatureEnders.empty())
        ImGui::TextDisabled("No creature questgivers to cross-reference.");
}
} // namespace we
