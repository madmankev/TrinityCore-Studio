#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace wowedit
{
/** Falloff curves shared by terrain, texture, and object-scatter brushes. */
enum class BrushFalloffType
{
    Linear,
    Smoothstep,
    Gaussian,
    Constant
};

/**
 * Editable square/rectangular height field. World X/Z map to height samples using
 * scale (world units per vertex); Y is the stored terrain height.
 */
class Heightmap
{
public:
    enum class BrushOperation { Raise, Lower, Smooth, Flatten, Noise };

    Heightmap() = default;
    Heightmap(std::uint32_t width, std::uint32_t height, float scale = 1.0f, float initialHeight = 0.0f);

    void resize(std::uint32_t width, std::uint32_t height, float scale = 1.0f, float initialHeight = 0.0f);
    std::uint32_t getWidth() const { return width; }
    std::uint32_t getHeightCount() const { return height; }
    float getScale() const { return scale; }
    void setScale(float worldUnitsPerPixel);

    float getHeight(std::uint32_t x, std::uint32_t y) const;
    void setHeight(std::uint32_t x, std::uint32_t y, float heightValue);
    float getInterpolatedHeight(float worldX, float worldZ) const;
    glm::vec3 calculateNormal(std::uint32_t x, std::uint32_t y) const;

    bool importFromImage(const std::string& imagePath); // RAW/R16/PGM/PNG/TGA
    bool exportToImage(const std::string& imagePath) const;

    void modifyHeight(std::uint32_t x, std::uint32_t y, float delta);
    void applyBrush(glm::vec3 worldPos, float radius, float strength, float falloff, BrushOperation op);
    void applyBrush(glm::vec3 worldPos, float radius, float strength, BrushFalloffType falloff,
                    BrushOperation op, float pressure = 1.0f);

    void setFlattenTarget(float targetHeight) { flattenTarget_ = targetHeight; }
    float getFlattenTarget() const { return flattenTarget_; }
    void setNoiseSettings(float frequency, float amplitude, std::uint32_t octaves, std::uint32_t seed);

    float getMinHeight() const;
    float getMaxHeight() const;
    const std::vector<float>& data() const { return heightData; }
    std::vector<float>& mutableData();

private:
    std::size_t index(std::uint32_t x, std::uint32_t y) const;
    bool inBounds(std::uint32_t x, std::uint32_t y) const;
    void recalculateRange() const;
    float brushWeight(float normalizedDistance, BrushFalloffType falloff) const;

    std::vector<float> heightData;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float scale = 1.0f;
    mutable float minHeight = 0.0f;
    mutable float maxHeight = 0.0f;
    mutable bool rangeDirty_ = false;
    float flattenTarget_ = 0.0f;
    float noiseFrequency_ = 0.1f;
    float noiseAmplitude_ = 1.0f;
    std::uint32_t noiseOctaves_ = 3;
    std::uint32_t noiseSeed_ = 0;
};
} // namespace wowedit
