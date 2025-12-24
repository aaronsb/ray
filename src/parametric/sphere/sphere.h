#pragma once

// Parametric sphere primitive

#include "../types.h"

namespace parametric {

// GPU sphere - 32 bytes, 16-byte aligned
// Implicit surface: |p - center|² = radius²
struct alignas(16) Sphere {
    Vec3 center;
    float radius;
    uint32_t materialId;
    float _pad[2];
};

} // namespace parametric
