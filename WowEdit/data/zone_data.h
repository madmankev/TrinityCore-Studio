#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wowedit
{
struct ZoneData
{
    std::uint32_t id = 0;
    std::string name;
    std::string biome;
    std::vector<std::uint32_t> mapIds;
    std::vector<std::string> tags;
};
} // namespace wowedit
