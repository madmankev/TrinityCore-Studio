#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

namespace we
{
    namespace
    {
        // Upper bound on retained messages; oldest are dropped past this.
        constexpr std::size_t kBufferCap = 1000;

        std::mutex& Mutex()
        {
            static std::mutex m;
            return m;
        }

        std::deque<std::string>& BufferStore()
        {
            static std::deque<std::string> store;
            return store;
        }

        Level& MinLevelStore()
        {
            static Level minLevel = Level::Trace;
            return minLevel;
        }

        // Render a printf-style format + va_list into a std::string, growing the buffer
        // once if the first attempt does not fit.
        std::string VFormat(const char* fmt, va_list args)
        {
            if (!fmt)
                return std::string();

            va_list copy;
            va_copy(copy, args);
            int needed = std::vsnprintf(nullptr, 0, fmt, copy);
            va_end(copy);

            if (needed <= 0)
                return std::string();

            std::vector<char> buf(static_cast<std::size_t>(needed) + 1);
            std::vsnprintf(buf.data(), buf.size(), fmt, args);
            return std::string(buf.data(), static_cast<std::size_t>(needed));
        }
    }

    void Log::Write(Level level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        if (static_cast<int>(level) < static_cast<int>(MinLevelStore()))
            return;

        std::string line = "[";
        line += LevelName(level);
        line += "] ";
        line += message;

        // Mirror to stderr (unbuffered enough for a dev tool) and retain in the ring buffer.
        std::fprintf(stderr, "%s\n", line.c_str());

        std::deque<std::string>& store = BufferStore();
        store.push_back(std::move(line));
        while (store.size() > kBufferCap)
            store.pop_front();
    }

    void Log::Writef(Level level, const char* fmt, ...)
    {
        // Cheap early-out before formatting when the level is filtered.
        if (static_cast<int>(level) < static_cast<int>(MinLevel()))
            return;

        va_list args;
        va_start(args, fmt);
        std::string message = VFormat(fmt, args);
        va_end(args);
        Write(level, message);
    }

    const std::deque<std::string>& Log::Buffer()
    {
        // Returning a const ref to the shared store; callers on the UI thread read it.
        return BufferStore();
    }

    void Log::Clear()
    {
        std::lock_guard<std::mutex> lock(Mutex());
        BufferStore().clear();
    }

    void Log::SetMinLevel(Level level)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        MinLevelStore() = level;
    }

    Level Log::MinLevel()
    {
        std::lock_guard<std::mutex> lock(Mutex());
        return MinLevelStore();
    }

    const char* Log::LevelName(Level level)
    {
        switch (level)
        {
            case Level::Trace: return "TRACE";
            case Level::Info:  return "INFO";
            case Level::Warn:  return "WARN";
            case Level::Error: return "ERROR";
        }
        return "?";
    }

    // --- printf-style free helpers ---

    void LogTrace(const char* fmt, ...)
    {
        va_list args; va_start(args, fmt);
        std::string m = VFormat(fmt, args); va_end(args);
        Log::Write(Level::Trace, m);
    }

    void LogInfo(const char* fmt, ...)
    {
        va_list args; va_start(args, fmt);
        std::string m = VFormat(fmt, args); va_end(args);
        Log::Write(Level::Info, m);
    }

    void LogWarn(const char* fmt, ...)
    {
        va_list args; va_start(args, fmt);
        std::string m = VFormat(fmt, args); va_end(args);
        Log::Write(Level::Warn, m);
    }

    void LogError(const char* fmt, ...)
    {
        va_list args; va_start(args, fmt);
        std::string m = VFormat(fmt, args); va_end(args);
        Log::Write(Level::Error, m);
    }

    // --- plain-string free helpers ---

    void LogTrace(const std::string& message) { Log::Write(Level::Trace, message); }
    void LogInfo(const std::string& message)  { Log::Write(Level::Info, message); }
    void LogWarn(const std::string& message)  { Log::Write(Level::Warn, message); }
    void LogError(const std::string& message) { Log::Write(Level::Error, message); }
}
