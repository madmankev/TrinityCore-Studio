#pragma once

// Layer C (data) — pure creature validation. No ImGui/SQL; only LookupCache for
// guarded id-existence checks.

#include <vector>

#include "data/Validation.h"
#include "schema/Creature.h"

namespace we
{
class LookupCache;

class CreatureValidator
{
public:
    std::vector<ValidationIssue> Validate(const Creature& c, const LookupCache& cache) const;
};
} // namespace we
