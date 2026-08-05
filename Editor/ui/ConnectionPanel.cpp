// Layer E (ui) — ConnectionPanel. See ConnectionPanel.h.

#include "ui/ConnectionPanel.h"

#include <cstdint>

#include "imgui.h"

#include "ui/Widgets.h"
#include "util/Log.h"

namespace we
{
void ConnectionPanel::LoadProfileIntoForm(const ConnectionProfile& p)
{
    profileName = p.name;
    config = p.config;
    savePassword = p.savePassword;
}

void ConnectionPanel::DrawBody(ConnectionStore& store, bool connected, const std::string& statusLine,
                               const std::string& lastError, ConnectionCallbacks& cb)
{
    std::vector<ConnectionProfile>& profiles = store.Profiles();

    // Seed the form from the first profile once, if any exist.
    if (!formInit)
    {
        if (!profiles.empty())
        {
            selectedProfile = 0;
            LoadProfileIntoForm(profiles[0]);
        }
        formInit = true;
    }

    // --- Profile selector ----------------------------------------------------
    const char* preview = (selectedProfile >= 0 && selectedProfile < (int)profiles.size())
                              ? profiles[selectedProfile].name.c_str()
                              : "(none)";
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Profile", preview))
    {
        for (int i = 0; i < (int)profiles.size(); ++i)
        {
            const bool selected = (i == selectedProfile);
            if (ImGui::Selectable(profiles[i].name.c_str(), selected))
            {
                selectedProfile = i;
                LoadProfileIntoForm(profiles[i]);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    // --- Editable connection fields -----------------------------------------
    ImGui::SetNextItemWidth(260.0f);
    InputTextString("Profile name", profileName);

    ImGui::SetNextItemWidth(260.0f);
    InputTextString("Host", config.host);

    int port = config.port;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Port", &port, 0))
    {
        if (port < 0) port = 0;
        if (port > 65535) port = 65535;
        config.port = static_cast<uint16_t>(port);
    }

    ImGui::SetNextItemWidth(260.0f);
    InputTextString("User", config.user);

    ImGui::SetNextItemWidth(260.0f);
    InputTextString("Password", config.password, ImGuiInputTextFlags_Password);

    ImGui::SetNextItemWidth(260.0f);
    InputTextString("World DB", config.worldDb);

    ImGui::Checkbox("Save password in profile", &savePassword);

    // --- Profile persistence -------------------------------------------------
    if (ImGui::Button("Save Profile"))
    {
        ConnectionProfile p;
        p.name = profileName;
        p.config = config;
        p.savePassword = savePassword;

        int existing = -1;
        for (int i = 0; i < (int)profiles.size(); ++i)
            if (profiles[i].name == profileName)
            {
                existing = i;
                break;
            }
        if (existing >= 0)
        {
            profiles[existing] = p;
            selectedProfile = existing;
        }
        else
        {
            profiles.push_back(p);
            selectedProfile = (int)profiles.size() - 1;
        }
        DbError e = store.Save();
        if (e.ok)
            LogInfo("Saved connection profile '" + profileName + "'");
        else
            LogError("Save profile failed: " + e.message);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedProfile < 0 || selectedProfile >= (int)profiles.size());
    if (ImGui::Button("Delete Profile"))
    {
        LogInfo("Deleted connection profile '" + profiles[selectedProfile].name + "'");
        profiles.erase(profiles.begin() + selectedProfile);
        selectedProfile = profiles.empty() ? -1 : 0;
        if (selectedProfile >= 0)
            LoadProfileIntoForm(profiles[selectedProfile]);
        DbError e = store.Save();
        if (!e.ok)
            LogError("Save profiles failed: " + e.message);
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    // --- Write mode ----------------------------------------------------------
    int modeInt = (mode == WriteMode::Live) ? 0 : 1;
    ImGui::TextUnformatted("Write mode:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Live", &modeInt, 0))
        mode = WriteMode::Live;
    ImGui::SameLine();
    if (ImGui::RadioButton("SQL Export", &modeInt, 1))
        mode = WriteMode::SqlExport;

    if (mode == WriteMode::SqlExport)
    {
        ImGui::SetNextItemWidth(360.0f);
        InputTextString("Output .sql", exportPath);
    }

    ImGui::Separator();

    // --- In-game reload (SOAP) ----------------------------------------------
    if (ImGui::CollapsingHeader("In-game reload (SOAP)"))
    {
        ImGui::Checkbox("Reload quest tables on the server after a live save", &reloadAfterSave);
        ImGui::TextDisabled("Requires worldserver SOAP enabled (worldserver.conf) and a GM account.");
        ImGui::SetNextItemWidth(260.0f);
        InputTextString("SOAP host", soapHost);
        int sp = soapPort;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("SOAP port", &sp, 0))
        {
            if (sp < 0) sp = 0;
            if (sp > 65535) sp = 65535;
            soapPort = static_cast<uint16_t>(sp);
        }
        ImGui::SetNextItemWidth(260.0f);
        InputTextString("SOAP account", soapUser);
        ImGui::SetNextItemWidth(260.0f);
        InputTextString("SOAP password", soapPassword, ImGuiInputTextFlags_Password);
    }

    ImGui::Separator();

    // --- Connect / Disconnect / Test ----------------------------------------
    ImGui::BeginDisabled(connected);
    if (ImGui::Button("Connect"))
    {
        if (cb.onConnect)
            cb.onConnect(config, mode, exportPath);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!connected);
    if (ImGui::Button("Disconnect"))
    {
        if (cb.onDisconnect)
            cb.onDisconnect();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Test"))
    {
        if (cb.onTest)
            cb.onTest(config);
    }

    // --- Status --------------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("Status: %s", connected ? "connected" : "disconnected");
    if (!statusLine.empty())
        ImGui::TextWrapped("%s", statusLine.c_str());
    if (!lastError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastError.c_str());
}
} // namespace we
