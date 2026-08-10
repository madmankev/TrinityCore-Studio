#pragma once
#include "terrain/terrain_chunk.h"
#include <cstdint>
#include <string>
#include <vector>
namespace wowedit
{
struct HierarchyNode { std::string name; std::uint64_t id = 0; std::vector<HierarchyNode> children; };
class HierarchyPanel { public: std::vector<HierarchyNode> build(const TerrainChunk& chunk) const; };
} // namespace wowedit
