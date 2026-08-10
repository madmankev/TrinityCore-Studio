#pragma once
#include "core/command.h"
#include "core/event_system.h"
#include "terrain/texture_splatmap.h"
#include <glm/glm.hpp>
namespace wowedit
{
class PaintTool
{
public:
    explicit PaintTool(CommandManager& commands, EventBus* events = nullptr) : commands_(commands), events_(events) {}
    void paint(TextureSplatmap& splatmap, glm::vec2 center, float radius, std::uint8_t layer, float opacity, float hardness);
    void vertexPaint(TextureSplatmap& splatmap, glm::vec2 center, float radius, glm::vec3 color, float alpha);
private:
    CommandManager& commands_;
    EventBus* events_ = nullptr;
};
} // namespace wowedit
