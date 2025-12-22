#pragma once

#include "types.h"

// Gobo pattern types for spotlights
enum class GoboType : uint32_t {
    None = 0,       // No pattern, solid light
    Stripes = 1,    // Vertical stripes
    Grid = 2,       // Grid/window pattern
    Circles = 3,    // Concentric circles
    Dots = 4,       // Dot array
    Star = 5,       // Star/burst pattern
    Off = 6         // Light completely off
};

// GPU spotlight - 80 bytes, 16-byte aligned
struct alignas(16) SpotLight {
    Vec3 position;         // 16 bytes
    Vec3 direction;        // 16 bytes (normalized)
    Vec3 color;            // 16 bytes (emission color/intensity)
    float innerAngle;      // radians - full intensity cone
    float outerAngle;      // radians - falloff to zero
    uint32_t goboType;     // GoboType enum
    float goboScale;       // pattern scale (larger = more repetitions)
    float goboRotation;    // pattern rotation in radians
    float _pad[3];         // pad to 16 bytes
};
