#pragma once
#include <cstdint>
#include <string>
namespace wowedit
{
struct ImportedTexture
{
    std::string sourcePath;
    std::string format;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bitsPerPixel = 0;
    bool compressed = false;
};
class TextureImporter
{
public:
    bool inspect(const std::string& path, ImportedTexture& texture, std::string& error) const;
    bool supports(const std::string& path) const;
};
} // namespace wowedit
