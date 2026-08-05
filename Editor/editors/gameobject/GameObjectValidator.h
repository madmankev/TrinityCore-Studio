#pragma once

// Layer C (data) — pure gameobject validation. No ImGui/SQL; only LookupCache for
// guarded id-existence checks.

#include <vector>

#include "data/Validation.h"
#include "schema/GameObject.h"

namespace we
{
class LookupCache;

class GameObjectValidator
{
public:
    std::vector<ValidationIssue> Validate(const GameObject& go, const LookupCache& cache) const;
};
} // namespace we
