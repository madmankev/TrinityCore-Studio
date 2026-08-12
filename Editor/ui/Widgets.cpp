// Layer E (ui) — reusable ImGui widgets. See Widgets.h.

#include "ui/Widgets.h"

#include <cfloat>
#include <cstdio>
#include <cstring>

#include "gfx/ClientAssets.h"
#include "data/LookupCache.h"
#include "util/StringUtil.h"

namespace we
{
namespace
{
// True if `label` should be drawn as visible text (honors ImGui's "##" hidden-
// label convention: a "##..." label is used only for the widget ID).
bool ShowLabel(const char* label)
{
    return label && label[0] && !(label[0] == '#' && label[1] == '#');
}

// CallbackResize handler letting ImGui::InputText grow a std::string in place.
int InputTextResizeCb(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        std::string* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = const_cast<char*>(str->c_str());
    }
    return 0;
}

std::string PickerLabel(LookupCache& cache, RefKind kind, uint32_t id)
{
    switch (kind)
    {
        case RefKind::Item:       return cache.LabelItem(id);
        case RefKind::Creature:   return cache.LabelCreature(id);
        case RefKind::GameObject: return cache.LabelGameObject(id);
        case RefKind::Faction:    return id ? cache.LabelFaction(id) : std::string();
        case RefKind::Skill:      return id ? cache.LabelSkill(id) : std::string();
        case RefKind::Spell:      return id ? cache.LabelSpell(id) : std::string();
        case RefKind::Title:      return id ? cache.LabelTitle(id) : std::string();
        case RefKind::Area:       return id ? cache.LabelArea(id) : std::string();
        case RefKind::Quest:      return id ? cache.LabelQuest(id) : std::string();
        case RefKind::FactionTemplate: return id ? cache.LabelFactionTemplate(id) : std::string();
        case RefKind::MailTemplate: return id ? cache.LabelMailTemplate(id) : std::string();
    }
    return {};
}

std::vector<NameEntry> PickerSearch(const LookupCache& cache, RefKind kind, const std::string& query)
{
    switch (kind)
    {
        case RefKind::Item:       return cache.SearchItems(query);
        case RefKind::Creature:   return cache.SearchCreatures(query);
        case RefKind::GameObject: return cache.SearchGameObjects(query);
        case RefKind::Faction:    return cache.SearchFactions(query);
        case RefKind::Skill:      return cache.SearchSkills(query);
        case RefKind::Spell:      return cache.SearchSpells(query);
        case RefKind::Title:      return cache.SearchTitles(query);
        case RefKind::Area:       return cache.SearchAreas(query);
        case RefKind::Quest:      return cache.SearchQuests(query);
        case RefKind::FactionTemplate: return cache.SearchFactionTemplates(query);
        case RefKind::MailTemplate: return cache.SearchMailTemplates(query);
    }
    return {};
}
} // namespace

bool InputTextString(const char* label, std::string& v, ImGuiInputTextFlags flags)
{
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, const_cast<char*>(v.c_str()), v.capacity() + 1, flags,
                            InputTextResizeCb, &v);
}

bool InputMultiline(const char* label, std::string& v, float height)
{
    return ImGui::InputTextMultiline(label, const_cast<char*>(v.c_str()), v.capacity() + 1,
                                     ImVec2(0.0f, height), ImGuiInputTextFlags_CallbackResize,
                                     InputTextResizeCb, &v);
}

bool InputU32(const char* label, uint32_t& v) { return ImGui::InputScalar(label, ImGuiDataType_U32, &v); }
bool InputI32(const char* label, int32_t& v) { return ImGui::InputScalar(label, ImGuiDataType_S32, &v); }
bool InputU16(const char* label, uint16_t& v) { return ImGui::InputScalar(label, ImGuiDataType_U16, &v); }
bool InputU8(const char* label, uint8_t& v) { return ImGui::InputScalar(label, ImGuiDataType_U8, &v); }
bool InputI16(const char* label, int16_t& v) { return ImGui::InputScalar(label, ImGuiDataType_S16, &v); }
bool InputFloatField(const char* label, float& v) { return ImGui::InputFloat(label, &v); }

namespace
{
// Draw a fixed-width scalar input followed by a dimmed resolved-name label.
bool ScalarNamed(const char* id, ImGuiDataType type, void* p, const std::string& name)
{
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(130.0f);
    bool changed = ImGui::InputScalar("##v", type, p);
    if (!name.empty())
    {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", name.c_str());
    }
    ImGui::PopID();
    return changed;
}
} // namespace

