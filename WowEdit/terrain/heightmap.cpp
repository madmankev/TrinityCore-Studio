#include "terrain/heightmap.h"

#include "utils/math_utils.h"
#include "io/png_codec.h"
#include "utils/string_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace wowedit
{
namespace
{
bool ReadAllBytes(const std::string& path, std::vector<std::uint8_t>& bytes)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0)
        return false;
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    return static_cast<bool>(input);
}

bool WriteAllBytes(const std::string& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

std::string Extension(const std::string& path)
{
    const std::size_t dot = path.find_last_of('.');
    return dot == std::string::npos ? std::string{} : strings::ToLower(path.substr(dot));
}

bool IsSquare(std::size_t samples, std::uint32_t& side)
{
    const std::uint32_t candidate = static_cast<std::uint32_t>(std::sqrt(static_cast<double>(samples)) + 0.5);
    if (candidate == 0 || static_cast<std::size_t>(candidate) * candidate != samples)
        return false;
    side = candidate;
    return true;
}

bool NextPgmToken(const std::vector<std::uint8_t>& bytes, std::size_t& cursor, std::string& token)
{
    while (cursor < bytes.size())
    {
        if (bytes[cursor] == '#')
            while (cursor < bytes.size() && bytes[cursor++] != '\n') {}
        else if (std::isspace(bytes[cursor]) != 0)
            ++cursor;
        else
            break;
    }
    const std::size_t begin = cursor;
    while (cursor < bytes.size() && std::isspace(bytes[cursor]) == 0)
        ++cursor;
    if (begin == cursor)
        return false;
    token.assign(reinterpret_cast<const char*>(bytes.data() + begin), cursor - begin);
    return true;
}
} // namespace

Heightmap::Heightmap(std::uint32_t widthValue, std::uint32_t heightValue, float scaleValue, float initialHeight)
{
    resize(widthValue, heightValue, scaleValue, initialHeight);
}

void Heightmap::resize(std::uint32_t widthValue, std::uint32_t heightValue, float scaleValue, float initialHeight)
{
    width = widthValue;
    height = heightValue;
    scale = std::max(0.0001f, scaleValue);
    heightData.assign(static_cast<std::size_t>(width) * height, initialHeight);
    minHeight = maxHeight = flattenTarget_ = initialHeight;
    rangeDirty_ = false;
}

void Heightmap::setScale(float worldUnitsPerPixel)
{
    scale = std::max(0.0001f, worldUnitsPerPixel);
}

std::size_t Heightmap::index(std::uint32_t x, std::uint32_t y) const
{
    return static_cast<std::size_t>(y) * width + x;
}

bool Heightmap::inBounds(std::uint32_t x, std::uint32_t y) const
{
    return x < width && y < height;
}

float Heightmap::getHeight(std::uint32_t x, std::uint32_t y) const
{
    return inBounds(x, y) ? heightData[index(x, y)] : 0.0f;
}

void Heightmap::setHeight(std::uint32_t x, std::uint32_t y, float heightValue)
{
    if (!inBounds(x, y))
        return;
    heightData[index(x, y)] = heightValue;
    rangeDirty_ = true;
}

float Heightmap::getInterpolatedHeight(float worldX, float worldZ) const
{
    if (width == 0 || height == 0)
        return 0.0f;

    const float sampleX = worldX / scale;
    const float sampleY = worldZ / scale;
    const float clampedX = glm::clamp(sampleX, 0.0f, static_cast<float>(width - 1));
    const float clampedY = glm::clamp(sampleY, 0.0f, static_cast<float>(height - 1));
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(clampedX));
    const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(clampedY));
    const std::uint32_t x1 = std::min(x0 + 1, width - 1);
    const std::uint32_t y1 = std::min(y0 + 1, height - 1);
    const float tx = clampedX - static_cast<float>(x0);
    const float ty = clampedY - static_cast<float>(y0);
    return glm::mix(glm::mix(getHeight(x0, y0), getHeight(x1, y0), tx),
                    glm::mix(getHeight(x0, y1), getHeight(x1, y1), tx), ty);
}

