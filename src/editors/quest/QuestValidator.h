#pragma once

// Layer C (data) — pure quest validation. Given a fully-assembled Quest, reports
// a list of issues (errors/warnings/info) the editor surfaces to the user. No
// ImGui, no SQL: the only outside dependency is LookupCache, used purely for
// id-existence checks and only for categories that are actually loaded (every
// existence check is guarded by a cache.XxxLoaded()), so an unloaded cache never
// produces false errors. See docs/SPEC.md §5.

#include <vector>

#include "data/Validation.h"
#include "schema/Quest.h"

namespace we
{
class LookupCache;

class QuestValidator
{
public:
    // Validate `q`, using `cache` for id-existence checks (guarded per-category
    // by cache.XxxLoaded(); an unloaded category is simply not checked).
    // Returned issues are sorted Error > Warning > Info (stable within a level).
    std::vector<ValidationIssue> Validate(const Quest& q, const LookupCache& cache) const;
};
} // namespace we
