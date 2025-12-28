#pragma once

// Gaussian-based Global Illumination
// Represents surfaces as Gaussians that emit/receive indirect light
// See ADR-012 for design rationale

#include <vector>
#include <cstdint>
#include <cmath>

namespace parametric {

// GPU-compatible Gaussian structure (48 bytes, 16-byte aligned)
struct alignas(16) GIGaussian {
    // Position (12 bytes)
    float posX, posY, posZ;
    // Radius of influence (4 bytes)
    float radius;

    // Normal direction (12 bytes)
    float normX, normY, normZ;
    // Direct emission (for emissive surfaces) (4 bytes)
    float emission;

    // Accumulated radiance (12 bytes)
    float radR, radG, radB;
    // Surface albedo for re-radiation (4 bytes) - packed RGB565 + unused
    uint32_t albedoPacked;

    // Helper to set albedo
    void setAlbedo(float r, float g, float b) {
        uint32_t ri = static_cast<uint32_t>(r * 31.0f) & 0x1F;
        uint32_t gi = static_cast<uint32_t>(g * 63.0f) & 0x3F;
        uint32_t bi = static_cast<uint32_t>(b * 31.0f) & 0x1F;
        albedoPacked = (ri << 11) | (gi << 5) | bi;
    }

    // Helper to get albedo
    void getAlbedo(float& r, float& g, float& b) const {
        r = static_cast<float>((albedoPacked >> 11) & 0x1F) / 31.0f;
        g = static_cast<float>((albedoPacked >> 5) & 0x3F) / 63.0f;
        b = static_cast<float>(albedoPacked & 0x1F) / 31.0f;
    }
};

// Forward declarations
class CSGScene;
class MaterialLibrary;
struct SunLight;
struct Light;
struct SpotLight;

// Gaussian field builder for GI
class GIGaussianField {
public:
    // Place Gaussians on CSG geometry
    void placeOnCSG(const CSGScene& scene, const MaterialLibrary& materials);

    // Compute direct lighting for all Gaussians
    void computeDirectLighting(const SunLight& sun,
                               const std::vector<Light>& pointLights,
                               const std::vector<SpotLight>& spotLights);

    // Trace caustic photons from sun through glass objects
    // Deposits high-intensity Gaussians where refracted light hits surfaces
    void traceCausticPhotons(const CSGScene& scene, const MaterialLibrary& materials,
                             const SunLight& sun, int photonsPerGlass = 64);

    // Propagate light between Gaussians (radiosity iterations)
    void propagate(int iterations = 3);

    // Clear all Gaussians
    void clear() { m_gaussians.clear(); }

    // Truncate to max count (for GPU performance limits)
    void truncate(uint32_t maxCount) {
        if (m_gaussians.size() > maxCount) {
            m_gaussians.resize(maxCount);
        }
    }

    // Accessors
    const std::vector<GIGaussian>& gaussians() const { return m_gaussians; }
    uint32_t count() const { return static_cast<uint32_t>(m_gaussians.size()); }

    // Get scene bounds (for shader normalization)
    void getBounds(float& minX, float& minY, float& minZ,
                   float& maxX, float& maxY, float& maxZ) const;

private:
    std::vector<GIGaussian> m_gaussians;

    // Add a Gaussian at a surface point
    void addGaussian(float px, float py, float pz,
                     float nx, float ny, float nz,
                     float radius,
                     float albedoR, float albedoG, float albedoB,
                     float emission = 0.0f);

    // Place Gaussians on a sphere primitive
    void placeOnSphere(float cx, float cy, float cz, float r,
                       float albedoR, float albedoG, float albedoB,
                       float emission, int samples = 6);

    // Place Gaussians on a box primitive
    void placeOnBox(float cx, float cy, float cz,
                    float hx, float hy, float hz,
                    float albedoR, float albedoG, float albedoB,
                    float emission);

    // Place Gaussians on a cylinder primitive
    void placeOnCylinder(float cx, float cy, float cz,
                         float radius, float height,
                         float albedoR, float albedoG, float albedoB,
                         float emission);

    // Place Gaussians on a cone primitive
    void placeOnCone(float cx, float cy, float cz,
                     float radius, float height,
                     float albedoR, float albedoG, float albedoB,
                     float emission);

    // Place Gaussians on a torus primitive
    void placeOnTorus(float cx, float cy, float cz,
                      float majorR, float minorR,
                      float albedoR, float albedoG, float albedoB,
                      float emission);
};

} // namespace parametric
