#pragma once

// De Casteljau subdivision for bicubic Bezier patches

#include "patch.h"
#include <vector>

namespace parametric {

// De Casteljau algorithm for cubic curve at t=0.5
// Returns left and right halves' control points
inline void subdivideCubic(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                           Vec3 left[4], Vec3 right[4]) {
    // Level 1
    Vec3 q0 = (p0 + p1) * 0.5f;
    Vec3 q1 = (p1 + p2) * 0.5f;
    Vec3 q2 = (p2 + p3) * 0.5f;

    // Level 2
    Vec3 r0 = (q0 + q1) * 0.5f;
    Vec3 r1 = (q1 + q2) * 0.5f;

    // Level 3 (midpoint)
    Vec3 s = (r0 + r1) * 0.5f;

    left[0] = p0; left[1] = q0; left[2] = r0; left[3] = s;
    right[0] = s; right[1] = r1; right[2] = q2; right[3] = p3;
}

// Subdivide patch in U direction
inline void subdividePatchU(const Patch& p, Patch& left, Patch& right) {
    for (int row = 0; row < 4; row++) {
        Vec3 leftCurve[4], rightCurve[4];
        subdivideCubic(p.at(row, 0), p.at(row, 1), p.at(row, 2), p.at(row, 3),
                       leftCurve, rightCurve);
        for (int col = 0; col < 4; col++) {
            left.at(row, col) = leftCurve[col];
            right.at(row, col) = rightCurve[col];
        }
    }
}

// Subdivide patch in V direction
inline void subdividePatchV(const Patch& p, Patch& bottom, Patch& top) {
    for (int col = 0; col < 4; col++) {
        Vec3 bottomCurve[4], topCurve[4];
        subdivideCubic(p.at(0, col), p.at(1, col), p.at(2, col), p.at(3, col),
                       bottomCurve, topCurve);
        for (int row = 0; row < 4; row++) {
            bottom.at(row, col) = bottomCurve[row];
            top.at(row, col) = topCurve[row];
        }
    }
}

// Subdivide patch into 4 quadrants
inline void subdividePatch(const Patch& p, Patch quad[4]) {
    Patch left, right;
    subdividePatchU(p, left, right);
    subdividePatchV(left, quad[0], quad[1]);
    subdividePatchV(right, quad[2], quad[3]);
}

// Recursive subdivision until flat enough or max depth
inline void subdivideRecursive(const Patch& p, int depth, int maxDepth,
                               float flatnessThreshold,
                               std::vector<SubPatch>& result) {
    AABB bounds = AABB::fromPatch(p);

    if (depth >= maxDepth || bounds.diagonal() < flatnessThreshold) {
        SubPatch sp;
        sp.cp = p.cp;
        sp.bounds = bounds;
        result.push_back(sp);
        return;
    }

    Patch quad[4];
    subdividePatch(p, quad);
    for (int i = 0; i < 4; i++) {
        subdivideRecursive(quad[i], depth + 1, maxDepth, flatnessThreshold, result);
    }
}

// Main entry: subdivide all patches
inline std::vector<SubPatch> subdividePatches(const std::vector<Patch>& patches,
                                               int maxDepth = 4,
                                               float flatnessThreshold = 0.05f) {
    std::vector<SubPatch> result;
    result.reserve(patches.size() * (1 << (2 * maxDepth)));

    for (const auto& p : patches) {
        subdivideRecursive(p, 0, maxDepth, flatnessThreshold, result);
    }

    return result;
}

} // namespace parametric
