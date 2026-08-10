#include "terrain/water_plane.h"

#include "core/types.h"
#include "terrain/heightmap.h"

#include <algorithm>

namespace wowedit
{
void WaterPlane::resize(std::uint32_t width, std::uint32_t height)
{
    width_ = width;
    height_ = height;
    liquidCells.assign(static_cast<std::size_t>(width_) * height_, {});
}

void WaterPlane::setGlobalHeight(float height)
{
    waterHeight = height;
}

void WaterPlane::setWaterType(WaterType value)
{
    type = value;
    switch (type)
    {
    case WaterType::Lava: tintColor = {0.95f, 0.17f, 0.02f, 0.88f}; break;
    case WaterType::Slime: tintColor = {0.24f, 0.72f, 0.09f, 0.72f}; break;
    case WaterType::Poison: tintColor = {0.46f, 0.07f, 0.58f, 0.74f}; break;
    default: break;
    }
}

void WaterPlane::editLiquidCell(std::uint32_t x, std::uint32_t y, bool filled)
{
    if (x < width_ && y < height_)
        liquidCells[index(x, y)].exists = filled;
}

bool WaterPlane::hasLiquidCell(std::uint32_t x, std::uint32_t y) const
{
    return x < width_ && y < height_ && liquidCells[index(x, y)].exists;
}

void WaterPlane::setCellVariation(std::uint32_t x, std::uint32_t y, float variation)
{
    if (x < width_ && y < height_)
        liquidCells[index(x, y)].heightVariation = variation;
}

void WaterPlane::render(const Camera& camera, const Heightmap& terrain) const
{
    if (renderCallback_)
        renderCallback_(camera, terrain, *this);
}

void WaterPlane::updateSimulation(float deltaTime)
{
    simulationTime_ += std::max(0.0f, deltaTime) * waveSpeed;
}

void WaterPlane::setTintColor(glm::vec4 color)
{
    tintColor = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));
}

void WaterPlane::setWaveProperties(float height, float speed)
{
    waveHeight = std::max(0.0f, height);
    waveSpeed = std::max(0.0f, speed);
}

std::size_t WaterPlane::index(std::uint32_t x, std::uint32_t y) const
{
    return static_cast<std::size_t>(y) * width_ + x;
}
} // namespace wowedit
