#pragma once

// Diffuse (matte) material presets
// Usage: library.add("white", diffuse::white());

#include "../material.h"

namespace parametric {
namespace diffuse {

// Neutrals
inline Material white() {
    return {0.9f, 0.9f, 0.9f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

inline Material gray() {
    return {0.5f, 0.5f, 0.5f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

inline Material black() {
    return {0.05f, 0.05f, 0.05f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

// Primary colors
inline Material red() {
    return {0.8f, 0.2f, 0.2f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

inline Material green() {
    return {0.2f, 0.7f, 0.2f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

inline Material blue() {
    return {0.2f, 0.3f, 0.8f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

// Secondary colors
inline Material yellow() {
    return {0.9f, 0.85f, 0.2f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

inline Material cyan() {
    return {0.2f, 0.8f, 0.85f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

inline Material magenta() {
    return {0.85f, 0.2f, 0.8f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

// Natural materials
inline Material terracotta() {
    return {0.8f, 0.45f, 0.3f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

inline Material clay() {
    return {0.65f, 0.55f, 0.45f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

inline Material skin() {
    return {0.85f, 0.65f, 0.55f, static_cast<uint32_t>(MaterialType::Diffuse),
            1.0f, 0.0f, 1.0f, 0.0f};
}

} // namespace diffuse
} // namespace parametric
