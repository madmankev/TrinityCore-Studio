#pragma once
#include "terrain/texture_splatmap.h"
#include <cstdint>
namespace wowedit
{
class TexturePalette { public: void selectLayer(std::uint8_t value) { selected_ = value < 4 ? value : 0; } std::uint8_t selectedLayer() const { return selected_; } const TextureSplatmap::TextureLayer* selected(const TextureSplatmap& map) const { return &map.getActiveLayers()[selected_]; } private: std::uint8_t selected_ = 0; };
} // namespace wowedit
