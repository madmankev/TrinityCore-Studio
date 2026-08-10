#include "io/png_codec.h"

extern "C"
{
#include "zlib.h"
}

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace wowedit::png
{
namespace
{
constexpr std::array<std::uint8_t, 8> kSignature = {137, 80, 78, 71, 13, 10, 26, 10};

void SetError(std::string* error, const char* message)
{
    if (error)
        *error = message;
}

std::uint32_t ReadBe32(const std::uint8_t* value)
{
    return (static_cast<std::uint32_t>(value[0]) << 24u) |
           (static_cast<std::uint32_t>(value[1]) << 16u) |
           (static_cast<std::uint32_t>(value[2]) << 8u) |
           static_cast<std::uint32_t>(value[3]);
}

void WriteBe32(std::vector<std::uint8_t>& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value >> 24u));
    output.push_back(static_cast<std::uint8_t>(value >> 16u));
    output.push_back(static_cast<std::uint8_t>(value >> 8u));
    output.push_back(static_cast<std::uint8_t>(value));
}

bool Inflate(const std::vector<std::uint8_t>& input, std::size_t expected, std::vector<std::uint8_t>& output)
{
    if (expected > static_cast<std::size_t>(std::numeric_limits<uInt>::max()) ||
        input.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max()))
        return false;
    output.assign(expected, 0);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit(&stream) != Z_OK)
        return false;
    const int result = inflate(&stream, Z_FINISH);
    const bool valid = result == Z_STREAM_END && stream.total_out == expected;
    inflateEnd(&stream);
    return valid;
}

bool Deflate(const std::vector<std::uint8_t>& input, std::vector<std::uint8_t>& output)
{
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max()))
        return false;
    const std::size_t capacity = input.size() + input.size() / 16u + 128u;
    if (capacity > static_cast<std::size_t>(std::numeric_limits<uInt>::max()))
        return false;
    output.assign(capacity, 0);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (deflateInit(&stream, Z_BEST_COMPRESSION) != Z_OK)
        return false;
    const int result = deflate(&stream, Z_FINISH);
    const bool valid = result == Z_STREAM_END;
    if (valid)
        output.resize(stream.total_out);
    deflateEnd(&stream);
    return valid;
}

int Paeth(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
}

bool Unfilter(const std::vector<std::uint8_t>& input, std::uint32_t width, std::uint32_t height,
              std::size_t stride, std::size_t bytesPerPixel, std::vector<std::uint8_t>& output)
{
    if (input.size() != (stride + 1u) * height)
        return false;
    output.assign(stride * height, 0);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const std::uint8_t filter = input[static_cast<std::size_t>(y) * (stride + 1u)];
        const std::uint8_t* source = input.data() + static_cast<std::size_t>(y) * (stride + 1u) + 1u;
        std::uint8_t* destination = output.data() + static_cast<std::size_t>(y) * stride;
        const std::uint8_t* prior = y == 0 ? nullptr : output.data() + static_cast<std::size_t>(y - 1u) * stride;
        for (std::size_t x = 0; x < stride; ++x)
        {
            const int left = x >= bytesPerPixel ? destination[x - bytesPerPixel] : 0;
            const int up = prior ? prior[x] : 0;
            const int upLeft = prior && x >= bytesPerPixel ? prior[x - bytesPerPixel] : 0;
            switch (filter)
            {
            case 0: destination[x] = source[x]; break;
            case 1: destination[x] = static_cast<std::uint8_t>(source[x] + left); break;
            case 2: destination[x] = static_cast<std::uint8_t>(source[x] + up); break;
            case 3: destination[x] = static_cast<std::uint8_t>(source[x] + (left + up) / 2); break;
            case 4: destination[x] = static_cast<std::uint8_t>(source[x] + Paeth(left, up, upLeft)); break;
            default: return false;
            }
        }
    }
    (void)width;
    return true;
}

float ReadSample(const std::uint8_t* pixels, int channel, int channels, int bitDepth)
{
    if (bitDepth == 16)
    {
        const std::size_t offset = static_cast<std::size_t>(channel) * 2u;
        return static_cast<float>((static_cast<std::uint16_t>(pixels[offset]) << 8u) | pixels[offset + 1u]) / 65535.0f;
    }
    (void)channels;
    return static_cast<float>(pixels[channel]) / 255.0f;
}

void WriteChunk(std::vector<std::uint8_t>& output, const char type[4], const std::vector<std::uint8_t>& data)
{
    WriteBe32(output, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeOffset = output.size();
    output.insert(output.end(), type, type + 4);
    output.insert(output.end(), data.begin(), data.end());
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, output.data() + typeOffset, static_cast<uInt>(4 + data.size()));
    WriteBe32(output, static_cast<std::uint32_t>(crc));
}
} // namespace

