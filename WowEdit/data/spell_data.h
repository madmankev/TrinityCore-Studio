#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wowedit
{
enum class SpellTargetType : std::uint8_t { Unit, Ground, Area, Self };

struct SpellEffectData
{
    std::string particlePath;
    std::string soundPath;
    float startSeconds = 0.0f;
    float durationSeconds = 0.0f;
    std::string phase; // Cast, Impact, Trail
};

struct SpellData
{
    std::uint32_t id = 0;
    std::string name;
    float rangeYards = 35.0f;
    float castTimeSeconds = 0.0f;
    float cooldownSeconds = 0.0f;
    std::uint32_t manaCost = 0;
    SpellTargetType targetType = SpellTargetType::Unit;
    std::vector<SpellEffectData> effects;
};
} // namespace wowedit
