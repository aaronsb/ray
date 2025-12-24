#pragma once

// Cylinder primitive with optional caps

#include "../types.h"

namespace parametric {

// GPU cylinder - 48 bytes, 16-byte aligned
// axis should be normalized
struct alignas(16) Cylinder {
    Vec3 base;
    Vec3 axis;       // normalized direction from base
    float radius;
    float height;
    uint32_t materialId;
    uint32_t caps;   // 0 = open, 1 = capped
};

} // namespace parametric
