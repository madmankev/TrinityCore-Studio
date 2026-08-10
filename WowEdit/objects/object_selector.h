#pragma once

#include "core/types.h"

#include <cstdint>
#include <set>

namespace wowedit
{
class DoodadManager;

class ObjectSelector
{
public:
    explicit ObjectSelector(DoodadManager& manager) : manager_(manager) {}

    std::uint64_t pick(const Ray& ray) const;
    std::set<std::uint64_t> pickBox(const AABB& bounds) const;

private:
    DoodadManager& manager_;
};
} // namespace wowedit
