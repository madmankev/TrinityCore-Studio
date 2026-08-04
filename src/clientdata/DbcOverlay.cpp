// DbcOverlay — see DbcOverlay.h.

#include "clientdata/DbcOverlay.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace we
{
namespace
{
// Normalize an archive path ('\\' or '/') into a relative filesystem path.
fs::path RelativeFromArchive(const std::string& archivePath)
{
    std::string s = archivePath;
    for (char& c : s)
        if (c == '\\')
            c = '/';
    // Strip any leading separators so it stays relative to editRoot.
    size_t start = s.find_first_not_of('/');
    if (start == std::string::npos)
        return {};
    return fs::path(s.substr(start));
}
} // namespace

bool WriteLooseFile(const std::string& editRoot, const std::string& archivePath,
                    const std::vector<uint8_t>& bytes, std::string& err)
{
    err.clear();
    if (editRoot.empty())
    {
        err = "no edit folder configured for this project";
        return false;
    }

    fs::path rel = RelativeFromArchive(archivePath);
    if (rel.empty())
    {
        err = "invalid output path: " + archivePath;
        return false;
    }
    const fs::path dst = fs::path(editRoot) / rel;

    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    if (ec)
    {
        err = "cannot create folder " + dst.parent_path().string() + ": " + ec.message();
        return false;
    }

    const fs::path tmp = dst.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            err = "cannot open for write: " + tmp.string();
            return false;
        }
        if (!bytes.empty())
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        if (!out)
        {
            err = "write failed: " + tmp.string();
            return false;
        }
    }

    fs::rename(tmp, dst, ec);
    if (ec)
    {
        // Rename can fail if the destination exists on some platforms; overwrite.
        fs::remove(dst, ec);
        fs::rename(tmp, dst, ec);
        if (ec)
        {
            err = "cannot finalize " + dst.string() + ": " + ec.message();
            fs::remove(tmp, ec);
            return false;
        }
    }
    return true;
}
} // namespace we
