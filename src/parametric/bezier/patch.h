#pragma once

// Bicubic Bezier patch - 4x4 control points

#include "../types.h"
#include <array>

namespace parametric {

// A single bicubic Bezier patch: 16 control points in row-major order
struct Patch {
    std::array<Vec3, 16> cp;  // cp[row * 4 + col]

    Vec3& at(int row, int col) { return cp[row * 4 + col]; }
    const Vec3& at(int row, int col) const { return cp[row * 4 + col]; }
};

// Axis-aligned bounding box
struct AABB {
    Vec3 min, max;

    float diagonal() const { return (max - min).length(); }

    static AABB fromPatch(const Patch& p, float padding = 0.01f) {
        AABB box;
        box.min = box.max = p.cp[0];
        for (int i = 1; i < 16; i++) {
            box.min.x = std::min(box.min.x, p.cp[i].x);
            box.min.y = std::min(box.min.y, p.cp[i].y);
            box.min.z = std::min(box.min.z, p.cp[i].z);
            box.max.x = std::max(box.max.x, p.cp[i].x);
            box.max.y = std::max(box.max.y, p.cp[i].y);
            box.max.z = std::max(box.max.z, p.cp[i].z);
        }
        // Pad to prevent gaps between adjacent patches
        box.min.x -= padding;
        box.min.y -= padding;
        box.min.z -= padding;
        box.max.x += padding;
        box.max.y += padding;
        box.max.z += padding;
        return box;
    }
};

// GPU-ready sub-patch with precomputed AABB
struct SubPatch {
    std::array<Vec3, 16> cp;
    AABB bounds;
};

} // namespace parametric
