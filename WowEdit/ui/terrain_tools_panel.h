#pragma once
#include "terrain/terrain_editor.h"
namespace wowedit
{
class TerrainToolsPanel { public: explicit TerrainToolsPanel(TerrainEditor& editor) : editor_(editor) {} TerrainBrushSettings& settings() { return editor_.settings(); } private: TerrainEditor& editor_; };
} // namespace wowedit
