#pragma once
#include "utils/logger.h"
#include "utils/profiler.h"
namespace wowedit
{
class DebugPanel { public: DebugPanel(Logger& logger, Profiler& profiler) : logger_(logger), profiler_(profiler) {} std::vector<LogEntry> logs() const { return logger_.recent(); } std::map<std::string,double> timings() const { return profiler_.lastMilliseconds(); } private: Logger& logger_; Profiler& profiler_; };
} // namespace wowedit
