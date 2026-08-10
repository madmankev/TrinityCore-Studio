#pragma once

#include "terrain/heightmap.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace wowedit
{
struct Texture
{
    std::uint32_t id = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string sourcePath;
};

class TextureSplatmap
{
public:
    struct TextureLayer
    {
        std::uint32_t textureId = 0;
        glm::vec4 channelMask{0.0f};
        std::string texturePath;
        std::shared_ptr<Texture> gpuTexture;
    };

    struct VertexColor
    {
        glm::vec3 colorTint{1.0f};
        float alpha = 1.0f;
    };

    TextureSplatmap() = default;
    TextureSplatmap(std::uint32_t width, std::uint32_t height);

    void resize(std::uint32_t width, std::uint32_t height);
    std::uint32_t getWidth() const { return width_; }
    std::uint32_t getHeight() const { return height_; }

    // Layer management: zones can register 13 textures, while any tile exposes 4 channels.
    void setLayer(std::uint8_t index, const std::string& texturePath, glm::vec4 channels);
    void swapLayer(std::uint8_t indexA, std::uint8_t indexB);
    const std::array<TextureLayer, 4>& getActiveLayers() const { return activeLayers; }
    bool addZoneTexture(const std::string& texturePath);
    const std::vector<std::string>& getZoneTextures() const { return zoneTextures_; }
    void setZoneTextures(std::vector<std::string> textures);

    void paintTexture(std::uint32_t texelX, std::uint32_t texelY, std::uint8_t targetLayer,
                      float opacity, float hardness, float falloff);
    void paintBrush(glm::vec2 center, float radius, std::uint8_t targetLayer, float opacity,
                    float hardness, BrushFalloffType falloff = BrushFalloffType::Smoothstep);
    void floodFill(std::uint32_t texelX, std::uint32_t texelY, std::uint8_t targetLayer,
                   float tolerance, float maxHeightDifference = -1.0f, const Heightmap* heightmap = nullptr);
    void smear(glm::vec2 from, glm::vec2 to, float radius, float strength);
    void cloneStamp(glm::ivec2 source, glm::ivec2 destination, float radius, bool aligned);
    void paintGradient(glm::vec2 start, glm::vec2 end, float radius, std::uint8_t fromLayer,
                       std::uint8_t toLayer, float opacity, bool radial = false);
    void paintAlphaMask(glm::vec2 center, float radius, std::uint8_t targetLayer, float opacity,
                        BrushFalloffType falloff = BrushFalloffType::Smoothstep);
    std::uint8_t sampleDominantLayer(std::uint32_t texelX, std::uint32_t texelY) const;
    VertexColor sampleVertexColor(std::uint32_t vertexIndex) const;

    void applyHeightBasedBlending(const Heightmap& heightmap);
    void paintVertexColor(std::uint32_t vertexIndex, glm::vec3 color, float alpha);

    glm::u8vec4 getTexel(std::uint32_t x, std::uint32_t y) const;
    void setTexel(std::uint32_t x, std::uint32_t y, glm::u8vec4 value);
    const std::vector<glm::u8vec4>& getSplatData() const { return splatData; }
    const std::vector<VertexColor>& getVertexColors() const { return vertexColors; }

    void uploadToGPU();
    std::uint64_t gpuRevision() const { return gpuRevision_; }

private:
    std::size_t index(std::uint32_t x, std::uint32_t y) const;
    bool inBounds(int x, int y) const;
    static glm::vec4 ToFloat(glm::u8vec4 value);
    static glm::u8vec4 ToByte(glm::vec4 value);
    static glm::vec4 Normalize(glm::vec4 weights);

    std::array<TextureLayer, 4> activeLayers{};
    std::vector<glm::u8vec4> splatData;
    std::shared_ptr<Texture> heightBlendMap;
    std::shared_ptr<Texture> cavityMap;
    std::vector<VertexColor> vertexColors;
    std::vector<std::string> zoneTextures_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint64_t gpuRevision_ = 0;
};
} // namespace wowedit
