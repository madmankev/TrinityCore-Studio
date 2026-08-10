#include "terrain/texture_splatmap.h"

#include "utils/math_utils.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace wowedit
{
TextureSplatmap::TextureSplatmap(std::uint32_t width, std::uint32_t height)
{
    resize(width, height);
}

void TextureSplatmap::resize(std::uint32_t width, std::uint32_t height)
{
    width_ = width;
    height_ = height;
    splatData.assign(static_cast<std::size_t>(width_) * height_, glm::u8vec4(255, 0, 0, 0));
    vertexColors.assign(splatData.size(), {});
    ++gpuRevision_;
}

void TextureSplatmap::setLayer(std::uint8_t layerIndex, const std::string& texturePath, glm::vec4 channels)
{
    if (layerIndex >= activeLayers.size())
        return;
    TextureLayer& layer = activeLayers[layerIndex];
    layer.texturePath = texturePath;
    layer.channelMask = channels;
    auto found = std::find(zoneTextures_.begin(), zoneTextures_.end(), texturePath);
    if (found == zoneTextures_.end() && addZoneTexture(texturePath))
        found = std::prev(zoneTextures_.end());
    layer.textureId = found == zoneTextures_.end() ? 0u : static_cast<std::uint32_t>(std::distance(zoneTextures_.begin(), found));
    layer.gpuTexture = std::make_shared<Texture>(Texture{layer.textureId, 0, 0, texturePath});
    ++gpuRevision_;
}

void TextureSplatmap::swapLayer(std::uint8_t indexA, std::uint8_t indexB)
{
    if (indexA >= activeLayers.size() || indexB >= activeLayers.size() || indexA == indexB)
        return;
    std::swap(activeLayers[indexA], activeLayers[indexB]);
    for (glm::u8vec4& texel : splatData)
        std::swap(texel[indexA], texel[indexB]);
    ++gpuRevision_;
}

bool TextureSplatmap::addZoneTexture(const std::string& texturePath)
{
    if (texturePath.empty() || zoneTextures_.size() >= 13 ||
        std::find(zoneTextures_.begin(), zoneTextures_.end(), texturePath) != zoneTextures_.end())
        return false;
    zoneTextures_.push_back(texturePath);
    return true;
}

void TextureSplatmap::setZoneTextures(std::vector<std::string> textures)
{
    zoneTextures_.clear();
    for (const std::string& texture : textures)
    {
        if (zoneTextures_.size() == 13)
            break;
        if (!texture.empty() && std::find(zoneTextures_.begin(), zoneTextures_.end(), texture) == zoneTextures_.end())
            zoneTextures_.push_back(texture);
    }
    ++gpuRevision_;
}

void TextureSplatmap::paintTexture(std::uint32_t texelX, std::uint32_t texelY, std::uint8_t targetLayer,
                                   float opacity, float hardness, float falloff)
{
    if (targetLayer >= 4 || !inBounds(static_cast<int>(texelX), static_cast<int>(texelY)))
        return;
    const float amount = Saturate(opacity) * Saturate(hardness) * Saturate(falloff);
    glm::vec4 weights = ToFloat(getTexel(texelX, texelY));
    weights[targetLayer] = glm::mix(weights[targetLayer], 1.0f, amount);
    const float remainder = std::max(0.0f, 1.0f - weights[targetLayer]);
    float otherSum = 0.0f;
    for (std::uint8_t layer = 0; layer < 4; ++layer)
        if (layer != targetLayer)
            otherSum += weights[layer];
    if (otherSum > 1e-6f)
    {
        for (std::uint8_t layer = 0; layer < 4; ++layer)
            if (layer != targetLayer)
                weights[layer] = weights[layer] / otherSum * remainder;
    }
    else
    {
        for (std::uint8_t layer = 0; layer < 4; ++layer)
            if (layer != targetLayer)
                weights[layer] = remainder / 3.0f;
    }
    setTexel(texelX, texelY, ToByte(weights));
}

void TextureSplatmap::paintBrush(glm::vec2 center, float radius, std::uint8_t targetLayer, float opacity,
                                 float hardness, BrushFalloffType falloff)
{
    if (radius <= 0.0f || targetLayer >= 4)
        return;
    const int minX = std::max(0, static_cast<int>(std::floor(center.x - radius)));
    const int maxX = std::min(static_cast<int>(width_) - 1, static_cast<int>(std::ceil(center.x + radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(center.y - radius)));
    const int maxY = std::min(static_cast<int>(height_) - 1, static_cast<int>(std::ceil(center.y + radius)));
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x)
        {
            const float normalized = glm::length(glm::vec2(static_cast<float>(x), static_cast<float>(y)) - center) / radius;
            if (normalized > 1.0f)
                continue;
            float curve = 1.0f - normalized;
            if (falloff == BrushFalloffType::Smoothstep)
                curve = 1.0f - math::SmoothStep(0.0f, 1.0f, normalized);
            else if (falloff == BrushFalloffType::Gaussian)
                curve = math::Gaussian(normalized) * (1.0f - normalized);
            else if (falloff == BrushFalloffType::Constant)
                curve = 1.0f;
            paintTexture(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), targetLayer, opacity, hardness, curve);
        }
    ++gpuRevision_;
}