glm::vec3 Heightmap::calculateNormal(std::uint32_t x, std::uint32_t y) const
{
    if (!inBounds(x, y))
        return {0.0f, 1.0f, 0.0f};
    const std::uint32_t left = x == 0 ? x : x - 1;
    const std::uint32_t right = std::min(x + 1, width - 1);
    const std::uint32_t down = y == 0 ? y : y - 1;
    const std::uint32_t up = std::min(y + 1, height - 1);
    const float dx = getHeight(right, y) - getHeight(left, y);
    const float dz = getHeight(x, up) - getHeight(x, down);
    return glm::normalize(glm::vec3(-dx, 2.0f * scale, -dz));
}

void Heightmap::modifyHeight(std::uint32_t x, std::uint32_t y, float delta)
{
    if (inBounds(x, y))
        setHeight(x, y, getHeight(x, y) + delta);
}

void Heightmap::applyBrush(glm::vec3 worldPos, float radius, float strength, float falloff, BrushOperation op)
{
    // Legacy/public overload: falloff is a positive exponent (1 = linear). The
    // explicit overload below exposes named curves for the UI and tablet tools.
    if (radius <= 0.0f || width == 0 || height == 0)
        return;

    const std::uint32_t minX = static_cast<std::uint32_t>(std::max(0.0f, std::floor((worldPos.x - radius) / scale)));
    const std::uint32_t maxX = static_cast<std::uint32_t>(std::min(static_cast<float>(width - 1), std::ceil((worldPos.x + radius) / scale)));
    const std::uint32_t minY = static_cast<std::uint32_t>(std::max(0.0f, std::floor((worldPos.z - radius) / scale)));
    const std::uint32_t maxY = static_cast<std::uint32_t>(std::min(static_cast<float>(height - 1), std::ceil((worldPos.z + radius) / scale)));
    const std::vector<float> original = heightData;
    const float exponent = std::max(0.01f, falloff);

    for (std::uint32_t y = minY; y <= maxY; ++y)
    {
        for (std::uint32_t x = minX; x <= maxX; ++x)
        {
            const glm::vec2 delta(static_cast<float>(x) * scale - worldPos.x,
                                  static_cast<float>(y) * scale - worldPos.z);
            const float distance = glm::length(delta);
            if (distance > radius)
                continue;
            const float weight = std::pow(1.0f - distance / radius, exponent);
            const std::size_t i = index(x, y);
            switch (op)
            {
            case BrushOperation::Raise: heightData[i] = original[i] + strength * weight; break;
            case BrushOperation::Lower: heightData[i] = original[i] - strength * weight; break;
            case BrushOperation::Flatten: heightData[i] = glm::mix(original[i], flattenTarget_, Saturate(strength * weight)); break;
            case BrushOperation::Noise:
            {
                float noise = 0.0f;
                float amplitude = noiseAmplitude_;
                float frequency = noiseFrequency_;
                for (std::uint32_t octave = 0; octave < std::max(1u, noiseOctaves_); ++octave)
                {
                    noise += math::PerlinLikeNoise(static_cast<float>(x) * frequency, static_cast<float>(y) * frequency,
                                                   noiseSeed_ + octave * 101u) * amplitude;
                    amplitude *= 0.5f;
                    frequency *= 2.0f;
                }
                heightData[i] = original[i] + noise * strength * weight;
                break;
            }
            case BrushOperation::Smooth:
            {
                float total = 0.0f;
                int count = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox)
                    {
                        const int sx = static_cast<int>(x) + ox;
                        const int sy = static_cast<int>(y) + oy;
                        if (sx >= 0 && sy >= 0 && sx < static_cast<int>(width) && sy < static_cast<int>(height))
                        {
                            total += original[index(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy))];
                            ++count;
                        }
                    }
                const float average = count == 0 ? original[i] : total / static_cast<float>(count);
                heightData[i] = glm::mix(original[i], average, Saturate(strength * weight));
                break;
            }
            }
        }
    }
    rangeDirty_ = true;
}

