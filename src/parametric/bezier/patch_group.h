#pragma once

// BezierPatchGroup - a collection of Bezier patches ready for GPU ray tracing
// This is the main abstraction for using Bezier surfaces in any renderer.

#include "patch.h"
#include "subdivision.h"
#include "bvh.h"
#include "instance.h"
#include <vector>
#include <cstdio>

namespace parametric {

// A group of Bezier patches with BVH acceleration
// Create once, instance many times with different transforms
class BezierPatchGroup {
public:
    BezierPatchGroup() = default;

    // Build from raw patches (subdivides and builds BVH)
    void build(const std::vector<Patch>& patches,
               int maxDepth = 4,
               float flatnessThreshold = 0.05f) {
        m_subPatches = subdividePatches(patches, maxDepth, flatnessThreshold);
        m_bvh.build(m_subPatches);

        printf("BezierPatchGroup: %zu patches -> %zu sub-patches, %zu BVH nodes\n",
               patches.size(), m_subPatches.size(), m_bvh.nodes.size());
    }

    // Accessors for GPU upload
    const std::vector<SubPatch>& subPatches() const { return m_subPatches; }
    const std::vector<BVHNode>& bvhNodes() const { return m_bvh.nodes; }
    const std::vector<uint32_t>& patchIndices() const { return m_bvh.patchIndices; }

    // Counts
    uint32_t subPatchCount() const { return static_cast<uint32_t>(m_subPatches.size()); }
    uint32_t bvhNodeCount() const { return static_cast<uint32_t>(m_bvh.nodes.size()); }

    // GPU buffer sizes
    size_t patchDataSize() const { return m_subPatches.size() * 16 * sizeof(float) * 4; }
    size_t bvhDataSize() const { return m_bvh.nodes.size() * sizeof(BVHNode); }
    size_t indexDataSize() const { return m_bvh.patchIndices.size() * sizeof(uint32_t); }

    // Pack patch data for GPU (16 vec4s per patch)
    std::vector<float> packPatchData() const {
        std::vector<float> data(m_subPatches.size() * 16 * 4);
        for (size_t p = 0; p < m_subPatches.size(); p++) {
            for (int i = 0; i < 16; i++) {
                size_t idx = (p * 16 + i) * 4;
                data[idx + 0] = m_subPatches[p].cp[i].x;
                data[idx + 1] = m_subPatches[p].cp[i].y;
                data[idx + 2] = m_subPatches[p].cp[i].z;
                data[idx + 3] = 0.0f;
            }
        }
        return data;
    }

private:
    std::vector<SubPatch> m_subPatches;
    BVH m_bvh;
};

} // namespace parametric
