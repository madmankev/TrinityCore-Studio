#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wowedit
{
class FileIo
{
public:
    static bool readBinary(const std::string& path, std::vector<std::uint8_t>& bytes, std::string* error = nullptr);
    static bool writeBinaryAtomic(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);
    static bool readText(const std::string& path, std::string& text, std::string* error = nullptr);
    static bool writeTextAtomic(const std::string& path, const std::string& text, std::string* error = nullptr);
    static bool ensureParentDirectory(const std::string& path, std::string* error = nullptr);
};
} // namespace wowedit
