#pragma once

#include <functional>
#include <string>
#include <vector>

namespace wowedit
{
struct MenuEntry
{
    std::string label;
    std::string actionId;
    std::string shortcut;
    std::vector<MenuEntry> children;
    bool separator = false;
};

struct PanelVisibility
{
    bool contentBrowser = true;
    bool properties = true;
    bool hierarchy = true;
    bool terrainTools = true;
    bool roadPath = false;
    bool texturePalette = true;
    bool doodadBrowser = true;
    bool creatureEditor = true;
    bool questIntegration = true;
    bool spellPreviewer = false;
    bool chunkOperations = false;
    bool debug = false;
    bool outputLog = true;
};

enum class ViewportDisplayMode { Textured, Wireframe, WireframeOnShaded, SolidColor, BoundingBoxes, CollisionMesh };
} // namespace wowedit
