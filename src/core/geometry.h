#pragma once

#include "types.h"
#include "../parametric/sphere/sphere.h"

// Re-export Sphere from parametric library
using parametric::Sphere;

// Geometry type enum for dispatch
enum class GeometryType : uint32_t {
    Sphere = 0,
    Box = 1,
    Cylinder = 2,
    Cone = 3,
    Torus = 4
};

// GPU box (axis-aligned) - 48 bytes, 16-byte aligned
struct alignas(16) Box {
    Vec3 center;
    Vec3 halfExtents;
    uint32_t materialId;
    float _pad[3];
};

// GPU cylinder - 48 bytes, 16-byte aligned
struct alignas(16) Cylinder {
    Vec3 base;
    Vec3 axis;
    float radius;
    float height;
    uint32_t materialId;
    uint32_t caps;
};

// GPU cone - 48 bytes, 16-byte aligned
struct alignas(16) Cone {
    Vec3 base;
    Vec3 axis;
    float radius;
    float height;
    uint32_t materialId;
    uint32_t cap;
};

// GPU torus - 48 bytes, 16-byte aligned
struct alignas(16) Torus {
    Vec3 center;
    Vec3 axis;
    float majorRadius;
    float minorRadius;
    uint32_t materialId;
    uint32_t _pad;
};
