// Layer D (util): tiny leveled logger.
//
// Writes each message to stderr with a level prefix AND appends it to an in-process
// ring buffer so an in-app console panel (Layer E) can render the recent log later.
// Header + cpp only, no dependencies beyond the standard library.
#pragma once

#include <deque>
#include <string>

namespace qe
{
    enum class Level
    {
        Trace,
        Info,
        Warn,
        Error
    };

    // Small facade around the process-wide log state. All methods are static; there is a
    // single shared buffer for the whole process (guarded by an internal mutex).
    struct Log
    {
        // Core sink: emit an already-formatted message at the given level.
        static void Write(Level level, const std::string& message);

        // printf-style entry point (used by the free LogXxx helpers below).
        static void Writef(Level level, const char* fmt, ...);

        // Recent messages, oldest first, each already prefixed with its level tag.
        // Capacity is bounded (see kBufferCap in the cpp); oldest entries are dropped.
        static const std::deque<std::string>& Buffer();

        // Drop everything currently in the ring buffer.
        static void Clear();

        // Messages below this level are neither printed nor buffered. Default: Trace.
        static void SetMinLevel(Level level);
        static Level MinLevel();

        // Human-readable tag for a level, e.g. "INFO". Never null.
        static const char* LevelName(Level level);
    };

    // Convenience free functions. Each has a printf-style overload and a plain-string
    // overload (the string overload avoids format-string pitfalls when the text is
    // already assembled and may contain stray '%').
    void LogTrace(const char* fmt, ...);
    void LogInfo(const char* fmt, ...);
    void LogWarn(const char* fmt, ...);
    void LogError(const char* fmt, ...);

    void LogTrace(const std::string& message);
    void LogInfo(const std::string& message);
    void LogWarn(const std::string& message);
    void LogError(const std::string& message);
}
