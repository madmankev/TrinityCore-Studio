// ClientData — see ClientData.h.

#include "clientdata/ClientData.h"

#include <windows.h>

#include "StormLib.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace we
{
namespace
{
std::string ToBackslashes(const std::string& p)
{
    std::string s = p;
    for (char& c : s)
        if (c == '/')
            c = '\\';
    return s;
}

// SFileOpenArchive takes TCHAR* (wide in this Unicode build); widen the path.
std::wstring WidenW(const std::string& s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
} // namespace

ClientData::~ClientData()
{
    Close();
}

bool ClientData::IsOpen() const
{
    return !archives.empty() || !looseRoot.empty();
}

void ClientData::Close()
{
    for (void* h : archives)
        if (h)
            SFileCloseArchive(h);
    archives.clear();
    looseRoot.clear();
    description.clear();
}

bool ClientData::Open(const std::string& path)
{
    Close();
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec))
    {
        description = "path not found: " + path;
        return false;
    }

    // Loose mode if the folder holds extracted data directly.
    if (fs::exists(fs::path(path) / "DBFilesClient", ec) ||
        fs::exists(fs::path(path) / "Interface", ec))
    {
        looseRoot = path;
    }

    // Build the official WoW 3.3.5a load order (existing files only). Later entries
    // are higher-priority patches; StormLib's patch chain makes reads return exactly
    // what the game sees.
    std::vector<std::string> order;
    auto addIfExists = [&](const fs::path& p)
    {
        std::error_code e;
        if (fs::exists(p, e))
            order.push_back(p.string());
    };
    const fs::path root(path);
    const char* worldMpqs[] = {"common.MPQ",  "common-2.MPQ", "expansion.MPQ", "lichking.MPQ",
                               "patch.MPQ",    "patch-2.MPQ",  "patch-3.MPQ"};
    for (const char* w : worldMpqs)
        addIfExists(root / w);

    // Detect the locale subfolder (the one holding locale-<X>.MPQ) and add its
    // archives in order (base locale first, its patches last).
    std::string locale;
    for (auto& entry : fs::directory_iterator(root, ec))
    {
        if (!entry.is_directory())
            continue;
        std::string name = entry.path().filename().string();
        std::error_code e;
        if (fs::exists(entry.path() / ("locale-" + name + ".MPQ"), e))
        {
            locale = name;
            break;
        }
    }
    if (!locale.empty())
    {
        const fs::path ld = root / locale;
        const char* localePatterns[] = {"locale-%s.MPQ", "expansion-locale-%s.MPQ",
                                        "lichking-locale-%s.MPQ", "patch-%s.MPQ",
                                        "patch-%s-2.MPQ", "patch-%s-3.MPQ"};
        for (const char* pat : localePatterns)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), pat, locale.c_str());
            addIfExists(ld / buf);
        }
    }

    // Open the first archive as the base, then apply the rest as patches in order.
    if (!order.empty())
    {
        HANDLE base = nullptr;
        if (SFileOpenArchive(WidenW(order[0]).c_str(), 0, MPQ_OPEN_READ_ONLY, &base) && base)
        {
            archives.push_back(base);
            for (size_t i = 1; i < order.size(); ++i)
                SFileOpenPatchArchive(base, WidenW(order[i]).c_str(), "", 0);
        }
    }

    if (!IsOpen())
    {
        description = "no MPQ archives or loose data found in: " + path;
        return false;
    }

    if (!archives.empty())
        description = std::to_string(order.size()) + " MPQ(s) (patch chain, locale " +
                      (locale.empty() ? "?" : locale) + ") in " + path;
    else
        description = "loose folder " + path;
    return true;
}

std::vector<uint8_t> ClientData::ReadFile(const std::string& archivePath) const
{
    std::vector<uint8_t> out;

    // Loose disk first.
    if (!looseRoot.empty())
    {
        fs::path p = fs::path(looseRoot) / ToBackslashes(archivePath);
        std::ifstream in(p, std::ios::binary);
        if (in)
        {
            in.seekg(0, std::ios::end);
            std::streamoff sz = in.tellg();
            in.seekg(0, std::ios::beg);
            if (sz > 0)
            {
                out.resize(static_cast<size_t>(sz));
                in.read(reinterpret_cast<char*>(out.data()), sz);
                return out;
            }
        }
    }

    const std::string mpqPath = ToBackslashes(archivePath);
    for (void* h : archives)
    {
        HANDLE hFile = nullptr;
        if (!SFileOpenFileEx(h, mpqPath.c_str(), 0, &hFile) || !hFile)
            continue;
        DWORD high = 0;
        DWORD size = SFileGetFileSize(hFile, &high);
        if (size != SFILE_INVALID_SIZE && size > 0)
        {
            out.resize(size);
            DWORD read = 0;
            if (SFileReadFile(hFile, out.data(), size, &read, nullptr) || read > 0)
            {
                out.resize(read);
                SFileCloseFile(hFile);
                return out;
            }
            out.clear();
        }
        SFileCloseFile(hFile);
    }
    return out;
}

bool ClientData::HasFile(const std::string& archivePath) const
{
    if (!looseRoot.empty())
    {
        std::error_code ec;
        if (fs::exists(fs::path(looseRoot) / ToBackslashes(archivePath), ec))
            return true;
    }
    const std::string mpqPath = ToBackslashes(archivePath);
    for (void* h : archives)
        if (SFileHasFile(h, mpqPath.c_str()))
            return true;
    return false;
}

std::vector<std::string> ClientData::ListFiles(const std::string& extension) const
{
    std::vector<std::string> out;

    // Lowercase the extension for a case-insensitive suffix match.
    std::string ext = extension;
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto endsWith = [&](const char* s) {
        size_t len = std::strlen(s);
        if (len < ext.size())
            return false;
        for (size_t i = 0; i < ext.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(s[len - ext.size() + i])) != ext[i])
                return false;
        return true;
    };

    // Enumerate via StormLib, which resolves names from each archive's internal
    // (listfile) across the whole patch chain (SFileOpenFileEx("(listfile)") only ever
    // sees the base archive, so it misses most models). De-dup patched overrides.
    std::unordered_set<std::string> seen;
    for (void* h : archives)
    {
        SFILE_FIND_DATA fd = {};
        HANDLE find = SFileFindFirstFile(h, "*", &fd, nullptr);
        if (!find)
            continue;
        do
        {
            if (endsWith(fd.cFileName) && seen.insert(fd.cFileName).second)
                out.emplace_back(fd.cFileName);
        } while (SFileFindNextFile(find, &fd));
        SFileFindClose(find);
    }
    return out;
}
} // namespace we
