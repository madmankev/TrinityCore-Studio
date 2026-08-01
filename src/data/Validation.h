#pragma once

// Layer C (data) — record-agnostic validation issue model shared by every editor
// module's validator (QuestValidator, ItemValidator, ...) and the shared
// ValidationPanel. No ImGui, no SQL.

#include <string>

namespace we
{
enum class Severity
{
    Error,
    Warning,
    Info
};

struct ValidationIssue
{
    Severity    severity;
    std::string tab;      // editor tab the issue belongs to ("" = none)
    std::string field;    // short field name
    std::string message;  // human-readable
};
} // namespace we
