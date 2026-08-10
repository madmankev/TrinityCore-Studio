#include "io/file_io.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace wowedit
{
namespace
{
void SetError(std::string* error, const std::string& value)
{
    if (error) *error = value;
}

std::filesystem::path TemporaryPath(const std::filesystem::path& target)
{
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return target.string() + ".tmp." + std::to_string(stamp);
}
} // namespace

bool FileIo::ensureParentDirectory(const std::string& path, std::string* error)
{
    std::error_code code;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty() && !std::filesystem::create_directories(parent, code) && code)
    {
        SetError(error, code.message());
        return false;
    }
    return true;
}

bool FileIo::readBinary(const std::string& path, std::vector<std::uint8_t>& bytes, std::string* error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) { SetError(error, "Could not open " + path); return false; }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) { SetError(error, "Could not measure " + path); return false; }
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    if (size > 0) input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input && size > 0) { SetError(error, "Could not read " + path); return false; }
    return true;
}

bool FileIo::writeBinaryAtomic(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string* error)
{
    if (!ensureParentDirectory(path, error)) return false;
    const std::filesystem::path target(path);
    const std::filesystem::path temporary = TemporaryPath(target);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { SetError(error, "Could not write " + temporary.string()); return false; }
        if (!bytes.empty()) output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) { SetError(error, "Could not finish writing " + temporary.string()); return false; }
    }
    std::error_code code;
    std::filesystem::rename(temporary, target, code);
    if (code)
    {
        // Windows does not replace an existing path on rename; remove only after a
        // complete temp file exists, retaining the previous file on write failure.
        std::filesystem::remove(target, code);
        code.clear();
        std::filesystem::rename(temporary, target, code);
    }
    if (code)
    {
        std::filesystem::remove(temporary);
        SetError(error, code.message());
        return false;
    }
    return true;
}

bool FileIo::readText(const std::string& path, std::string& text, std::string* error)
{
    std::vector<std::uint8_t> bytes;
    if (!readBinary(path, bytes, error)) return false;
    text.assign(bytes.begin(), bytes.end());
    return true;
}

bool FileIo::writeTextAtomic(const std::string& path, const std::string& text, std::string* error)
{
    return writeBinaryAtomic(path, std::vector<std::uint8_t>(text.begin(), text.end()), error);
}
} // namespace wowedit
