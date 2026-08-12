#include "adt/AdtWriter.h"
#include "adt/AdtTypes.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void Expect(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void Append(std::vector<std::uint8_t>& bytes, const void* data, std::size_t size)
{
    const auto* raw = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), raw, raw + size);
}

void AppendChunk(std::vector<std::uint8_t>& bytes, const char magic[4], const std::vector<std::uint8_t>& payload)
{
    bytes.insert(bytes.end(), magic, magic + 4);
    const std::uint32_t size = static_cast<std::uint32_t>(payload.size());
    Append(bytes, &size, sizeof(size));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}
} // namespace

int main()
{
    try
    {
        // One valid-enough MCNK with a MCVT payload but no MCCV. The painter must
        // append MCCV, repair the MCNK offset, and tint the vertex at the brush center.
        we::adt::AdtMcnkHeader header{};
        header.position[0] = 0.0f;
        header.position[1] = 0.0f;
        header.position[2] = 0.0f;
        header.ofsHeight = static_cast<std::uint32_t>(8 + sizeof(header));

        std::vector<std::uint8_t> mcnk;
        Append(mcnk, &header, sizeof(header));
        const char mcvtMagic[] = {'T', 'V', 'C', 'M'}; // reversed on-disk MCVT
        std::vector<std::uint8_t> heights(145 * sizeof(float), 0);
        AppendChunk(mcnk, mcvtMagic, heights);

        const char mcnkMagic[] = {'K', 'N', 'C', 'M'}; // reversed on-disk MCNK
        std::vector<std::uint8_t> adt;
        AppendChunk(adt, mcnkMagic, mcnk);

        we::adt::TerrainVertexColorStroke stroke;
        stroke.worldX = 0.0f;
        stroke.worldY = 0.0f;
        stroke.radius = 8.0f;
        stroke.color[0] = 0.25f;
        stroke.color[1] = 0.75f;
        stroke.color[2] = 0.40f;
        stroke.opacity = 1.0f;
        we::adt::TerrainVertexColorResult result;
        Expect(we::adt::PaintTerrainVertexColor(adt, stroke, &result), "vertex paint succeeds");
        Expect(result.touchedVertices > 0 && result.touchedChunks == 1, "vertex paint reports touched MCNK");

        we::adt::AdtMcnkHeader after{};
        std::memcpy(&after, adt.data() + 8, sizeof(after));
        Expect(after.ofsMCCV != 0 && (after.flags & we::adt::kAdtMcnkHasMccv) != 0, "MCCV created and flagged");
        const std::size_t colorChunk = after.ofsMCCV;
        Expect(colorChunk + 8 + 145 * sizeof(std::uint32_t) <= adt.size(), "MCCV payload bounds valid");
        Expect(adt[colorChunk + 0] == 'V' && adt[colorChunk + 1] == 'C' && adt[colorChunk + 2] == 'C' && adt[colorChunk + 3] == 'M',
               "MCCV magic emitted");
        std::uint32_t color = 0;
        std::memcpy(&color, adt.data() + colorChunk + 8, sizeof(color));
        const int r = static_cast<int>((color >> 16u) & 0xFFu);
        const int g = static_cast<int>((color >> 8u) & 0xFFu);
        const int b = static_cast<int>(color & 0xFFu);
        Expect(r < 80 && g > 80 && b > 35, "MCCV channel tint encoded");

        std::cout << "ADT vertex color writer tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "ADT vertex color writer test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