void Heightmap::applyBrush(glm::vec3 worldPos, float radius, float strength, BrushFalloffType falloff,
                           BrushOperation op, float pressure)
{
    if (radius <= 0.0f || width == 0 || height == 0)
        return;

    const std::uint32_t minX = static_cast<std::uint32_t>(std::max(0.0f, std::floor((worldPos.x - radius) / scale)));
    const std::uint32_t maxX = static_cast<std::uint32_t>(std::min(static_cast<float>(width - 1), std::ceil((worldPos.x + radius) / scale)));
    const std::uint32_t minY = static_cast<std::uint32_t>(std::max(0.0f, std::floor((worldPos.z - radius) / scale)));
    const std::uint32_t maxY = static_cast<std::uint32_t>(std::min(static_cast<float>(height - 1), std::ceil((worldPos.z + radius) / scale)));
    const std::vector<float> original = heightData;
    const float effectiveStrength = strength * Saturate(pressure);

    for (std::uint32_t y = minY; y <= maxY; ++y)
    {
        for (std::uint32_t x = minX; x <= maxX; ++x)
        {
            const float dx = static_cast<float>(x) * scale - worldPos.x;
            const float dz = static_cast<float>(y) * scale - worldPos.z;
            const float normalizedDistance = std::sqrt(dx * dx + dz * dz) / radius;
            if (normalizedDistance > 1.0f)
                continue;
            const float weight = brushWeight(normalizedDistance, falloff);
            const std::size_t i = index(x, y);
            if (op == BrushOperation::Raise)
                heightData[i] = original[i] + effectiveStrength * weight;
            else if (op == BrushOperation::Lower)
                heightData[i] = original[i] - effectiveStrength * weight;
            else if (op == BrushOperation::Flatten)
                heightData[i] = glm::mix(original[i], flattenTarget_, Saturate(effectiveStrength * weight));
            else if (op == BrushOperation::Smooth)
            {
                float total = 0.0f;
                int count = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox)
                    {
                        const int sx = static_cast<int>(x) + ox;
                        const int sy = static_cast<int>(y) + oy;
                        if (sx >= 0 && sy >= 0 && sx < static_cast<int>(width) && sy < static_cast<int>(height))
                        {
                            total += original[index(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy))];
                            ++count;
                        }
                    }
                heightData[i] = glm::mix(original[i], total / static_cast<float>(std::max(count, 1)),
                                          Saturate(effectiveStrength * weight));
            }
            else
            {
                float noise = 0.0f;
                float amplitude = noiseAmplitude_;
                float frequency = noiseFrequency_;
                for (std::uint32_t octave = 0; octave < std::max(1u, noiseOctaves_); ++octave)
                {
                    noise += math::PerlinLikeNoise(static_cast<float>(x) * frequency, static_cast<float>(y) * frequency,
                                                   noiseSeed_ + octave * 101u) * amplitude;
                    amplitude *= 0.5f;
                    frequency *= 2.0f;
                }
                heightData[i] = original[i] + noise * effectiveStrength * weight;
            }
        }
    }
    rangeDirty_ = true;
}

void Heightmap::setNoiseSettings(float frequency, float amplitude, std::uint32_t octaves, std::uint32_t seed)
{
    noiseFrequency_ = std::max(0.0001f, frequency);
    noiseAmplitude_ = std::max(0.0f, amplitude);
    noiseOctaves_ = std::max(1u, octaves);
    noiseSeed_ = seed;
}

float Heightmap::getMinHeight() const
{
    recalculateRange();
    return minHeight;
}

float Heightmap::getMaxHeight() const
{
    recalculateRange();
    return maxHeight;
}

std::vector<float>& Heightmap::mutableData()
{
    rangeDirty_ = true;
    return heightData;
}

void Heightmap::recalculateRange() const
{
    if (!rangeDirty_)
        return;
    if (heightData.empty())
    {
        minHeight = maxHeight = 0.0f;
    }
    else
    {
        const auto range = std::minmax_element(heightData.begin(), heightData.end());
        minHeight = *range.first;
        maxHeight = *range.second;
    }
    rangeDirty_ = false;
}

float Heightmap::brushWeight(float normalizedDistance, BrushFalloffType falloff) const
{
    const float distance = Saturate(normalizedDistance);
    switch (falloff)
    {
    case BrushFalloffType::Linear: return 1.0f - distance;
    case BrushFalloffType::Smoothstep: return 1.0f - math::SmoothStep(0.0f, 1.0f, distance);
    case BrushFalloffType::Gaussian: return math::Gaussian(distance) * (1.0f - distance);
    case BrushFalloffType::Constant: return 1.0f;
    }
    return 0.0f;
}

