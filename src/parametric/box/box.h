#pragma once

// Axis-aligned box primitive

#include "../types.h"

namespace parametric {

// GPU box (axis-aligned) - 48 bytes, 16-byte aligned
// Implicit: |p - center| <= halfExtents (component-wise)
struct alignas(16) Box {
    Vec3 center;
    Vec3 halfExtents;
    uint32_t materialId;
    float _pad[3];
};

} // namespace parametric
