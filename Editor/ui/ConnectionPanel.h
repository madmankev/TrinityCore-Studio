#pragma once

// Layer E (ui) — connection & write-mode panel. Lists saved profiles from a
// ConnectionStore, edits host/port/user/password/worldDb, toggles Live vs SQL
// export (+ export path), and drives Connect/Disconnect/Test via callbacks.
// Owns its own editable form state; the App acts on the values it hands back.

#include <functional>
#include <string>

#include "db/ConnectionStore.h"
#include "db/DbTypes.h"

namespace we
{
struct ConnectionCallbacks
{
    // Connect using the given config, write mode, and (export mode) .sql path.
    std::function<void(const ConnectionConfig&, WriteMode, const std::string&)> onConnect;
    std::function<void()> onDisconnect;
    // Lightweight connectivity check that does not change the active session.
    std::function<void(const ConnectionConfig&)> onTest;
};

class ConnectionPanel
{
public:
    // Renders the connection form contents (no window Begin/End) so it can be
    // embedded inside a modal popup. `connected` gates Connect/Disconnect;
    // `statusLine` and `lastError` are rendered as-is beneath the buttons.
    void DrawBody(ConnectionStore& store, bool connected, const std::string& statusLine,
                  const std::string& lastError, ConnectionCallbacks& cb);

    WriteMode Mode() const { return mode; }
    const std::string& ExportPath() const { return exportPath; }

    // SOAP (in-game .reload after a live save) settings, edited in this dialog.
    bool ReloadAfterSave() const { return reloadAfterSave; }
    const std::string& SoapHost() const { return soapHost; }
    uint16_t SoapPort() const { return soapPort; }
    const std::string& SoapUser() const { return soapUser; }
    const std::string& SoapPassword() const { return soapPassword; }

private:
    void LoadProfileIntoForm(const ConnectionProfile& p);

    int selectedProfile = -1;
    bool formInit = false;

    ConnectionConfig config;
    std::string profileName = "local";
    bool savePassword = false;

    WriteMode mode = WriteMode::Live;
    std::string exportPath = "quest_export.sql";

    bool reloadAfterSave = false;
    std::string soapHost = "127.0.0.1";
    uint16_t soapPort = 7878;
    std::string soapUser;
    std::string soapPassword;
};
} // namespace we
