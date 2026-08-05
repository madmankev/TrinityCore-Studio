// Layer E (ui) — LogPanel. See LogPanel.h.

#include "ui/LogPanel.h"

#include "imgui.h"

#include "util/Log.h"

namespace we
{
void LogPanel::Draw()
{
    if (!ImGui::Begin("Log"))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear"))
        Log::Clear();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu lines", Log::Buffer().size());
    ImGui::Separator();

    if (ImGui::BeginChild("##logscroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar))
    {
        for (const std::string& line : Log::Buffer())
            ImGui::TextUnformatted(line.c_str());

        // Auto-scroll only when already pinned to the bottom.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::End();
}
} // namespace we
