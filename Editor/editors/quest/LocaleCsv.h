#pragma once

// Export/import a quest's localized strings as CSV (columns: locale,field,value) so
// translators can work outside the editor. Round-trips the QuestLocale fields.

#include <string>

namespace we
{
struct Quest;

// Serialize all non-empty locale strings of `q` to CSV text (with a header row).
std::string ExportLocalesCsv(const Quest& q);

// Parse CSV text into `q.locales`, setting per-source present flags and
// q.localesDirty. Returns false with `error` set on a malformed file.
bool ImportLocalesCsv(const std::string& csv, Quest& q, std::string& error);
} // namespace we
