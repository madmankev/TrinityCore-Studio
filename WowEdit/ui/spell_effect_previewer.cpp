#include "ui/spell_effect_previewer.h"
#include <algorithm>
#include <cmath>
namespace wowedit
{
void SpellEffectPreviewer::select(const SpellData& spell) { selected_ = spell; stop(); }
void SpellEffectPreviewer::play(bool loop) { if (!selected_) return; playing_ = true; looping_ = loop; }
void SpellEffectPreviewer::pause() { playing_ = false; }
void SpellEffectPreviewer::stop() { playing_ = false; looping_ = false; currentTime_ = 0.0f; }
void SpellEffectPreviewer::scrub(float seconds) { currentTime_ = std::clamp(seconds, 0.0f, duration()); }
void SpellEffectPreviewer::update(float deltaSeconds) { if (!playing_) return; currentTime_ += std::max(0.0f, deltaSeconds); const float length = duration(); if (currentTime_ < length) return; if (looping_ && length > 0.0f) currentTime_ = std::fmod(currentTime_, length); else { currentTime_ = length; playing_ = false; } }
float SpellEffectPreviewer::duration() const { if (!selected_) return 0.0f; float result = selected_->castTimeSeconds; for (const auto& effect : selected_->effects) result = std::max(result, effect.startSeconds + effect.durationSeconds); return std::max(result, 0.1f); }
std::vector<SpellTimelineMarker> SpellEffectPreviewer::timeline() const { std::vector<SpellTimelineMarker> result; if (!selected_) return result; for (const auto& effect : selected_->effects) result.push_back({effect.startSeconds, effect.phase, effect.particlePath, effect.soundPath}); return result; }
} // namespace wowedit