void TextureSplatmap::floodFill(std::uint32_t texelX, std::uint32_t texelY, std::uint8_t targetLayer,
                                float tolerance, float maxHeightDifference, const Heightmap* heightmap)
{
    if (targetLayer >= 4 || !inBounds(static_cast<int>(texelX), static_cast<int>(texelY)))
        return;
    const glm::vec4 source = ToFloat(getTexel(texelX, texelY));
    const float sourceHeight = heightmap ? heightmap->getInterpolatedHeight(static_cast<float>(texelX), static_cast<float>(texelY)) : 0.0f;
    std::vector<bool> visited(splatData.size(), false);
    std::queue<glm::ivec2> frontier;
    frontier.emplace(static_cast<int>(texelX), static_cast<int>(texelY));
    while (!frontier.empty())
    {
        const glm::ivec2 current = frontier.front();
        frontier.pop();
        if (!inBounds(current.x, current.y))
            continue;
        const std::size_t i = index(static_cast<std::uint32_t>(current.x), static_cast<std::uint32_t>(current.y));
        if (visited[i])
            continue;
        visited[i] = true;
        const glm::vec4 candidate = ToFloat(splatData[i]);
        if (glm::length(candidate - source) > std::max(0.0f, tolerance))
            continue;
        if (heightmap && maxHeightDifference >= 0.0f &&
            std::abs(heightmap->getInterpolatedHeight(static_cast<float>(current.x), static_cast<float>(current.y)) - sourceHeight) > maxHeightDifference)
            continue;
        splatData[i] = glm::u8vec4(0, 0, 0, 0);
        splatData[i][targetLayer] = 255;
        frontier.emplace(current.x + 1, current.y);
        frontier.emplace(current.x - 1, current.y);
        frontier.emplace(current.x, current.y + 1);
        frontier.emplace(current.x, current.y - 1);
    }
    ++gpuRevision_;
}