bool InputU32Named(const char* id, uint32_t& v, const std::string& name) { return ScalarNamed(id, ImGuiDataType_U32, &v, name); }
bool InputU16Named(const char* id, uint16_t& v, const std::string& name) { return ScalarNamed(id, ImGuiDataType_U16, &v, name); }
bool InputU8Named(const char* id, uint8_t& v, const std::string& name) { return ScalarNamed(id, ImGuiDataType_U8, &v, name); }
bool InputI16Named(const char* id, int16_t& v, const std::string& name) { return ScalarNamed(id, ImGuiDataType_S16, &v, name); }
bool InputI32Named(const char* id, int32_t& v, const std::string& name) { return ScalarNamed(id, ImGuiDataType_S32, &v, name); }

bool EnumCombo(const char* label, uint32_t& value, const std::vector<EnumEntry>& entries)
{
    const char* known = LabelFor(entries, value);
    char preview[96];
    if (known)
        std::snprintf(preview, sizeof(preview), "%s (%u)", known, value);
    else
        std::snprintf(preview, sizeof(preview), "%u (custom)", value);

    bool changed = false;
    if (ImGui::BeginCombo(label, preview))
    {
        for (const EnumEntry& e : entries)
        {
            const bool selected = (e.value == value);
            char item[96];
            std::snprintf(item, sizeof(item), "%s (%u)", e.label, e.value);
            if (ImGui::Selectable(item, selected))
            {
                value = e.value;
                changed = true;
            }
            if (e.tooltip && e.tooltip[0] && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", e.tooltip);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool FlagCheckboxGrid(const char* label, uint32_t& bits, const std::vector<FlagEntry>& entries,
                      int columns)
{
    if (columns < 1)
        columns = 1;

    bool changed = false;
    ImGui::PushID(label);
    // Honor ImGui's "##" hidden-label convention (label used only for the ID).
    if (ShowLabel(label))
        ImGui::TextUnformatted(label);

    // Responsive: derive the column count from the available width so labels never
    // run off the right edge. `columns` acts as an upper bound.
    const float avail = ImGui::GetContentRegionAvail().x;
    const float minCol = ImGui::GetFontSize() * 12.0f; // ~ enough for the longest flag label
    int fit = (avail > 0.0f) ? static_cast<int>(avail / minCol) : 1;
    if (fit < 1) fit = 1;
    if (fit > columns) fit = columns;
    columns = fit;

    if (ImGui::BeginTable("##flaggrid", columns, ImGuiTableFlags_SizingStretchSame))
    {
        int idx = 0;
        for (const FlagEntry& e : entries)
        {
            ImGui::TableNextColumn();
            ImGui::PushID(idx++);
            bool on = (bits & e.bit) != 0;
            if (ImGui::Checkbox(e.label, &on))
            {
                if (on)
                    bits |= e.bit;
                else
                    bits &= ~e.bit;
                changed = true;
            }
            if (e.tooltip && e.tooltip[0] && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", e.tooltip);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    return changed;
}

bool MoneyEditor(const char* label, int32_t& copper)
{
    ImGui::PushID(label);
    bool changed = false;

    const int sign = (copper < 0) ? -1 : 1;
    int64_t magnitude = (copper < 0) ? -static_cast<int64_t>(copper) : static_cast<int64_t>(copper);
    int gold = static_cast<int>(magnitude / 10000);
    int silver = static_cast<int>((magnitude / 100) % 100);
    int cop = static_cast<int>(magnitude % 100);

    if (ShowLabel(label))
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
    }

    const float w = 70.0f;
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::InputInt("g##money", &gold, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::InputInt("s##money", &silver, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w);
    changed |= ImGui::InputInt("c##money", &cop, 0);

    if (gold < 0) gold = 0;
    if (silver < 0) silver = 0;
    if (cop < 0) cop = 0;

    if (changed)
        copper = sign * (gold * 10000 + silver * 100 + cop);

    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", MoneyToString(copper).c_str());
    ImGui::PopID();
    return changed;
}

bool EmoteCombo(const char* label, uint16_t& value)
{
    uint32_t v = value;
    if (EnumCombo(label, v, EmoteValues()))
    {
        value = static_cast<uint16_t>(v);
        return true;
    }
    return false;
}

bool IdNamePicker(const char* label, uint32_t& id, LookupCache& cache, RefKind kind)
{
    ImGui::PushID(label);
    bool changed = false;

    if (ShowLabel(label))
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
    }
    // Item/spell icon (from client data, if loaded).
    if (kind == RefKind::Item || kind == RefKind::Spell)
        if (ClientAssets* a = Assets())
            if (a->Ready())
            {
                ImTextureID tex = (kind == RefKind::Item) ? a->ItemIcon(id) : a->SpellIcon(id);
                if (tex)
                {
                    const float h = ImGui::GetFrameHeight();
                    ImGui::Image(tex, ImVec2(h, h));
                    ImGui::SameLine();
                }
            }
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputScalar("##id", ImGuiDataType_U32, &id))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Button("..."))
        ImGui::OpenPopup("##pick");
    ImGui::SameLine();
    // Resolved name, clipped to whatever space remains (never pushes siblings off).
    ImGui::TextDisabled("%s", PickerLabel(cache, kind, id).c_str());

    if (ImGui::BeginPopup("##pick"))
    {
        static std::string query;
        ImGui::SetNextItemWidth(280.0f);
        InputTextString("Search##pickq", query);
        std::vector<NameEntry> results = PickerSearch(cache, kind, query);
        if (ImGui::BeginChild("##pickres", ImVec2(320.0f, 240.0f)))
        {
            for (const NameEntry& r : results)
            {
                char item[256];
                std::snprintf(item, sizeof(item), "%u - %s", r.id, r.name.c_str());
                if (ImGui::Selectable(item))
                {
                    id = r.id;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

bool IdNamePicker(const char* label, uint16_t& id, LookupCache& cache, RefKind kind)
{
    uint32_t tmp = id;
    if (IdNamePicker(label, tmp, cache, kind))
    {
        id = static_cast<uint16_t>(tmp);
        return true;
    }
    return false;
}

bool IdNamePicker(const char* label, uint8_t& id, LookupCache& cache, RefKind kind)
{
    uint32_t tmp = id;
    if (IdNamePicker(label, tmp, cache, kind))
    {
        id = static_cast<uint8_t>(tmp);
        return true;
    }
    return false;
}

bool IdNamePicker(const char* label, int32_t& id, LookupCache& cache, RefKind kind)
{
    // For reward spell etc.: a spell id is non-negative in practice. Edit through the
    // unsigned picker; a pre-existing negative value is treated as 0 for the picker.
    uint32_t tmp = id < 0 ? 0u : static_cast<uint32_t>(id);
    if (IdNamePicker(label, tmp, cache, kind))
    {
        id = static_cast<int32_t>(tmp);
        return true;
    }
    return false;
}

bool IdNamePickerSigned(const char* label, int32_t& value, LookupCache& cache)
{
    ImGui::PushID(label);
    bool changed = false;

    if (ShowLabel(label))
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputScalar("##id", ImGuiDataType_S32, &value))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Button("..."))
        ImGui::OpenPopup("##pickng");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", cache.LabelNpcOrGo(value).c_str());

    if (ImGui::BeginPopup("##pickng"))
    {
        static int mode = 0; // 0 = creature (positive), 1 = gameobject (negative)
        ImGui::RadioButton("Creature", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("GameObject", &mode, 1);

        static std::string query;
        ImGui::SetNextItemWidth(280.0f);
        InputTextString("Search##pickngq", query);
        std::vector<NameEntry> results =
            (mode == 0) ? cache.SearchCreatures(query) : cache.SearchGameObjects(query);
        if (ImGui::BeginChild("##pickngres", ImVec2(320.0f, 240.0f)))
        {
            for (const NameEntry& r : results)
            {
                char item[256];
                std::snprintf(item, sizeof(item), "%u - %s", r.id, r.name.c_str());
                if (ImGui::Selectable(item))
                {
                    value = (mode == 0) ? static_cast<int32_t>(r.id) : -static_cast<int32_t>(r.id);
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

bool BeginFieldTable(const char* id, float labelWidth)
{
    if (!ImGui::BeginTable(id, 2,
                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX |
                               ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
        return false;
    ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void FieldRow(const char* label, const char* tooltip)
{
    ImGui::TableNextRow();
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(255, 255, 255, 7));
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tooltip && *tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void EndFieldTable()
{
    ImGui::EndTable();
}

namespace
{
ImVec4 ButtonColor(StudioButtonTone tone)
{
    switch (tone)
    {
    case StudioButtonTone::Primary:   return ImVec4(0.78f, 0.48f, 0.15f, 1.0f);
    case StudioButtonTone::Quiet:     return ImVec4(0.18f, 0.21f, 0.28f, 0.78f);
    case StudioButtonTone::Danger:    return ImVec4(0.56f, 0.20f, 0.18f, 1.0f);
    default:                          return ImVec4(0.24f, 0.28f, 0.37f, 1.0f);
    }
}

ImVec4 HoverColor(StudioButtonTone tone)
{
    switch (tone)
    {
    case StudioButtonTone::Primary:   return ImVec4(0.92f, 0.63f, 0.22f, 1.0f);
    case StudioButtonTone::Quiet:     return ImVec4(0.26f, 0.31f, 0.42f, 1.0f);
    case StudioButtonTone::Danger:    return ImVec4(0.76f, 0.29f, 0.24f, 1.0f);
    default:                          return ImVec4(0.32f, 0.38f, 0.50f, 1.0f);
    }
}
} // namespace

bool StudioButton(const char* label, StudioButtonTone tone, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ButtonColor(tone));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HoverColor(tone));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, HoverColor(tone));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return pressed;
}

void StudioPill(const char* label, const ImVec4& background, const ImVec4& foreground)
{
    if (!label || !*label)
        return;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 text = ImGui::CalcTextSize(label);
    const ImVec2 padding(8.0f, 3.0f);
    const ImVec2 end(start.x + text.x + padding.x * 2.0f, start.y + text.y + padding.y * 2.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, end, ImGui::GetColorU32(background), 6.0f);
    draw->AddText(ImVec2(start.x + padding.x, start.y + padding.y), ImGui::GetColorU32(foreground), label);
    ImGui::Dummy(ImVec2(end.x - start.x, end.y - start.y));
}

void StudioPanelHeader(const char* eyebrow, const char* title, const char* description,
                       const char* badge, const ImVec4* badgeColor)
{
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    if (width <= 1.0f)
        return;
    const float height = description && *description ? 70.0f : 50.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(start.x + width, start.y + height);
    draw->AddRectFilledMultiColor(start, end,
                                  IM_COL32(38, 48, 70, 230), IM_COL32(27, 33, 49, 230),
                                  IM_COL32(24, 30, 44, 230), IM_COL32(32, 41, 60, 230));
    draw->AddRect(start, end, IM_COL32(118, 145, 196, 88), 8.0f);
    const ImU32 accent = IM_COL32(224, 170, 66, 255);
    draw->AddRectFilled(ImVec2(start.x, start.y), ImVec2(start.x + 4.0f, end.y), accent, 8.0f, ImDrawFlags_RoundCornersLeft);
    if (eyebrow && *eyebrow)
        draw->AddText(ImVec2(start.x + 16.0f, start.y + 10.0f), IM_COL32(150, 171, 213, 235), eyebrow);
    if (title && *title)
        draw->AddText(ImVec2(start.x + 16.0f, start.y + (eyebrow && *eyebrow ? 27.0f : 16.0f)),
                      ImGui::GetColorU32(ImGuiCol_Text), title);
    if (description && *description)
        draw->AddText(ImVec2(start.x + 16.0f, start.y + 49.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), description);
    if (badge && *badge)
    {
        const ImVec2 text = ImGui::CalcTextSize(badge);
        const ImVec2 pad(8.0f, 3.0f);
        const ImVec2 badgeEnd(end.x - 12.0f, start.y + 12.0f);
        const ImVec2 badgeStart(badgeEnd.x - text.x - pad.x * 2.0f, badgeEnd.y);
        draw->AddRectFilled(badgeStart, ImVec2(badgeEnd.x, badgeEnd.y + text.y + pad.y * 2.0f),
                            ImGui::GetColorU32(badgeColor ? *badgeColor : ImVec4(0.26f, 0.33f, 0.48f, 1.0f)), 6.0f);
        draw->AddText(ImVec2(badgeStart.x + pad.x, badgeStart.y + pad.y), IM_COL32(242, 246, 255, 255), badge);
    }
    ImGui::Dummy(ImVec2(width, height + 8.0f));
}

void StudioEmptyState(const char* glyph, const char* title, const char* description)
{
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 132.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(start.x + width, start.y + height);
    draw->AddRectFilled(start, end, IM_COL32(27, 34, 49, 210), 9.0f);
    draw->AddRect(start, end, IM_COL32(108, 128, 169, 80), 9.0f);
    const float iconSize = 40.0f;
    const ImVec2 icon(start.x + 18.0f, start.y + 18.0f);
    draw->AddRectFilled(icon, ImVec2(icon.x + iconSize, icon.y + iconSize), IM_COL32(218, 165, 58, 235), 8.0f);
    if (glyph && *glyph)
    {
        const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
        draw->AddText(ImVec2(icon.x + (iconSize - glyphSize.x) * 0.5f, icon.y + (iconSize - glyphSize.y) * 0.5f),
                      IM_COL32(26, 30, 39, 255), glyph);
    }
    if (title && *title)
        draw->AddText(ImVec2(start.x + 72.0f, start.y + 22.0f), ImGui::GetColorU32(ImGuiCol_Text), title);
    if (description && *description)
    {
        ImGui::PushTextWrapPos(end.x - 18.0f);
        ImGui::SetCursorScreenPos(ImVec2(start.x + 72.0f, start.y + 48.0f));
        ImGui::TextDisabled("%s", description);
        ImGui::PopTextWrapPos();
    }
    ImGui::SetCursorScreenPos(ImVec2(start.x, end.y));
    ImGui::Dummy(ImVec2(width, 4.0f));
}
} // namespace we
