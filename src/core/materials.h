#pragma once

#include "types.h"

// Material types matching shader
enum class MaterialType : uint32_t {
    Diffuse = 0,
    Metal = 1,
    Dielectric = 2,
    Emissive = 3,
    RoughDielectric = 4,  // Frosted glass
    AnisotropicMetal = 5, // Brushed steel
    Dichroic = 6,         // Thin-film interference
    Marble = 7,           // Turbulence-based veins
    Wood = 8,             // Concentric rings with grain
    Swirl = 9,            // FBM noise pattern
    Checker = 10          // Checkerboard pattern
};

// GPU material - 48 bytes, 16-byte aligned
struct alignas(16) Material {
    Vec3 albedo;           // 16 bytes (padded)
    Vec3 emission;         // 16 bytes (padded) - also used as secondary color for dichroic
    uint32_t type;         // MaterialType
    float param;           // roughness (metal) or IOR (dielectric/rough dielectric)
    float param2;          // roughness (rough dielectric), anisotropy (brushed metal)
    float _pad;            // pad to 16-byte boundary
};
