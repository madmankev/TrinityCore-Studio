#pragma once

// Decodes Blizzard BLP2 textures (WoW 3.3.5a) to RGBA8. Handles the three BLP2
// storage modes: palettized (compression 1, alpha depth 0/1/4/8), DXT (compression
// 2: DXT1/DXT3/DXT5) and uncompressed BGRA (compression 3). Only mip level 0 is
// decoded (enough for icons/maps).

#include <cstdint>
#include <vector>

namespace we
{
struct BlpImage
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // width*height*4, row-major, RGBA8
    bool valid() const { return width > 0 && height > 0 && !rgba.empty(); }
};

// Decode a .blp blob. Returns an invalid image on failure (never throws).
BlpImage DecodeBlp(const std::vector<uint8_t>& bytes);
} // namespace we
