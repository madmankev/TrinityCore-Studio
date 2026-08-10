#include "editing/paint_tool.h"
#include "core/commands.h"
#include <cmath>
namespace wowedit
{
void PaintTool::paint(TextureSplatmap& splatmap, glm::vec2 center, float radius, std::uint8_t layer, float opacity, float hardness)
{
    const std::vector<glm::u8vec4> before = splatmap.getSplatData();
    splatmap.paintBrush(center, radius, layer, opacity, hardness);
    std::vector<TexturePaintChange> changes;
    const std::vector<glm::u8vec4>& after = splatmap.getSplatData();
    for (std::uint32_t y = 0; y < splatmap.getHeight(); ++y)
        for (std::uint32_t x = 0; x < splatmap.getWidth(); ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y) * splatmap.getWidth() + x;
            if (before[i] != after[i]) changes.push_back({x, y, before[i], after[i]});
        }
    if (changes.empty()) return;
    for (const TexturePaintChange& change : changes) splatmap.setTexel(change.x, change.y, change.before);
    commands_.executeCommand(std::make_unique<TexturePaintCommand>(splatmap, std::move(changes)));
    if (events_) events_->publish({EventType::TexturePainted, 0, "PaintTool", {}});
}
void PaintTool::vertexPaint(TextureSplatmap& splatmap, glm::vec2 center, float radius, glm::vec3 color, float alpha)
{
    const std::vector<TextureSplatmap::VertexColor> before = splatmap.getVertexColors();
    for (std::uint32_t y = 0; y < splatmap.getHeight(); ++y)
        for (std::uint32_t x = 0; x < splatmap.getWidth(); ++x)
        {
            if (glm::length(glm::vec2(x, y) - center) <= radius)
                splatmap.paintVertexColor(y * splatmap.getWidth() + x, color, alpha);
        }
    std::vector<VertexPaintChange> changes;
    const std::vector<TextureSplatmap::VertexColor>& after = splatmap.getVertexColors();
    for (std::uint32_t i = 0; i < after.size(); ++i)
        if (before[i].alpha != after[i].alpha || before[i].colorTint != after[i].colorTint)
            changes.push_back({i, before[i], after[i]});
    if (changes.empty()) return;
    for (const VertexPaintChange& change : changes) splatmap.paintVertexColor(change.vertexIndex, change.before.colorTint, change.before.alpha);
    commands_.executeCommand(std::make_unique<VertexPaintCommand>(splatmap, std::move(changes)));
    if (events_) events_->publish({EventType::TexturePainted, 0, "PaintTool", {}});
}
} // namespace wowedit
