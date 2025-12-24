#pragma once

// Torus primitive (ring/donut shape)

#include "../types.h"

namespace parametric {

// GPU torus - 48 bytes, 16-byte aligned
// axis points through the hole (perpendicular to ring plane)
struct alignas(16) Torus {
    Vec3 center;
    Vec3 axis;          // normalized, through the hole
    float majorRadius;  // distance from center to tube center
    float minorRadius;  // tube radius
    uint32_t materialId;
    uint32_t _pad;
};

} // namespace parametric
