#pragma once

// De Casteljau subdivision for bicubic Bezier patches
// CPU-side preprocessing to create GPU-friendly sub-patches

#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace bezier {

struct Vec3 {
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float length() const { return std::sqrt(x*x + y*y + z*z); }

    static Vec3 min(const Vec3& a, const Vec3& b) {
        return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    }
    static Vec3 max(const Vec3& a, const Vec3& b) {
        return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    }
};

// A bicubic Bezier patch: 4x4 = 16 control points
struct Patch {
    std::array<Vec3, 16> cp;  // Row-major: cp[row*4 + col]

    Vec3& at(int row, int col) { return cp[row * 4 + col]; }
    const Vec3& at(int row, int col) const { return cp[row * 4 + col]; }
};

// Axis-aligned bounding box
struct AABB {
    Vec3 min, max;

    float diagonal() const { return (max - min).length(); }

    static AABB fromPatch(const Patch& p) {
        AABB box;
        box.min = box.max = p.cp[0];
        for (int i = 1; i < 16; i++) {
            box.min = Vec3::min(box.min, p.cp[i]);
            box.max = Vec3::max(box.max, p.cp[i]);
        }
        return box;
    }
};

// GPU-ready sub-patch with precomputed AABB
struct SubPatch {
    std::array<Vec3, 16> cp;
    AABB bounds;
};

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

    // Left half: p0, q0, r0, s
    left[0] = p0;
    left[1] = q0;
    left[2] = r0;
    left[3] = s;

    // Right half: s, r1, q2, p3
    right[0] = s;
    right[1] = r1;
    right[2] = q2;
    right[3] = p3;
}

// Subdivide patch in U direction (splits into left/right)
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

// Subdivide patch in V direction (splits into bottom/top)
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

// Subdivide patch into 4 quadrants (one level)
inline void subdividePatch(const Patch& p, Patch quad[4]) {
    Patch left, right;
    subdividePatchU(p, left, right);

    // Subdivide each half in V
    subdividePatchV(left, quad[0], quad[1]);   // bottom-left, top-left
    subdividePatchV(right, quad[2], quad[3]);  // bottom-right, top-right
}

// Recursively subdivide until patches are "flat enough" or max depth reached
inline void subdivideRecursive(const Patch& p, int depth, int maxDepth,
                               float flatnessThreshold,
                               std::vector<SubPatch>& result) {
    AABB bounds = AABB::fromPatch(p);

    // Stop if max depth reached or patch is flat enough
    if (depth >= maxDepth || bounds.diagonal() < flatnessThreshold) {
        SubPatch sp;
        sp.cp = p.cp;
        sp.bounds = bounds;
        result.push_back(sp);
        return;
    }

    // Subdivide into 4 and recurse
    Patch quad[4];
    subdividePatch(p, quad);
    for (int i = 0; i < 4; i++) {
        subdivideRecursive(quad[i], depth + 1, maxDepth, flatnessThreshold, result);
    }
}

// Main entry point: subdivide all patches
inline std::vector<SubPatch> subdividePatches(const std::vector<Patch>& patches,
                                               int maxDepth = 3,
                                               float flatnessThreshold = 0.1f) {
    std::vector<SubPatch> result;
    result.reserve(patches.size() * (1 << (2 * maxDepth)));  // 4^depth per patch

    for (const auto& p : patches) {
        subdivideRecursive(p, 0, maxDepth, flatnessThreshold, result);
    }

    return result;
}

} // namespace bezier
