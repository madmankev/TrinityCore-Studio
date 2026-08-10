#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <vector>
namespace wowedit
{
enum class LogLevel { Trace, Info, Warning, Error };
struct LogEntry { LogLevel level = LogLevel::Info; std::string category; std::string message; };
class Logger
{
public:
    using Sink = std::function<void(const LogEntry&)>;
    static Logger& instance();
    void addSink(Sink sink);
    void log(LogLevel level, std::string category, std::string message);
    std::vector<LogEntry> recent() const;
    void clear();
private:
    mutable std::mutex mutex_; std::vector<LogEntry> entries_; std::vector<Sink> sinks_;
};
} // namespace wowedit
