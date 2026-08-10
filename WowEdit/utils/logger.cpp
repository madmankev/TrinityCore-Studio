#include "utils/logger.h"
#include <utility>
namespace wowedit
{
Logger& Logger::instance() { static Logger logger; return logger; }
void Logger::addSink(Sink sink) { if (!sink) return; std::lock_guard<std::mutex> lock(mutex_); sinks_.push_back(std::move(sink)); }
void Logger::log(LogLevel level, std::string category, std::string message)
{
    LogEntry entry{level, std::move(category), std::move(message)}; std::vector<Sink> sinks;
    { std::lock_guard<std::mutex> lock(mutex_); entries_.push_back(entry); if (entries_.size() > 5000) entries_.erase(entries_.begin(), entries_.begin() + 1000); sinks = sinks_; }
    for (const Sink& sink : sinks) sink(entry);
}
std::vector<LogEntry> Logger::recent() const { std::lock_guard<std::mutex> lock(mutex_); return entries_; }
void Logger::clear() { std::lock_guard<std::mutex> lock(mutex_); entries_.clear(); }
} // namespace wowedit
