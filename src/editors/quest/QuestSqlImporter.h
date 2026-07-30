#pragma once

// Parses a .sql dump (in the format this editor emits, and typical TrinityCore
// 3.3.5a quest dumps) into a single Quest aggregate. Pure module: no ImGui, no
// DB, no SQL execution. Columns are matched to fields BY NAME, so column order
// or a subset of columns is fine. See data/QuestRepository.cpp for the emitted
// column lists this importer round-trips.

#include <cstdint>
#include <string>
#include <vector>

#include "schema/Quest.h"

namespace qe
{
// Outcome of an import attempt. `ok` is true only when a quest_template row for
// the chosen quest id was found and assembled. `warnings` collects non-fatal
// issues (unknown columns/tables, orphan POI points, ...).
struct ImportResult
{
    bool ok = false;
    std::string error;
    Quest quest;                       // populated on success
    std::vector<std::string> warnings;
};

class QuestSqlImporter
{
public:
    // Parse SQL text (one quest's worth of statements). Assigns columns to
    // fields by column name. If multiple quest ids are present, imports the one
    // matching `onlyQuestId` (0 = the first quest_template ID seen). Never
    // throws; failures are reported via ImportResult::ok / ::error.
    ImportResult ImportFromSql(const std::string& sqlText, uint32_t onlyQuestId = 0) const;
};
} // namespace qe
