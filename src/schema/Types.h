#pragma once

// Shared schema-layer bits for the TrinityCore 3.3.5a quest editor.
// Plain data only; no logic. See docs/SPEC.md §5/§6.

#include <cstdint>

namespace we
{
// Locale codes present in the *_locale tables. enUS is the base row stored in
// the main tables and never appears here. Order matches the client's locale
// index order used throughout TrinityCore.
inline constexpr const char* kLocales[] = {
    "koKR", "frFR", "deDE", "zhCN", "zhTW", "esES", "esMX", "ruRU"
};

inline constexpr int kLocaleCount = 8;

// quest_greeting / quest_greeting_locale Type column: which owner the greeting
// belongs to.
enum class GreetingType : uint8_t
{
    Creature = 0,
    GameObject = 1
};
} // namespace we
