#pragma once
#include <chrono>
#include <map>
#include <mutex>
#include <string>
namespace wowedit
{
class Profiler
{
public:
    void record(const std::string& name, double milliseconds);
    std::map<std::string, double> lastMilliseconds() const;
private:
    mutable std::mutex mutex_; std::map<std::string, double> values_;
};
class ProfileScope
{
public:
    ProfileScope(Profiler& profiler, std::string name) : profiler_(profiler), name_(std::move(name)), start_(std::chrono::steady_clock::now()) {}
    ~ProfileScope();
private:
    Profiler& profiler_; std::string name_; std::chrono::steady_clock::time_point start_;
};
} // namespace wowedit