bool DecodeGray(const std::vector<std::uint8_t>& bytes, GrayImage& image, std::string* error)
{
    image = {};
    if (bytes.size() < kSignature.size() || !std::equal(kSignature.begin(), kSignature.end(), bytes.begin()))
    {
        SetError(error, "Invalid PNG signature");
        return false;
    }
    std::uint32_t width = 0, height = 0;
    int bitDepth = 0, colorType = 0;
    bool hasHeader = false;
    std::vector<std::uint8_t> compressed;
    for (std::size_t cursor = kSignature.size(); cursor + 12u <= bytes.size();)
    {
        const std::uint32_t length = ReadBe32(bytes.data() + cursor);
        cursor += 4;
        if (cursor + 4u + length + 4u > bytes.size())
        {
            SetError(error, "Truncated PNG chunk");
            return false;
        }
        const char* type = reinterpret_cast<const char*>(bytes.data() + cursor);
        cursor += 4;
        const std::uint8_t* data = bytes.data() + cursor;
        cursor += length + 4u; // payload and CRC
        if (std::memcmp(type, "IHDR", 4) == 0)
        {
            if (length != 13u || hasHeader)
            {
                SetError(error, "Invalid PNG header");
                return false;
            }
            width = ReadBe32(data);
            height = ReadBe32(data + 4);
            bitDepth = data[8];
            colorType = data[9];
            if (data[10] != 0 || data[11] != 0 || data[12] != 0)
            {
                SetError(error, "PNG compression/filter/interlace unsupported");
                return false;
            }
            hasHeader = true;
        }
        else if (std::memcmp(type, "IDAT", 4) == 0)
            compressed.insert(compressed.end(), data, data + length);
        else if (std::memcmp(type, "IEND", 4) == 0)
            break;
    }
    int channels = 0;
    switch (colorType)
    {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default:
        SetError(error, "PNG color type unsupported (use grayscale/RGB/RGBA)");
        return false;
    }
    if (!hasHeader || width == 0 || height == 0 || (bitDepth != 8 && bitDepth != 16))
    {
        SetError(error, "PNG dimensions or bit depth unsupported");
        return false;
    }
    const std::size_t bytesPerSample = static_cast<std::size_t>(bitDepth / 8);
    const std::size_t bytesPerPixel = static_cast<std::size_t>(channels) * bytesPerSample;
    if (width > std::numeric_limits<std::size_t>::max() / bytesPerPixel ||
        height > std::numeric_limits<std::size_t>::max() / (static_cast<std::size_t>(width) * bytesPerPixel + 1u))
    {
        SetError(error, "PNG dimensions too large");
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(width) * bytesPerPixel;
    std::vector<std::uint8_t> filtered;
    if (!Inflate(compressed, (stride + 1u) * height, filtered))
    {
        SetError(error, "PNG zlib stream could not be decoded");
        return false;
    }
    std::vector<std::uint8_t> pixels;
    if (!Unfilter(filtered, width, height, stride, bytesPerPixel, pixels))
    {
        SetError(error, "PNG filter stream unsupported");
        return false;
    }
    image.width = width;
    image.height = height;
    image.samples.resize(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::uint8_t* pixel = pixels.data() + (static_cast<std::size_t>(y) * width + x) * bytesPerPixel;
            if (colorType == 0 || colorType == 4)
                image.samples[static_cast<std::size_t>(y) * width + x] = ReadSample(pixel, 0, channels, bitDepth);
            else
            {
                const float red = ReadSample(pixel, 0, channels, bitDepth);
                const float green = ReadSample(pixel, 1, channels, bitDepth);
                const float blue = ReadSample(pixel, 2, channels, bitDepth);
                image.samples[static_cast<std::size_t>(y) * width + x] = red * 0.299f + green * 0.587f + blue * 0.114f;
            }
        }
    return true;
}

bool EncodeGray16(const GrayImage& image, std::vector<std::uint8_t>& bytes, std::string* error)
{
    bytes.clear();
    if (image.width == 0 || image.height == 0 || image.samples.size() != static_cast<std::size_t>(image.width) * image.height)
    {
        SetError(error, "Invalid grayscale PNG image");
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(image.width) * 2u;
    std::vector<std::uint8_t> raw((stride + 1u) * image.height, 0);
    for (std::uint32_t y = 0; y < image.height; ++y)
    {
        std::uint8_t* row = raw.data() + static_cast<std::size_t>(y) * (stride + 1u);
        row[0] = 0; // no filter: deterministic and easy to recover
        for (std::uint32_t x = 0; x < image.width; ++x)
        {
            const auto value = static_cast<std::uint16_t>(std::clamp(image.samples[static_cast<std::size_t>(y) * image.width + x], 0.0f, 1.0f) * 65535.0f + 0.5f);
            row[1u + x * 2u] = static_cast<std::uint8_t>(value >> 8u);
            row[2u + x * 2u] = static_cast<std::uint8_t>(value);
        }
    }
    std::vector<std::uint8_t> compressed;
    if (!Deflate(raw, compressed))
    {
        SetError(error, "PNG zlib stream could not be encoded");
        return false;
    }
    bytes.insert(bytes.end(), kSignature.begin(), kSignature.end());
    std::vector<std::uint8_t> header;
    WriteBe32(header, image.width);
    WriteBe32(header, image.height);
    header.insert(header.end(), {16, 0, 0, 0, 0}); // 16 bit gray, standard compression/filter/no interlace
    WriteChunk(bytes, "IHDR", header);
    WriteChunk(bytes, "IDAT", compressed);
    WriteChunk(bytes, "IEND", {});
    return true;
}
} // namespace wowedit::png