void TextureSplatmap::smear(glm::vec2 from, glm::vec2 to, float radius, float strength)
{
    if (radius <= 0.0f || strength <= 0.0f)
        return;
    const glm::vec2 direction = to - from;
    const int minX = std::max(0, static_cast<int>(std::floor(std::min(from.x, to.x) - radius)));
    const int maxX = std::min(static_cast<int>(width_) - 1, static_cast<int>(std::ceil(std::max(from.x, to.x) + radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min(from.y, to.y) - radius)));
    const int maxY = std::min(static_cast<int>(height_) - 1, static_cast<int>(std::ceil(std::max(from.y, to.y) + radius)));
    const std::vector<glm::u8vec4> before = splatData;
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x)
        {
            const glm::vec2 point(static_cast<float>(x), static_cast<float>(y));
            const float dist = glm::length(point - to);
            if (dist > radius)
                continue;
            const glm::ivec2 source = glm::ivec2(glm::round(point - direction));
            if (!inBounds(source.x, source.y))
                continue;
            const float amount = Saturate(strength * (1.0f - dist / radius));
            const glm::vec4 mixed = glm::mix(ToFloat(before[index(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y))]),
                                              ToFloat(before[index(static_cast<std::uint32_t>(source.x), static_cast<std::uint32_t>(source.y))]), amount);
            splatData[index(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y))] = ToByte(mixed);
        }
    ++gpuRevision_;
}

void TextureSplatmap::cloneStamp(glm::ivec2 source, glm::ivec2 destination, float radius, bool aligned)
{
    if (radius <= 0.0f)
        return;
    const std::vector<glm::u8vec4> before = splatData;
    const int r = static_cast<int>(std::ceil(radius));
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
        {
            if (std::sqrt(static_cast<float>(x * x + y * y)) > radius)
                continue;
            const glm::ivec2 target = destination + glm::ivec2(x, y);
            const glm::ivec2 sampled = aligned ? source + glm::ivec2(x, y) : source;
            if (!inBounds(target.x, target.y) || !inBounds(sampled.x, sampled.y))
                continue;
            splatData[index(static_cast<std::uint32_t>(target.x), static_cast<std::uint32_t>(target.y))] =
                before[index(static_cast<std::uint32_t>(sampled.x), static_cast<std::uint32_t>(sampled.y))];
        }
    ++gpuRevision_;
}

void TextureSplatmap::paintGradient(glm::vec2 start, glm::vec2 end, float radius, std::uint8_t fromLayer,
                                     std::uint8_t toLayer, float opacity, bool radial)
{
    if (fromLayer >= 4 || toLayer >= 4 || width_ == 0 || height_ == 0)
        return;
    const glm::vec2 axis = end - start;
    const float axisLengthSquared = glm::max(glm::dot(axis, axis), 1e-6f);
    const int minX = std::max(0, static_cast<int>(std::floor(std::min(start.x, end.x) - radius)));
    const int maxX = std::min(static_cast<int>(width_) - 1, static_cast<int>(std::ceil(std::max(start.x, end.x) + radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min(start.y, end.y) - radius)));
    const int maxY = std::min(static_cast<int>(height_) - 1, static_cast<int>(std::ceil(std::max(start.y, end.y) + radius)));
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x)
        {
            const glm::vec2 point(static_cast<float>(x), static_cast<float>(y));
            const float t = radial
                ? Saturate(glm::length(point - start) / std::max(radius, 0.001f))
                : Saturate(glm::dot(point - start, axis) / axisLengthSquared);
            glm::vec4 weights = ToFloat(getTexel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
            const float blend = Saturate(opacity) * t;
            weights[fromLayer] *= 1.0f - blend;
            weights[toLayer] = glm::mix(weights[toLayer], 1.0f, blend);
            setTexel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), ToByte(weights));
        }
    ++gpuRevision_;
}

void TextureSplatmap::paintAlphaMask(glm::vec2 center, float radius, std::uint8_t targetLayer, float opacity,
                                     BrushFalloffType falloff)
{
    // Detail alpha is represented by the selected splat channel in the portable
    // format. Render backends can bind that channel as an overlay/detail mask.
    paintBrush(center, radius, targetLayer, opacity, 1.0f, falloff);
}

std::uint8_t TextureSplatmap::sampleDominantLayer(std::uint32_t texelX, std::uint32_t texelY) const
{
    const glm::u8vec4 sample = getTexel(texelX, texelY);
    std::uint8_t result = 0;
    for (std::uint8_t layer = 1; layer < 4; ++layer)
        if (sample[layer] > sample[result])
            result = layer;
    return result;
}

