#pragma once

// Cone primitive with optional base cap

#include "../types.h"

namespace parametric {

// GPU cone - 48 bytes, 16-byte aligned
// axis points from base to tip
struct alignas(16) Cone {
    Vec3 base;
    Vec3 axis;       // normalized direction from base to tip
    float radius;    // radius at base
    float height;    // distance from base to tip
    uint32_t materialId;
    uint32_t cap;    // 0 = open, 1 = capped base
};

} // namespace parametric
