#pragma once

// Bezier patch instance for GPU instancing

#include "../types.h"

namespace parametric {

// Instance data for bezier patch objects
// Supports position + uniform scale + XYZ Euler rotation
struct alignas(16) BezierInstance {
    float posX, posY, posZ;
    float scale;
    float rotX, rotY, rotZ;  // Euler angles (radians), applied in XYZ order
    uint32_t materialId;
};

} // namespace parametric
