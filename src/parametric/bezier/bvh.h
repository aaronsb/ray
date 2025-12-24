#pragma once

// BVH acceleration structure for Bezier patches

#include "patch.h"
#include <vector>
#include <algorithm>

namespace parametric {

// GPU-compatible BVH node (32 bytes)
struct alignas(16) BVHNode {
    float minX, minY, minZ;
    uint32_t leftOrFirst;   // Left child index, or first patch index if leaf
    float maxX, maxY, maxZ;
    uint32_t rightOrCount;  // Right child index, or patch count if leaf (high bit = leaf flag)
};

// BVH for patch acceleration
class BVH {
public:
    std::vector<BVHNode> nodes;
    std::vector<uint32_t> patchIndices;

    void build(const std::vector<SubPatch>& patches) {
        if (patches.empty()) return;

        patchIndices.resize(patches.size());
        for (size_t i = 0; i < patches.size(); i++) {
            patchIndices[i] = static_cast<uint32_t>(i);
        }

        nodes.clear();
        nodes.reserve(patches.size() * 2);

        buildRecursive(patches, 0, static_cast<uint32_t>(patches.size()));
    }

private:
    static constexpr uint32_t MAX_LEAF_PATCHES = 4;
    static constexpr float AABB_EPSILON = 1e-4f;

    uint32_t buildRecursive(const std::vector<SubPatch>& patches,
                            uint32_t start, uint32_t count) {
        uint32_t nodeIdx = static_cast<uint32_t>(nodes.size());
        nodes.push_back({});
        BVHNode& node = nodes[nodeIdx];

        // Compute bounds
        AABB bounds = computeBounds(patches, start, count);
        node.minX = bounds.min.x - AABB_EPSILON;
        node.minY = bounds.min.y - AABB_EPSILON;
        node.minZ = bounds.min.z - AABB_EPSILON;
        node.maxX = bounds.max.x + AABB_EPSILON;
        node.maxY = bounds.max.y + AABB_EPSILON;
        node.maxZ = bounds.max.z + AABB_EPSILON;

        if (count <= MAX_LEAF_PATCHES) {
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
        std::sort(patchIndices.begin() + start,
                  patchIndices.begin() + start + count,
                  [&](uint32_t a, uint32_t b) {
                      const AABB& ba = patches[a].bounds;
                      const AABB& bb = patches[b].bounds;
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
        uint32_t leftChild = buildRecursive(patches, start, mid);
        uint32_t rightChild = buildRecursive(patches, start + mid, count - mid);

        // Update node (may have been reallocated)
        nodes[nodeIdx].leftOrFirst = leftChild;
        nodes[nodeIdx].rightOrCount = rightChild;

        return nodeIdx;
    }

    AABB computeBounds(const std::vector<SubPatch>& patches,
                       uint32_t start, uint32_t count) {
        AABB bounds;
        bounds.min = Vec3(1e30f, 1e30f, 1e30f);
        bounds.max = Vec3(-1e30f, -1e30f, -1e30f);

        for (uint32_t i = 0; i < count; i++) {
            const AABB& pb = patches[patchIndices[start + i]].bounds;
            bounds.min.x = std::min(bounds.min.x, pb.min.x);
            bounds.min.y = std::min(bounds.min.y, pb.min.y);
            bounds.min.z = std::min(bounds.min.z, pb.min.z);
            bounds.max.x = std::max(bounds.max.x, pb.max.x);
            bounds.max.y = std::max(bounds.max.y, pb.max.y);
            bounds.max.z = std::max(bounds.max.z, pb.max.z);
        }

        return bounds;
    }
};

} // namespace parametric
