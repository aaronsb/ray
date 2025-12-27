#pragma once

// Scene lighting: sun and point lights

#include <vector>
#include <cstdint>
#include <cmath>

namespace parametric {

// Light types
enum class LightType : uint32_t {
    Directional = 0,  // Sun/moon - infinitely distant
    Point = 1,        // Point light
    Spot = 2,         // Spotlight with cone and optional gobo
};

// Gobo pattern types (procedural)
enum class GoboPattern : uint32_t {
    None = 0,         // No pattern (solid light)
    Bars = 1,         // Vertical bars (window blinds)
    Grid = 2,         // Grid pattern
    Dots = 3,         // Dot array
    Radial = 4,       // Radial spokes
    Noise = 5,        // Perlin noise
};

// GPU-compatible light structure (32 bytes)
struct alignas(16) Light {
    float dirX, dirY, dirZ;    // Direction (directional) or position (point)
    uint32_t type;
    float r, g, b;             // Color
    float intensity;           // Brightness multiplier
};

// GPU-compatible spotlight structure (64 bytes)
struct alignas(16) SpotLight {
    float posX, posY, posZ;    // Position
    uint32_t goboPattern;      // GoboPattern enum value
    float dirX, dirY, dirZ;    // Direction (normalized)
    float goboScale;           // Pattern scale (higher = more repetitions)
    float r, g, b;             // Color
    float intensity;           // Brightness multiplier
    float cosInner;            // cos(inner cone angle) - full brightness inside
    float cosOuter;            // cos(outer cone angle) - falloff to this
    float _pad0, _pad1;
};

// Sun parameters for scene
struct SunLight {
    float azimuth = 45.0f;     // Horizontal angle (degrees, 0 = north, 90 = east)
    float elevation = 45.0f;   // Vertical angle (degrees, 0 = horizon, 90 = zenith)
    float r = 1.0f, g = 0.98f, b = 0.9f;  // Warm white
    float intensity = 0.0f;    // Default 0 = no light unless scene defines it
    float angularRadius = 0.53f;  // Sun's angular radius in degrees (~0.53 for real sun)
    float ambient = 0.15f;     // Sky ambient contribution (0 = no ambient, e.g. space)

    // Convert azimuth/elevation to direction vector
    void getDirection(float& x, float& y, float& z) const {
        float azRad = azimuth * 3.14159265f / 180.0f;
        float elRad = elevation * 3.14159265f / 180.0f;
        x = std::sin(azRad) * std::cos(elRad);
        y = std::sin(elRad);
        z = std::cos(azRad) * std::cos(elRad);
    }

    // Build GPU light from sun parameters
    Light toLight() const {
        Light l;
        getDirection(l.dirX, l.dirY, l.dirZ);
        l.type = static_cast<uint32_t>(LightType::Directional);
        l.r = r;
        l.g = g;
        l.b = b;
        l.intensity = intensity;
        return l;
    }
};

// Emissive area light (from CSG primitive with emissive material)
// GPU-compatible structure (16 bytes)
struct alignas(16) EmissiveLight {
    uint32_t primitiveIndex;  // Index into CSGPrimitive buffer
    uint32_t nodeIndex;       // Index into CSGNode buffer (for material lookup)
    float area;               // Surface area for PDF calculation
    float _pad;
};

// Light collection for a scene
class LightList {
public:
    SunLight sun;
    std::vector<Light> pointLights;
    std::vector<SpotLight> spotLights;
    std::vector<EmissiveLight> emissiveLights;

    // Build complete light buffer (sun first, then point lights)
    std::vector<Light> buildBuffer() const {
        std::vector<Light> result;
        result.push_back(sun.toLight());
        result.insert(result.end(), pointLights.begin(), pointLights.end());
        return result;
    }

    // Build spotlight buffer
    const std::vector<SpotLight>& spotLightBuffer() const {
        return spotLights;
    }

    // Build emissive light buffer
    const std::vector<EmissiveLight>& emissiveBuffer() const {
        return emissiveLights;
    }

    uint32_t sunCount() const { return 1; }
    uint32_t pointLightCount() const { return static_cast<uint32_t>(pointLights.size()); }
    uint32_t spotLightCount() const { return static_cast<uint32_t>(spotLights.size()); }
    uint32_t emissiveCount() const { return static_cast<uint32_t>(emissiveLights.size()); }
    uint32_t totalCount() const { return sunCount() + pointLightCount(); }

    // Get sun angular radius for disc rendering
    float sunAngularRadius() const { return sun.angularRadius; }

    // Get sky ambient level (0 = no ambient, e.g. space)
    float skyAmbient() const { return sun.ambient; }
};

} // namespace parametric