bool Heightmap::importFromImage(const std::string& imagePath)
{
    std::vector<std::uint8_t> bytes;
    if (!ReadAllBytes(imagePath, bytes))
        return false;
    const std::string extension = Extension(imagePath);

    if (extension == ".png")
    {
        png::GrayImage image;
        if (!png::DecodeGray(bytes, image))
            return false;
        resize(image.width, image.height, scale, 0.0f);
        heightData = std::move(image.samples);
        rangeDirty_ = true;
        return true;
    }
    if (extension == ".raw" || extension == ".r32")
    {
        if (bytes.size() % sizeof(float) != 0)
            return false;
        std::uint32_t side = 0;
        if (!IsSquare(bytes.size() / sizeof(float), side))
            return false;
        resize(side, side, scale, 0.0f);
        std::memcpy(heightData.data(), bytes.data(), bytes.size());
        rangeDirty_ = true;
        return true;
    }
    if (extension == ".r16")
    {
        if (bytes.size() % 2 != 0)
            return false;
        std::uint32_t side = 0;
        if (!IsSquare(bytes.size() / 2, side))
            return false;
        resize(side, side, scale, 0.0f);
        for (std::size_t i = 0; i < heightData.size(); ++i)
        {
            const std::uint16_t value = static_cast<std::uint16_t>(bytes[i * 2]) |
                                        (static_cast<std::uint16_t>(bytes[i * 2 + 1]) << 8u);
            heightData[i] = static_cast<float>(value) / 65535.0f;
        }
        rangeDirty_ = true;
        return true;
    }
    if (extension == ".pgm")
    {
        std::size_t cursor = 0;
        std::string magic, w, h, max;
        if (!NextPgmToken(bytes, cursor, magic) || magic != "P5" || !NextPgmToken(bytes, cursor, w) ||
            !NextPgmToken(bytes, cursor, h) || !NextPgmToken(bytes, cursor, max))
            return false;
        const std::uint32_t imageWidth = static_cast<std::uint32_t>(std::stoul(w));
        const std::uint32_t imageHeight = static_cast<std::uint32_t>(std::stoul(h));
        const int maximum = std::stoi(max);
        while (cursor < bytes.size() && std::isspace(bytes[cursor]) != 0)
            ++cursor;
        const std::size_t sampleBytes = maximum > 255 ? 2 : 1;
        if (cursor + static_cast<std::size_t>(imageWidth) * imageHeight * sampleBytes > bytes.size())
            return false;
        resize(imageWidth, imageHeight, scale, 0.0f);
        for (std::size_t i = 0; i < heightData.size(); ++i)
        {
            const int value = sampleBytes == 1 ? bytes[cursor + i]
                                               : (static_cast<int>(bytes[cursor + i * 2]) << 8) | bytes[cursor + i * 2 + 1];
            heightData[i] = static_cast<float>(value) / static_cast<float>(std::max(maximum, 1));
        }
        rangeDirty_ = true;
        return true;
    }
    if (extension == ".tga")
    {
        if (bytes.size() < 18)
            return false;
        const std::uint8_t imageType = bytes[2];
        const std::uint16_t imageWidth = static_cast<std::uint16_t>(bytes[12] | (bytes[13] << 8u));
        const std::uint16_t imageHeight = static_cast<std::uint16_t>(bytes[14] | (bytes[15] << 8u));
        const std::uint8_t bitsPerPixel = bytes[16];
        const std::size_t offset = 18u + bytes[0];
        const std::size_t bytesPerPixel = bitsPerPixel / 8u;
        if ((imageType != 2 && imageType != 3) || imageWidth == 0 || imageHeight == 0 ||
            (bytesPerPixel != 1 && bytesPerPixel != 2 && bytesPerPixel != 3 && bytesPerPixel != 4) ||
            offset + static_cast<std::size_t>(imageWidth) * imageHeight * bytesPerPixel > bytes.size())
            return false;
        resize(imageWidth, imageHeight, scale, 0.0f);
        const bool topOrigin = (bytes[17] & 0x20u) != 0;
        for (std::uint32_t y = 0; y < imageHeight; ++y)
        {
            for (std::uint32_t x = 0; x < imageWidth; ++x)
            {
                const std::uint32_t sourceY = topOrigin ? y : imageHeight - 1u - y;
                const std::size_t source = offset + (static_cast<std::size_t>(sourceY) * imageWidth + x) * bytesPerPixel;
                float normalized = 0.0f;
                if (imageType == 3 && bytesPerPixel == 2)
                    normalized = static_cast<float>(bytes[source] | (bytes[source + 1] << 8u)) / 65535.0f;
                else if (imageType == 3)
                    normalized = static_cast<float>(bytes[source]) / 255.0f;
                else
                    normalized = (0.114f * bytes[source] + 0.587f * bytes[source + 1] + 0.299f * bytes[source + 2]) / 255.0f;
                heightData[index(x, y)] = normalized;
            }
        }
        rangeDirty_ = true;
        return true;
    }

    return false;
}

