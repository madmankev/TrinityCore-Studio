// Layer E (ui) — ValidationPanel. See ValidationPanel.h.

#include "ui/ValidationPanel.h"

#include <cstdio>

#include "imgui.h"

#include "ui/Widgets.h"

namespace we
{
namespace
{
ImVec4 ColorFor(Severity s)
{
    switch (s)
    {
        case Severity::Error:   return ImVec4(0.94f, 0.35f, 0.35f, 1.0f);
        case Severity::Warning: return ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
        case Severity::Info:    return ImVec4(0.70f, 0.70f, 0.72f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}
} // namespace

void ValidationPanel::Draw(const char* windowTitle,
                           const std::vector<ValidationIssue>& issues, bool hasRecord,
                           const std::function<void(const std::string&)>& onSelectTab,
                           const std::function<void()>& onRevalidate)
{
    if (!ImGui::Begin(windowTitle))
    {
        ImGui::End();
        return;
    }

    StudioPanelHeader("QUALITY", "Validation", "Review issues before committing a world-data change.");
    if (!hasRecord)
    {
        StudioEmptyState("✓", "No record loaded", "Select or create a record to run validation.");
        ImGui::End();
        return;
    }

    if (StudioButton("Re-check", StudioButtonTone::Secondary) && onRevalidate)
        onRevalidate();
    ImGui::SameLine();

    int errors = 0, warnings = 0, infos = 0;
    for (const ValidationIssue& i : issues)
    {
        if (i.severity == Severity::Error) ++errors;
        else if (i.severity == Severity::Warning) ++warnings;
        else ++infos;
    }

    if (issues.empty())
    {
        StudioEmptyState("✓", "Ready to save", "No validation issues were found for the current record.");
    }
    else
    {
        ImGui::TextColored(ColorFor(Severity::Error), "%d errors", errors);
        ImGui::SameLine();
        ImGui::TextColored(ColorFor(Severity::Warning), "%d warnings", warnings);
        ImGui::SameLine();
        ImGui::TextColored(ColorFor(Severity::Info), "%d info", infos);
    }

    ImGui::Separator();

    if (ImGui::BeginChild("##issues"))
    {
        for (int idx = 0; idx < static_cast<int>(issues.size()); ++idx)
        {
            const ValidationIssue& i = issues[idx];
            ImGui::PushID(idx);
            ImGui::PushStyleColor(ImGuiCol_Text, ColorFor(i.severity));
            char line[512];
            std::snprintf(line, sizeof(line), "[%s] %s: %s",
                          i.tab.empty() ? "-" : i.tab.c_str(),
                          i.field.c_str(), i.message.c_str());
            if (ImGui::Selectable(line) && onSelectTab && !i.tab.empty())
                onSelectTab(i.tab);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}
} // namespace we
