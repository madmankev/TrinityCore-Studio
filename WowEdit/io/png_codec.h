#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wowedit::png
{
struct GrayImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> samples; // normalized 0..1, row-major
};

/** Minimal standards-compliant non-interlaced PNG codec for 8/16-bit grayscale,
 * RGB, gray-alpha, and RGBA heightmaps. zlib is vendored through StormLib. */
bool DecodeGray(const std::vector<std::uint8_t>& bytes, GrayImage& image, std::string* error = nullptr);
bool EncodeGray16(const GrayImage& image, std::vector<std::uint8_t>& bytes, std::string* error = nullptr);
} // namespace wowedit::png
