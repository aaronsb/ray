#pragma once

// BVH acceleration structure for CSG scene roots

#include "csg.h"
#include <vector>
#include <algorithm>

namespace parametric {

// GPU-compatible BVH node for CSG (32 bytes, same as Bezier BVH)
struct alignas(16) CSGBVHNode {
    float minX, minY, minZ;
    uint32_t leftOrFirst;   // Left child index, or first root index if leaf
    float maxX, maxY, maxZ;
    uint32_t rightOrCount;  // Right child index, or root count if leaf (high bit = leaf flag)
};

// BVH over CSG scene roots
class CSGBVH {
public:
    std::vector<CSGBVHNode> nodes;
    std::vector<uint32_t> rootIndices;  // Reordered root indices

    void build(const CSGScene& scene) {
        auto aabbs = scene.computeRootAABBs();
        const auto& roots = scene.roots();

        if (roots.empty()) return;

        rootIndices.resize(roots.size());
        for (size_t i = 0; i < roots.size(); i++) {
            rootIndices[i] = static_cast<uint32_t>(i);
        }

        nodes.clear();
        nodes.reserve(roots.size() * 2);

        buildRecursive(aabbs, roots, 0, static_cast<uint32_t>(roots.size()));
    }

    bool empty() const { return nodes.empty(); }
    size_t nodeCount() const { return nodes.size(); }

private:
    static constexpr uint32_t MAX_LEAF_ROOTS = 2;
    static constexpr float AABB_EPSILON = 1e-4f;

    uint32_t buildRecursive(const std::vector<AABB>& aabbs,
                            const std::vector<uint32_t>& roots,
                            uint32_t start, uint32_t count) {
        uint32_t nodeIdx = static_cast<uint32_t>(nodes.size());
        nodes.push_back({});
        CSGBVHNode& node = nodes[nodeIdx];

        // Compute bounds
        AABB bounds = computeBounds(aabbs, start, count);
        node.minX = bounds.min.x - AABB_EPSILON;
        node.minY = bounds.min.y - AABB_EPSILON;
        node.minZ = bounds.min.z - AABB_EPSILON;
        node.maxX = bounds.max.x + AABB_EPSILON;
        node.maxY = bounds.max.y + AABB_EPSILON;
        node.maxZ = bounds.max.z + AABB_EPSILON;

        if (count <= MAX_LEAF_ROOTS) {
            // Leaf node
            node.leftOrFirst = start;
            node.rightOrCount = count | 0x80000000u;  // High bit = leaf flag
            return nodeIdx;
        }

        // Find split axis (longest)
        float extentX = bounds.max.x - bounds.min.x;
        float extentY = bounds.max.y - bounds.min.y;
        float extentZ = bounds.max.z - bounds.min.z;

        int axis = 0;
        if (extentY > extentX && extentY > extentZ) axis = 1;
        else if (extentZ > extentX) axis = 2;

        // Sort by centroid on split axis
        std::sort(rootIndices.begin() + start,
                  rootIndices.begin() + start + count,
                  [&](uint32_t a, uint32_t b) {
                      const AABB& ba = aabbs[a];
                      const AABB& bb = aabbs[b];
                      float ca = (axis == 0) ? (ba.min.x + ba.max.x) :
                                 (axis == 1) ? (ba.min.y + ba.max.y) :
                                               (ba.min.z + ba.max.z);
                      float cb = (axis == 0) ? (bb.min.x + bb.max.x) :
                                 (axis == 1) ? (bb.min.y + bb.max.y) :
                                               (bb.min.z + bb.max.z);
                      return ca < cb;
                  });

        // Split at median
        uint32_t mid = count / 2;

        // Build children
        uint32_t leftChild = buildRecursive(aabbs, roots, start, mid);
        uint32_t rightChild = buildRecursive(aabbs, roots, start + mid, count - mid);

        // Update node (may have been reallocated)
        nodes[nodeIdx].leftOrFirst = leftChild;
        nodes[nodeIdx].rightOrCount = rightChild;

        return nodeIdx;
    }

    AABB computeBounds(const std::vector<AABB>& aabbs, uint32_t start, uint32_t count) {
        AABB bounds;
        for (uint32_t i = 0; i < count; i++) {
            bounds.expand(aabbs[rootIndices[start + i]]);
        }
        return bounds;
    }
};

} // namespace parametric