bool Heightmap::exportToImage(const std::string& imagePath) const
{
    if (width == 0 || height == 0 || heightData.empty())
        return false;
    const std::string extension = Extension(imagePath);
    if (extension == ".raw" || extension == ".r32")
    {
        std::vector<std::uint8_t> bytes(heightData.size() * sizeof(float));
        std::memcpy(bytes.data(), heightData.data(), bytes.size());
        return WriteAllBytes(imagePath, bytes);
    }
    const float minimum = getMinHeight();
    const float maximum = getMaxHeight();
    const float range = std::max(1e-6f, maximum - minimum);
    if (extension == ".png")
    {
        png::GrayImage image;
        image.width = width;
        image.height = height;
        image.samples.resize(heightData.size());
        for (std::size_t i = 0; i < heightData.size(); ++i)
            image.samples[i] = Saturate((heightData[i] - minimum) / range);
        std::vector<std::uint8_t> bytes;
        return png::EncodeGray16(image, bytes) && WriteAllBytes(imagePath, bytes);
    }
    if (extension == ".r16")
    {
        std::vector<std::uint8_t> bytes(heightData.size() * 2);
        for (std::size_t i = 0; i < heightData.size(); ++i)
        {
            const auto value = static_cast<std::uint16_t>(Saturate((heightData[i] - minimum) / range) * 65535.0f + 0.5f);
            bytes[i * 2] = static_cast<std::uint8_t>(value & 0xffu);
            bytes[i * 2 + 1] = static_cast<std::uint8_t>(value >> 8u);
        }
        return WriteAllBytes(imagePath, bytes);
    }
    if (extension == ".tga")
    {
        std::vector<std::uint8_t> bytes(18 + heightData.size());
        bytes[2] = 3; // uncompressed grayscale
        bytes[12] = static_cast<std::uint8_t>(width & 0xffu);
        bytes[13] = static_cast<std::uint8_t>(width >> 8u);
        bytes[14] = static_cast<std::uint8_t>(height & 0xffu);
        bytes[15] = static_cast<std::uint8_t>(height >> 8u);
        bytes[16] = 8;
        bytes[17] = 0x20; // top-left origin
        for (std::size_t i = 0; i < heightData.size(); ++i)
            bytes[18 + i] = static_cast<std::uint8_t>(Saturate((heightData[i] - minimum) / range) * 255.0f + 0.5f);
        return WriteAllBytes(imagePath, bytes);
    }

    // PGM is the default human-inspectable export format.
    std::ostringstream header;
    header << "P5\n" << width << ' ' << height << "\n65535\n";
    const std::string text = header.str();
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.reserve(bytes.size() + heightData.size() * 2);
    for (float value : heightData)
    {
        const auto sample = static_cast<std::uint16_t>(Saturate((value - minimum) / range) * 65535.0f + 0.5f);
        bytes.push_back(static_cast<std::uint8_t>(sample >> 8u));
        bytes.push_back(static_cast<std::uint8_t>(sample & 0xffu));
    }
    return WriteAllBytes(imagePath, bytes);
}
} // namespace wowedit
