#pragma once
#include "data/spell_data.h"
#include <cstdint>
#include <optional>
#include <vector>
namespace wowedit
{
enum class WeatherPreset : std::uint8_t { Clear, Rain, Snow, Fog, Storm };
struct SpellPreviewSettings { std::uint64_t targetUnit = 0; bool casterAtCamera = true; float timeOfDayMinutes = 720.0f; WeatherPreset weather = WeatherPreset::Clear; };
struct SpellTimelineMarker { float timeSeconds = 0.0f; std::string phase; std::string particlePath; std::string soundPath; };
class SpellEffectPreviewer
{
public:
    void select(const SpellData& spell); const SpellData* selected() const { return selected_ ? &*selected_ : nullptr; }
    void play(bool loop = false); void pause(); void stop(); void scrub(float seconds); void update(float deltaSeconds);
    bool playing() const { return playing_; } bool looping() const { return looping_; } float currentTime() const { return currentTime_; } float duration() const;
    SpellPreviewSettings& settings() { return settings_; } const SpellPreviewSettings& settings() const { return settings_; }
    std::vector<SpellTimelineMarker> timeline() const;
private:
    std::optional<SpellData> selected_; SpellPreviewSettings settings_; float currentTime_ = 0.0f; bool playing_ = false; bool looping_ = false;
};
} // namespace wowedit
