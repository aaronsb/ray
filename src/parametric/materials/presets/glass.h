#pragma once

// Glass/transparent material presets
// Usage: library.add("glass", glass::clear());

#include "../material.h"

namespace parametric {
namespace glass {

// Standard glass (crown glass)
inline Material clear() {
    return {0.95f, 0.95f, 0.95f, static_cast<uint32_t>(MaterialType::Glass),
            0.0f, 0.0f, 1.5f, 0.0f};
}

// Tinted glass
inline Material blue() {
    return {0.85f, 0.9f, 1.0f, static_cast<uint32_t>(MaterialType::Glass),
            0.0f, 0.0f, 1.5f, 0.0f};
}

inline Material green() {
    return {0.85f, 1.0f, 0.9f, static_cast<uint32_t>(MaterialType::Glass),
            0.0f, 0.0f, 1.5f, 0.0f};
}

inline Material amber() {
    return {1.0f, 0.9f, 0.7f, static_cast<uint32_t>(MaterialType::Glass),
            0.0f, 0.0f, 1.5f, 0.0f};
}

inline Material ruby() {
    return {1.0f, 0.4f, 0.5f, static_cast<uint32_t>(MaterialType::Glass),
            0.0f, 0.0f, 1.5f, 0.0f};
}

// Special optical materials
inline Material diamond() {
    return {1.0f, 1.0f, 1.0f, static_cast<uint32_t>(MaterialType::Glass),
            0.0f, 0.0f, 2.42f, 0.0f};
}

inline Material water() {
    return {0.95f, 0.98f, 1.0f, static_cast<uint32_t>(MaterialType::Glass),
            0.0f, 0.0f, 1.33f, 0.0f};
}

inline Material ice() {
    return {0.9f, 0.95f, 1.0f, static_cast<uint32_t>(MaterialType::Glass),
            0.05f, 0.0f, 1.31f, 0.0f};
}

// Frosted glass (rough surface)
inline Material frosted() {
    return {0.95f, 0.95f, 0.95f, static_cast<uint32_t>(MaterialType::Glass),
            0.3f, 0.0f, 1.5f, 0.0f};
}

} // namespace glass
} // namespace parametric
