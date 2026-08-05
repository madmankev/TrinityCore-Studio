#pragma once

#include <cstdint>
#include <string>

namespace we {

// Configuration for connecting to a TrinityCore worldserver SOAP endpoint.
struct SoapConfig {
    std::string host = "127.0.0.1";
    uint16_t    port = 7878;
    std::string user;      // GM account name
    std::string password;  // GM account password
};

// Result of a SOAP command execution.
struct SoapResult {
    bool        ok = false;
    std::string output;  // server's <result> text on success (or raw body)
    std::string error;   // error / <faultstring> on failure
};

// Minimal blocking SOAP client for TrinityCore's worldserver command interface.
// Implemented on top of WinHTTP. Never throws.
class SoapClient {
public:
    // Blocking. Sends `command` (e.g. ".reload quest_template") to the worldserver
    // described by `cfg` and returns the server's <result> text on success, or
    // ok=false with `error` set on failure. Never throws.
    SoapResult ExecuteCommand(const SoapConfig& cfg, const std::string& command) const;
};

} // namespace we
