#pragma once

#include "core/types.h"
#include "utils/math_utils.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace wowedit
{
/**
 * Loose octree used as a broad-phase acceleration structure. Objects spanning a
 * child boundary remain in their parent node, preventing duplicate ray hits.
 */
template <typename TValue>
class Octree
{
public:
    explicit Octree(AABB bounds = AABB{glm::vec3(-32768.0f), glm::vec3(32768.0f)}, int maxDepth = 8, std::size_t bucketSize = 16)
        : root_(std::make_unique<Node>(bounds, 0)), maxDepth_(maxDepth), bucketSize_(bucketSize)
    {
    }

    void clear()
    {
        const AABB bounds = root_->bounds;
        root_ = std::make_unique<Node>(bounds, 0);
    }

    void resetBounds(const AABB& bounds)
    {
        root_ = std::make_unique<Node>(bounds, 0);
    }

    void insert(const AABB& bounds, TValue value)
    {
        if (root_)
            insert(*root_, bounds, value);
    }

    std::vector<TValue> queryRay(const Ray& ray) const
    {
        std::vector<TValue> values;
        if (root_)
            queryRay(*root_, ray, values);
        return values;
    }

private:
    struct Entry
    {
        AABB bounds;
        TValue value;
    };

    struct Node
    {
        explicit Node(AABB value, int nodeDepth) : bounds(value), depth(nodeDepth) {}
        AABB bounds;
        int depth = 0;
        std::vector<Entry> entries;
        std::array<std::unique_ptr<Node>, 8> children{};

        bool leaf() const { return !children[0]; }
    };

    static bool contains(const AABB& parent, const AABB& child)
    {
        return child.min.x >= parent.min.x && child.min.y >= parent.min.y && child.min.z >= parent.min.z &&
               child.max.x <= parent.max.x && child.max.y <= parent.max.y && child.max.z <= parent.max.z;
    }

    AABB childBounds(const Node& node, int index) const
    {
        const glm::vec3 center = node.bounds.center();
        AABB result;
        result.min = {
            (index & 1) ? center.x : node.bounds.min.x,
            (index & 2) ? center.y : node.bounds.min.y,
            (index & 4) ? center.z : node.bounds.min.z};
        result.max = {
            (index & 1) ? node.bounds.max.x : center.x,
            (index & 2) ? node.bounds.max.y : center.y,
            (index & 4) ? node.bounds.max.z : center.z};
        return result;
    }

    int fittingChild(const Node& node, const AABB& bounds) const
    {
        for (int i = 0; i < 8; ++i)
            if (contains(childBounds(node, i), bounds))
                return i;
        return -1;
    }

    void split(Node& node)
    {
        if (!node.leaf())
            return;
        for (int i = 0; i < 8; ++i)
            node.children[i] = std::make_unique<Node>(childBounds(node, i), node.depth + 1);
        std::vector<Entry> retained;
        retained.reserve(node.entries.size());
        for (const Entry& entry : node.entries)
        {
            const int child = fittingChild(node, entry.bounds);
            if (child < 0)
                retained.push_back(entry);
            else
                insert(*node.children[child], entry.bounds, entry.value);
        }
        node.entries = std::move(retained);
    }

    void insert(Node& node, const AABB& bounds, TValue value)
    {
        if (!contains(node.bounds, bounds))
        {
            // Root-sized overflow objects are still retained and remain pickable.
            node.entries.push_back({bounds, value});
            return;
        }
        if (!node.leaf())
        {
            const int child = fittingChild(node, bounds);
            if (child >= 0)
            {
                insert(*node.children[child], bounds, value);
                return;
            }
        }
        node.entries.push_back({bounds, value});
        if (node.leaf() && node.depth < maxDepth_ && node.entries.size() > bucketSize_)
            split(node);
    }

    void queryRay(const Node& node, const Ray& ray, std::vector<TValue>& output) const
    {
        float nodeDistance = 0.0f;
        if (!math::IntersectRayAabb(ray, node.bounds, nodeDistance))
            return;
        for (const Entry& entry : node.entries)
        {
            float distance = 0.0f;
            if (math::IntersectRayAabb(ray, entry.bounds, distance))
                output.push_back(entry.value);
        }
        if (!node.leaf())
            for (const std::unique_ptr<Node>& child : node.children)
                queryRay(*child, ray, output);
    }

    std::unique_ptr<Node> root_;
    int maxDepth_ = 8;
    std::size_t bucketSize_ = 16;
};
} // namespace wowedit
