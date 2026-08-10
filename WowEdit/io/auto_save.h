#pragma once

#include "io/map_serializer.h"

#include <string>

namespace wowedit
{
/** Configurable, atomic recovery writer. Hosts call update() from the main loop;
 * expensive serialization can be moved to a worker after taking a document snapshot. */
class AutoSaveManager
{
public:
    void setIntervalSeconds(float seconds);
    float intervalSeconds() const { return intervalSeconds_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    bool update(float deltaSeconds, const WorldProject& project, const std::string& recoveryPath, std::string& error);
    void markDirty() { dirty_ = true; }
    void markSaved() { dirty_ = false; elapsedSeconds_ = 0.0f; }
    bool dirty() const { return dirty_; }
private:
    MapSerializer serializer_;
    float intervalSeconds_ = 300.0f;
    float elapsedSeconds_ = 0.0f;
    bool enabled_ = true;
    bool dirty_ = false;
};
} // namespace wowedit
