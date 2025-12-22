#pragma once

#include "types.h"

// Geometry type enum for dispatch
enum class GeometryType : uint32_t {
    Sphere = 0,
    Box = 1,
    Cylinder = 2,
    Cone = 3
    // Future: Torus, VoxelChunk, TriangleMesh (see ADR-004)
};

// GPU sphere - 32 bytes, 16-byte aligned
struct alignas(16) Sphere {
    Vec3 center;           // 16 bytes (padded)
    float radius;
    uint32_t materialId;
    float _pad[2];         // pad to 16 bytes
};

// GPU box (axis-aligned) - 48 bytes, 16-byte aligned
struct alignas(16) Box {
    Vec3 center;           // 16 bytes (padded)
    Vec3 halfExtents;      // 16 bytes (padded) - size/2 in each dimension
    uint32_t materialId;
    float _pad[3];         // pad to 16 bytes
};

// GPU cylinder - 48 bytes, 16-byte aligned
// Defined by base center, axis direction, radius, and height
struct alignas(16) Cylinder {
    Vec3 base;             // 16 bytes (padded) - center of base cap
    Vec3 axis;             // 16 bytes (padded) - normalized direction (base to top)
    float radius;
    float height;
    uint32_t materialId;
    uint32_t caps;         // 0 = no caps, 1 = caps (default)
};

// GPU cone - 48 bytes, 16-byte aligned
// Defined by base center, axis direction, base radius, and height
// Tip is at base + axis * height
struct alignas(16) Cone {
    Vec3 base;             // 16 bytes (padded) - center of base cap
    Vec3 axis;             // 16 bytes (padded) - normalized direction (base to tip)
    float radius;          // radius at base (tip has radius 0)
    float height;
    uint32_t materialId;
    uint32_t cap;          // 0 = no base cap, 1 = cap (default)
};
