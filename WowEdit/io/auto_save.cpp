#include "io/auto_save.h"

#include <algorithm>

namespace wowedit
{
void AutoSaveManager::setIntervalSeconds(float seconds)
{
    intervalSeconds_ = std::max(5.0f, seconds);
}

bool AutoSaveManager::update(float deltaSeconds, const WorldProject& project, const std::string& recoveryPath,
                             std::string& error)
{
    if (!enabled_ || !dirty_ || recoveryPath.empty())
        return false;
    elapsedSeconds_ += std::max(0.0f, deltaSeconds);
    if (elapsedSeconds_ < intervalSeconds_)
        return false;
    elapsedSeconds_ = 0.0f;
    if (!serializer_.save(project, recoveryPath, error))
        return false;
    return true;
}
} // namespace wowedit
