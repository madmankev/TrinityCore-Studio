#include "utils/profiler.h"
namespace wowedit
{
void Profiler::record(const std::string& name, double milliseconds) { std::lock_guard<std::mutex> lock(mutex_); values_[name] = milliseconds; }
std::map<std::string, double> Profiler::lastMilliseconds() const { std::lock_guard<std::mutex> lock(mutex_); return values_; }
ProfileScope::~ProfileScope() { const auto end = std::chrono::steady_clock::now(); profiler_.record(name_, std::chrono::duration<double, std::milli>(end - start_).count()); }
} // namespace wowedit
