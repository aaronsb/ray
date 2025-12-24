#pragma once

// Simple BVH for Bezier patch acceleration
// CPU build, GPU traversal

#include <vector>
#include <algorithm>
#include <cmath>
#include "bezier_subdiv.h"

namespace bvh {

// GPU-friendly BVH node (64 bytes, good alignment)
struct alignas(16) BVHNode {
    float minX, minY, minZ;
    uint32_t leftOrFirst;   // Left child index, or first patch index if leaf
    float maxX, maxY, maxZ;
    uint32_t rightOrCount;  // Right child index, or patch count if leaf (high bit = leaf flag)

    bool isLeaf() const { return rightOrCount & 0x80000000u; }
    uint32_t patchCount() const { return rightOrCount & 0x7FFFFFFFu; }
};

// Build context
struct BuildContext {
    std::vector<bezier::SubPatch>& patches;
    std::vector<uint32_t> patchIndices;  // Sorted indices into patches
    std::vector<BVHNode> nodes;

    BuildContext(std::vector<bezier::SubPatch>& p) : patches(p) {
        patchIndices.resize(p.size());
        for (size_t i = 0; i < p.size(); i++) {
            patchIndices[i] = static_cast<uint32_t>(i);
        }
        nodes.reserve(2 * p.size());  // Upper bound for binary tree
    }
};

inline bezier::AABB computeBounds(const BuildContext& ctx, uint32_t start, uint32_t count) {
    bezier::AABB bounds;
    bounds.min = bezier::Vec3(1e30f, 1e30f, 1e30f);
    bounds.max = bezier::Vec3(-1e30f, -1e30f, -1e30f);

    for (uint32_t i = start; i < start + count; i++) {
        const auto& patch = ctx.patches[ctx.patchIndices[i]];
        bounds.min = bezier::Vec3::min(bounds.min, patch.bounds.min);
        bounds.max = bezier::Vec3::max(bounds.max, patch.bounds.max);
    }
    return bounds;
}

inline bezier::Vec3 centroid(const bezier::AABB& box) {
    return bezier::Vec3(
        (box.min.x + box.max.x) * 0.5f,
        (box.min.y + box.max.y) * 0.5f,
        (box.min.z + box.max.z) * 0.5f
    );
}

// Recursive BVH build with median split
inline uint32_t buildRecursive(BuildContext& ctx, uint32_t start, uint32_t count, int depth) {
    uint32_t nodeIdx = static_cast<uint32_t>(ctx.nodes.size());
    ctx.nodes.push_back(BVHNode{});

    bezier::AABB bounds = computeBounds(ctx, start, count);
    BVHNode& node = ctx.nodes[nodeIdx];
    node.minX = bounds.min.x;
    node.minY = bounds.min.y;
    node.minZ = bounds.min.z;
    node.maxX = bounds.max.x;
    node.maxY = bounds.max.y;
    node.maxZ = bounds.max.z;

    // Leaf if few patches or max depth
    const uint32_t MAX_LEAF_SIZE = 4;
    const int MAX_DEPTH = 20;

    if (count <= MAX_LEAF_SIZE || depth >= MAX_DEPTH) {
        node.leftOrFirst = start;
        node.rightOrCount = count | 0x80000000u;  // Set leaf flag
        return nodeIdx;
    }

    // Find split axis (longest extent)
    float extentX = bounds.max.x - bounds.min.x;
    float extentY = bounds.max.y - bounds.min.y;
    float extentZ = bounds.max.z - bounds.min.z;

    int axis = 0;
    if (extentY > extentX && extentY > extentZ) axis = 1;
    else if (extentZ > extentX && extentZ > extentY) axis = 2;

    // Sort by centroid on split axis
    auto getCentroidAxis = [&ctx, axis](uint32_t idx) -> float {
        const auto& b = ctx.patches[idx].bounds;
        if (axis == 0) return (b.min.x + b.max.x) * 0.5f;
        if (axis == 1) return (b.min.y + b.max.y) * 0.5f;
        return (b.min.z + b.max.z) * 0.5f;
    };

    std::sort(ctx.patchIndices.begin() + start,
              ctx.patchIndices.begin() + start + count,
              [&](uint32_t a, uint32_t b) {
                  return getCentroidAxis(a) < getCentroidAxis(b);
              });

    // Split at median
    uint32_t mid = count / 2;

    // Build children
    uint32_t leftChild = buildRecursive(ctx, start, mid, depth + 1);
    uint32_t rightChild = buildRecursive(ctx, start + mid, count - mid, depth + 1);

    // Update node (may have been reallocated!)
    ctx.nodes[nodeIdx].leftOrFirst = leftChild;
    ctx.nodes[nodeIdx].rightOrCount = rightChild;

    return nodeIdx;
}

struct BVH {
    std::vector<BVHNode> nodes;
    std::vector<uint32_t> patchIndices;  // Reordered patch indices

    void build(std::vector<bezier::SubPatch>& patches) {
        if (patches.empty()) return;

        BuildContext ctx(patches);
        buildRecursive(ctx, 0, static_cast<uint32_t>(patches.size()), 0);

        nodes = std::move(ctx.nodes);
        patchIndices = std::move(ctx.patchIndices);

        printf("BVH built: %zu nodes for %zu patches\n", nodes.size(), patches.size());
    }
};

} // namespace bvh
