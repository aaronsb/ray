#pragma once

// Metal material presets
// Usage: library.add("gold", metals::gold());

#include "../material.h"

namespace parametric {
namespace metals {

// Precious metals
inline Material gold() {
    return {0.83f, 0.69f, 0.22f, static_cast<uint32_t>(MaterialType::Metal),
            0.02f, 1.0f, 1.0f, 0.0f};
}

inline Material silver() {
    return {0.95f, 0.95f, 0.97f, static_cast<uint32_t>(MaterialType::Metal),
            0.02f, 1.0f, 1.0f, 0.0f};
}

inline Material copper() {
    return {0.95f, 0.64f, 0.54f, static_cast<uint32_t>(MaterialType::Metal),
            0.03f, 1.0f, 1.0f, 0.0f};
}

inline Material bronze() {
    return {0.71f, 0.53f, 0.26f, static_cast<uint32_t>(MaterialType::Metal),
            0.15f, 1.0f, 1.0f, 0.0f};
}

// Industrial metals
inline Material chrome() {
    return {0.95f, 0.95f, 0.95f, static_cast<uint32_t>(MaterialType::Metal),
            0.0f, 1.0f, 1.0f, 0.0f};
}

inline Material brushedSteel() {
    return {0.7f, 0.7f, 0.72f, static_cast<uint32_t>(MaterialType::Metal),
            0.25f, 1.0f, 1.0f, 0.0f};
}

inline Material aluminum() {
    return {0.91f, 0.92f, 0.92f, static_cast<uint32_t>(MaterialType::Metal),
            0.1f, 1.0f, 1.0f, 0.0f};
}

// Colored metals (anodized/coated)
inline Material redMetal() {
    return {0.9f, 0.2f, 0.2f, static_cast<uint32_t>(MaterialType::Metal),
            0.05f, 1.0f, 1.0f, 0.0f};
}

inline Material greenMetal() {
    return {0.2f, 0.8f, 0.3f, static_cast<uint32_t>(MaterialType::Metal),
            0.05f, 1.0f, 1.0f, 0.0f};
}

inline Material blueMetal() {
    return {0.2f, 0.4f, 0.9f, static_cast<uint32_t>(MaterialType::Metal),
            0.05f, 1.0f, 1.0f, 0.0f};
}

} // namespace metals
} // namespace parametric
