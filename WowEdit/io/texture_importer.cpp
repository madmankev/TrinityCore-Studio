#include "io/texture_importer.h"
#include "io/file_io.h"
#include "utils/string_utils.h"
#include <filesystem>
#include <algorithm>
namespace wowedit
{
namespace { std::string Ext(const std::string& p) { return strings::ToLower(std::filesystem::path(p).extension().string()); }
std::uint32_t Le32(const std::vector<std::uint8_t>& v, std::size_t p) { return std::uint32_t(v[p]) | (std::uint32_t(v[p+1]) << 8u) | (std::uint32_t(v[p+2]) << 16u) | (std::uint32_t(v[p+3]) << 24u); } }
bool TextureImporter::supports(const std::string& path) const { const auto e = Ext(path); return e == ".blp" || e == ".png" || e == ".tga"; }
bool TextureImporter::inspect(const std::string& path, ImportedTexture& texture, std::string& error) const
{
    if (!supports(path)) { error = "Expected .blp, .png, or .tga"; return false; }
    std::vector<std::uint8_t> bytes; if (!FileIo::readBinary(path, bytes, &error)) return false;
    texture = {}; texture.sourcePath = path; texture.format = Ext(path).substr(1);
    if (texture.format == "blp")
    {
        if (bytes.size() < 20 || std::string(reinterpret_cast<const char*>(bytes.data()), 4) != "BLP2") { error = "Unsupported BLP (expected BLP2)"; return false; }
        texture.width = Le32(bytes, 12); texture.height = Le32(bytes, 16); texture.compressed = bytes[8] == 2;
    }
    else if (texture.format == "png")
    {
        static const std::uint8_t signature[] = {137,80,78,71,13,10,26,10};
        if (bytes.size() < 24 || !std::equal(std::begin(signature), std::end(signature), bytes.begin())) { error = "Invalid PNG signature"; return false; }
        texture.width = (std::uint32_t(bytes[16]) << 24u) | (std::uint32_t(bytes[17]) << 16u) | (std::uint32_t(bytes[18]) << 8u) | bytes[19];
        texture.height = (std::uint32_t(bytes[20]) << 24u) | (std::uint32_t(bytes[21]) << 16u) | (std::uint32_t(bytes[22]) << 8u) | bytes[23];
        texture.compressed = true;
    }
    else
    {
        if (bytes.size() < 18) { error = "Invalid TGA header"; return false; }
        texture.width = std::uint32_t(bytes[12]) | (std::uint32_t(bytes[13]) << 8u);
        texture.height = std::uint32_t(bytes[14]) | (std::uint32_t(bytes[15]) << 8u);
        texture.bitsPerPixel = bytes[16]; texture.compressed = bytes[2] == 10 || bytes[2] == 11;
    }
    return texture.width > 0 && texture.height > 0;
}
} // namespace wowedit
