#pragma once
#include "core/types.h"
#include <cstdint>
#include <set>
namespace wowedit
{
class DoodadManager;
class SelectionTool
{
public:
    explicit SelectionTool(DoodadManager& doodads) : doodads_(doodads) {}
    std::uint64_t click(const Ray& ray, bool additive);
    void marquee(const AABB& bounds, bool additive);
    void clear();
private:
    DoodadManager& doodads_;
};
} // namespace wowedit
