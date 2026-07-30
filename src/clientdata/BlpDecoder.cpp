// BlpDecoder — see BlpDecoder.h.

#include "clientdata/BlpDecoder.h"

#include <cstring>

namespace qe
{
namespace
{
uint32_t LE32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Decode a 5:6:5 color into RGB bytes.
void Rgb565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b)
{
    r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
    g = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
    b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
}

// Decode DXT (1/3/5) into RGBA. `mode`: 1=DXT1, 3=DXT3, 5=DXT5.
void DecodeDxt(const uint8_t* src, size_t srcSize, int w, int h, int mode, std::vector<uint8_t>& out)
{
    out.assign(static_cast<size_t>(w) * h * 4, 0);
    const int blocksX = (w + 3) / 4;
    const int blocksY = (h + 3) / 4;
    const int blockBytes = (mode == 1) ? 8 : 16;
    size_t pos = 0;

    for (int by = 0; by < blocksY; ++by)
    {
        for (int bx = 0; bx < blocksX; ++bx)
        {
            if (pos + blockBytes > srcSize)
                return;
            const uint8_t* block = src + pos;
            pos += blockBytes;

            const uint8_t* colorBlock = block + (mode == 1 ? 0 : 8);
            uint16_t c0 = static_cast<uint16_t>(colorBlock[0] | (colorBlock[1] << 8));
            uint16_t c1 = static_cast<uint16_t>(colorBlock[2] | (colorBlock[3] << 8));
            uint8_t r[4], g[4], b[4];
            Rgb565(c0, r[0], g[0], b[0]);
            Rgb565(c1, r[1], g[1], b[1]);
            if (mode == 1 && c0 <= c1)
            {
                r[2] = static_cast<uint8_t>((r[0] + r[1]) / 2);
                g[2] = static_cast<uint8_t>((g[0] + g[1]) / 2);
                b[2] = static_cast<uint8_t>((b[0] + b[1]) / 2);
                r[3] = g[3] = b[3] = 0;  // transparent
            }
            else
            {
                r[2] = static_cast<uint8_t>((2 * r[0] + r[1]) / 3);
                g[2] = static_cast<uint8_t>((2 * g[0] + g[1]) / 3);
                b[2] = static_cast<uint8_t>((2 * b[0] + b[1]) / 3);
                r[3] = static_cast<uint8_t>((r[0] + 2 * r[1]) / 3);
                g[3] = static_cast<uint8_t>((g[0] + 2 * g[1]) / 3);
                b[3] = static_cast<uint8_t>((b[0] + 2 * b[1]) / 3);
            }
            uint32_t idx = LE32(colorBlock + 4);

            // Alpha
            uint8_t alpha[16];
            if (mode == 1)
            {
                for (int i = 0; i < 16; ++i)
                    alpha[i] = 255;  // DXT1: 1-bit alpha handled via c0<=c1 index 3
            }
            else if (mode == 3)
            {
                for (int i = 0; i < 16; ++i)
                {
                    uint8_t a4 = (block[i / 2] >> ((i & 1) * 4)) & 0x0F;
                    alpha[i] = static_cast<uint8_t>(a4 * 255 / 15);
                }
            }
            else  // DXT5
            {
                uint8_t a0 = block[0], a1 = block[1];
                uint8_t at[8];
                at[0] = a0;
                at[1] = a1;
                if (a0 > a1)
                    for (int i = 1; i < 7; ++i)
                        at[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
                else
                {
                    for (int i = 1; i < 5; ++i)
                        at[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
                    at[6] = 0;
                    at[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; ++i)
                    bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
                for (int i = 0; i < 16; ++i)
                    alpha[i] = at[(bits >> (3 * i)) & 0x7];
            }

            for (int py = 0; py < 4; ++py)
            {
                for (int px = 0; px < 4; ++px)
                {
                    int x = bx * 4 + px;
                    int y = by * 4 + py;
                    if (x >= w || y >= h)
                        continue;
                    int pi = py * 4 + px;
                    int sel = (idx >> (2 * pi)) & 0x3;
                    size_t o = (static_cast<size_t>(y) * w + x) * 4;
                    out[o + 0] = r[sel];
                    out[o + 1] = g[sel];
                    out[o + 2] = b[sel];
                    uint8_t a = alpha[pi];
                    if (mode == 1 && c0 <= c1 && sel == 3)
                        a = 0;
                    out[o + 3] = a;
                }
            }
        }
    }
}
} // namespace

BlpImage DecodeBlp(const std::vector<uint8_t>& bytes)
{
    BlpImage img;
    if (bytes.size() < 148 || std::memcmp(bytes.data(), "BLP2", 4) != 0)
        return img;

    const uint8_t compression = bytes[8];
    const uint8_t alphaDepth = bytes[9];
    const uint8_t alphaType = bytes[10];
    const int width = static_cast<int>(LE32(&bytes[12]));
    const int height = static_cast<int>(LE32(&bytes[16]));
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
        return img;

    const uint32_t mip0Off = LE32(&bytes[20]);          // mipOffsets[0]
    const uint32_t mip0Size = LE32(&bytes[20 + 64]);    // mipSizes[0]
    if (mip0Off == 0 || static_cast<size_t>(mip0Off) + mip0Size > bytes.size())
        return img;
    const uint8_t* data = bytes.data() + mip0Off;

    img.width = width;
    img.height = height;
    const size_t px = static_cast<size_t>(width) * height;

    if (compression == 1)
    {
        // Palette is 256 * BGRA right after the 148-byte header.
        if (bytes.size() < 148 + 256 * 4)
            return {};
        const uint8_t* pal = bytes.data() + 148;
        if (mip0Size < px)
            return {};
        img.rgba.assign(px * 4, 255);
        for (size_t i = 0; i < px; ++i)
        {
            uint8_t idx = data[i];
            const uint8_t* pe = pal + idx * 4;  // B,G,R,A
            img.rgba[i * 4 + 0] = pe[2];
            img.rgba[i * 4 + 1] = pe[1];
            img.rgba[i * 4 + 2] = pe[0];
            img.rgba[i * 4 + 3] = 255;
        }
        // Alpha follows the index block.
        const uint8_t* ad = data + px;
        const size_t adAvail = mip0Size > px ? mip0Size - px : 0;
        if (alphaDepth == 8 && adAvail >= px)
        {
            for (size_t i = 0; i < px; ++i)
                img.rgba[i * 4 + 3] = ad[i];
        }
        else if (alphaDepth == 1 && adAvail >= (px + 7) / 8)
        {
            for (size_t i = 0; i < px; ++i)
                img.rgba[i * 4 + 3] = ((ad[i / 8] >> (i & 7)) & 1) ? 255 : 0;
        }
        else if (alphaDepth == 4 && adAvail >= (px + 1) / 2)
        {
            for (size_t i = 0; i < px; ++i)
            {
                uint8_t a = (ad[i / 2] >> ((i & 1) * 4)) & 0x0F;
                img.rgba[i * 4 + 3] = static_cast<uint8_t>(a * 255 / 15);
            }
        }
    }
    else if (compression == 2)
    {
        int mode = 1;                      // DXT1
        if (alphaType == 1)      mode = 3; // DXT3
        else if (alphaType == 7) mode = 5; // DXT5
        else if (alphaType == 8) mode = 5; // some tools use 8 for DXT5
        DecodeDxt(data, mip0Size, width, height, mode, img.rgba);
        if (img.rgba.empty())
            return {};
    }
    else if (compression == 3)
    {
        // Uncompressed BGRA.
        if (mip0Size < px * 4)
            return {};
        img.rgba.assign(px * 4, 255);
        for (size_t i = 0; i < px; ++i)
        {
            img.rgba[i * 4 + 0] = data[i * 4 + 2];
            img.rgba[i * 4 + 1] = data[i * 4 + 1];
            img.rgba[i * 4 + 2] = data[i * 4 + 0];
            img.rgba[i * 4 + 3] = data[i * 4 + 3];
        }
    }
    else
    {
        return {};
    }

    return img;
}
} // namespace qe
