#pragma once

#include "types.h"

// Geometry type enum for dispatch
enum class GeometryType : uint32_t {
    Sphere = 0,
    Box = 1
    // Future: Cylinder, Cone, Torus, VoxelChunk, TriangleMesh (see ADR-004)
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
