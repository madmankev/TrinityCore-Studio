// DbcDefRegistry — see DbcDefRegistry.h.

#include "editors/generic/DbcDefRegistry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace we
{
namespace fs = std::filesystem;

namespace
{
// Directory of the running executable (empty if unavailable). Lets a shipped build find the
// bundled definitions next to the .exe regardless of the working directory.
std::string ExeDir()
{
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    return fs::path(std::string(buf, n)).parent_path().string();
#else
    return {};
#endif
}

std::string ReadTextFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

const std::string& DbcDefRegistry::DefinitionsDir()
{
    if (resolved_)
        return defsDir_;
    resolved_ = true;

    std::vector<std::string> candidates;
    candidates.push_back("third_party/wowdbdefs/definitions");  // run from repo root (dev)
    const std::string exe = ExeDir();
    if (!exe.empty())
    {
        candidates.push_back((fs::path(exe) / "definitions").string());
        candidates.push_back(
            (fs::path(exe) / ".." / "third_party" / "wowdbdefs" / "definitions").string());
        candidates.push_back((fs::path(exe) / ".." / ".." / "third_party" / "wowdbdefs" /
                              "definitions").string());
        candidates.push_back((fs::path(exe) / ".." / ".." / ".." / "third_party" / "wowdbdefs" /
                              "definitions").string());
    }
    std::error_code ec;
    for (const std::string& c : candidates)
        if (fs::is_directory(c, ec))
        {
            defsDir_ = c;
            break;
        }
    return defsDir_;
}

const DbcSchema* DbcDefRegistry::Lookup(const std::string& dbcBaseName)
{
    auto cit = curated_.find(dbcBaseName);
    if (cit != curated_.end())
        return cit->second;

    auto dit = dbdCache_.find(dbcBaseName);
    if (dit == dbdCache_.end())
    {
        // Parse the .dbd once; cache the result (null on failure so we don't re-read).
        std::unique_ptr<ParsedDbc> parsed;
        const std::string& dir = DefinitionsDir();
        if (!dir.empty())
        {
            std::string text = ReadTextFile((fs::path(dir) / (dbcBaseName + ".dbd")).string());
            if (!text.empty())
                parsed = ParseDbdFor12340(text);
        }
        dit = dbdCache_.emplace(dbcBaseName, std::move(parsed)).first;
    }
    return dit->second ? &dit->second->schema : nullptr;
}

const char* DbcDefRegistry::Source(const std::string& dbcBaseName)
{
    if (curated_.count(dbcBaseName))
        return "curated";
    auto dit = dbdCache_.find(dbcBaseName);
    if (dit != dbdCache_.end() && dit->second)
        return "dbd";
    return "";
}

std::vector<std::string> DbcDefRegistry::DefinedNames()
{
    std::vector<std::string> names;
    const std::string& dir = DefinitionsDir();
    if (dir.empty())
        return names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".dbd")
            names.push_back(e.path().stem().string());
    std::sort(names.begin(), names.end());
    return names;
}
} // namespace we
