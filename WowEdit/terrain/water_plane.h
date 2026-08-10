#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace wowedit
{
class Camera;
class Heightmap;

enum class WaterType { Ocean, Lake, River, Lava, Slime, Poison };

class WaterPlane
{
public:
    struct LiquidCell
    {
        bool exists = false;
        float heightVariation = 0.0f;
    };

    void resize(std::uint32_t width, std::uint32_t height);
    void setGlobalHeight(float height);
    float getGlobalHeight() const { return waterHeight; }
    void setWaterType(WaterType value);
    WaterType getWaterType() const { return type; }
    float getOpacity() const { return opacity; }
    bool shoreBlendEnabled() const { return hasShoreBlend; }
    void setOpacity(float value) { opacity = glm::clamp(value, 0.0f, 1.0f); }
    void setShoreBlend(bool value) { hasShoreBlend = value; }
    void editLiquidCell(std::uint32_t x, std::uint32_t y, bool filled);
    bool hasLiquidCell(std::uint32_t x, std::uint32_t y) const;

    using RenderCallback = std::function<void(const Camera&, const Heightmap&, const WaterPlane&)>;
    // The active graphics backend installs a callback and owns actual API submission.
    void setRenderCallback(RenderCallback callback) { renderCallback_ = std::move(callback); }
    void render(const Camera& camera, const Heightmap& terrain) const;
    void updateSimulation(float deltaTime);
    void setTintColor(glm::vec4 color);
    void setWaveProperties(float height, float speed);
    glm::vec4 getTintColor() const { return tintColor; }
    float getWaveHeight() const { return waveHeight; }
    float getWaveSpeed() const { return waveSpeed; }
    float getSimulationTime() const { return simulationTime_; }
    const std::vector<LiquidCell>& cells() const { return liquidCells; }
    void setCellVariation(std::uint32_t x, std::uint32_t y, float variation);

private:
    std::size_t index(std::uint32_t x, std::uint32_t y) const;

    float waterHeight = 0.0f;
    WaterType type = WaterType::Lake;
    glm::vec4 tintColor{0.08f, 0.28f, 0.55f, 0.72f};
    float waveHeight = 0.15f;
    float waveSpeed = 1.0f;
    float opacity = 0.72f;
    bool hasShoreBlend = true;
    std::vector<LiquidCell> liquidCells;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    float simulationTime_ = 0.0f;
    RenderCallback renderCallback_;
};
} // namespace wowedit
