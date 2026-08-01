#pragma once

// Layer C (data) — pure item validation. Given an assembled Item, reports a list of
// issues the editor surfaces. No ImGui, no SQL: the only outside dependency is
// LookupCache, used purely for id-existence checks and only for categories that are
// actually loaded (every check is guarded by a cache.XxxLoaded()).

#include <vector>

#include "data/Validation.h"
#include "schema/Item.h"

namespace we
{
class LookupCache;

class ItemValidator
{
public:
    // Validate `item`, using `cache` for id-existence checks (guarded per-category).
    // Returned issues are sorted Error > Warning > Info (stable within a level).
    std::vector<ValidationIssue> Validate(const Item& item, const LookupCache& cache) const;
};
} // namespace we