TextureSplatmap::VertexColor TextureSplatmap::sampleVertexColor(std::uint32_t vertexIndex) const
{
    return vertexIndex < vertexColors.size() ? vertexColors[vertexIndex] : VertexColor{};
}

void TextureSplatmap::applyHeightBasedBlending(const Heightmap& heightmap)
{
    if (width_ == 0 || height_ == 0 || heightmap.getWidth() == 0 || heightmap.getHeightCount() == 0)
        return;
    const float minHeight = heightmap.getMinHeight();
    const float range = std::max(0.0001f, heightmap.getMaxHeight() - minHeight);
    for (std::uint32_t y = 0; y < height_; ++y)
        for (std::uint32_t x = 0; x < width_; ++x)
        {
            const float hx = static_cast<float>(x) / std::max(1u, width_ - 1u) * (heightmap.getWidth() - 1u) * heightmap.getScale();
            const float hz = static_cast<float>(y) / std::max(1u, height_ - 1u) * (heightmap.getHeightCount() - 1u) * heightmap.getScale();
            const float normalized = Saturate((heightmap.getInterpolatedHeight(hx, hz) - minHeight) / range);
            // Sand/low layer (R), grass (G), rock (B), snow/high layer (A).
            glm::vec4 weights(1.0f - normalized, 1.0f - std::abs(normalized - 0.45f) * 2.0f,
                              normalized > 0.55f ? (normalized - 0.55f) / 0.30f : 0.0f,
                              normalized > 0.80f ? (normalized - 0.80f) / 0.20f : 0.0f);
            setTexel(x, y, ToByte(Normalize(weights)));
        }
    ++gpuRevision_;
}

void TextureSplatmap::paintVertexColor(std::uint32_t vertexIndex, glm::vec3 color, float alpha)
{
    if (vertexIndex >= vertexColors.size())
        return;
    vertexColors[vertexIndex].colorTint = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
    vertexColors[vertexIndex].alpha = Saturate(alpha);
    ++gpuRevision_;
}

glm::u8vec4 TextureSplatmap::getTexel(std::uint32_t x, std::uint32_t y) const
{
    return inBounds(static_cast<int>(x), static_cast<int>(y)) ? splatData[index(x, y)] : glm::u8vec4(0);
}

void TextureSplatmap::setTexel(std::uint32_t x, std::uint32_t y, glm::u8vec4 value)
{
    if (!inBounds(static_cast<int>(x), static_cast<int>(y)))
        return;
    splatData[index(x, y)] = value;
    ++gpuRevision_;
}

void TextureSplatmap::uploadToGPU()
{
    // The Vulkan/OpenGL backend observes this monotonic revision and uploads lazily;
    // keeping CPU edits renderer-independent enables headless authoring and testing.
    ++gpuRevision_;
}

std::size_t TextureSplatmap::index(std::uint32_t x, std::uint32_t y) const
{
    return static_cast<std::size_t>(y) * width_ + x;
}

bool TextureSplatmap::inBounds(int x, int y) const
{
    return x >= 0 && y >= 0 && x < static_cast<int>(width_) && y < static_cast<int>(height_);
}

glm::vec4 TextureSplatmap::ToFloat(glm::u8vec4 value)
{
    return glm::vec4(value) / 255.0f;
}

glm::u8vec4 TextureSplatmap::ToByte(glm::vec4 value)
{
    const glm::vec4 normalized = Normalize(glm::max(value, glm::vec4(0.0f)));
    return glm::u8vec4(glm::round(normalized * 255.0f));
}

glm::vec4 TextureSplatmap::Normalize(glm::vec4 weights)
{
    const float total = weights.r + weights.g + weights.b + weights.a;
    return total > 1e-6f ? weights / total : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
}
} // namespace wowedit
