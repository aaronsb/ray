// Gaussian-based Global Illumination implementation
// See ADR-012 for design rationale

#include "gi_gaussian.h"
#include "../csg/csg.h"
#include "../materials/material.h"
#include "../lights/lights.h"
#include <cmath>
#include <algorithm>

namespace parametric {

namespace {
    constexpr float PI = 3.14159265358979323846f;
}

void GIGaussianField::addGaussian(float px, float py, float pz,
                                   float nx, float ny, float nz,
                                   float radius,
                                   float albedoR, float albedoG, float albedoB,
                                   float emission) {
    GIGaussian g;
    g.posX = px; g.posY = py; g.posZ = pz;
    g.normX = nx; g.normY = ny; g.normZ = nz;
    g.radius = radius;
    g.emission = emission;
    g.radR = 0; g.radG = 0; g.radB = 0;  // Will be computed later
    g.setAlbedo(albedoR, albedoG, albedoB);
    m_gaussians.push_back(g);
}

void GIGaussianField::placeOnSphere(float cx, float cy, float cz, float r,
                                     float albedoR, float albedoG, float albedoB,
                                     float emission, int samples) {
    // Place Gaussians using spherical fibonacci distribution
    float goldenRatio = (1.0f + std::sqrt(5.0f)) / 2.0f;
    float angleIncrement = PI * 2.0f * goldenRatio;

    for (int i = 0; i < samples; i++) {
        float t = static_cast<float>(i) / static_cast<float>(samples);
        float inclination = std::acos(1.0f - 2.0f * t);
        float azimuth = angleIncrement * i;

        float nx = std::sin(inclination) * std::cos(azimuth);
        float ny = std::cos(inclination);
        float nz = std::sin(inclination) * std::sin(azimuth);

        float px = cx + nx * r;
        float py = cy + ny * r;
        float pz = cz + nz * r;

        // Gaussian radius based on sphere size and sample count
        float gRadius = r * 2.0f / std::sqrt(static_cast<float>(samples));

        addGaussian(px, py, pz, nx, ny, nz, gRadius, albedoR, albedoG, albedoB, emission);
    }
}

void GIGaussianField::placeOnBox(float cx, float cy, float cz,
                                  float hx, float hy, float hz,
                                  float albedoR, float albedoG, float albedoB,
                                  float emission) {
    // Place one Gaussian at center of each face
    float gRadius = std::min({hx, hy, hz}) * 1.5f;

    // +X face
    addGaussian(cx + hx, cy, cz, 1, 0, 0, gRadius, albedoR, albedoG, albedoB, emission);
    // -X face
    addGaussian(cx - hx, cy, cz, -1, 0, 0, gRadius, albedoR, albedoG, albedoB, emission);
    // +Y face
    addGaussian(cx, cy + hy, cz, 0, 1, 0, gRadius, albedoR, albedoG, albedoB, emission);
    // -Y face
    addGaussian(cx, cy - hy, cz, 0, -1, 0, gRadius, albedoR, albedoG, albedoB, emission);
    // +Z face
    addGaussian(cx, cy, cz + hz, 0, 0, 1, gRadius, albedoR, albedoG, albedoB, emission);
    // -Z face
    addGaussian(cx, cy, cz - hz, 0, 0, -1, gRadius, albedoR, albedoG, albedoB, emission);
}

void GIGaussianField::placeOnCylinder(float cx, float cy, float cz,
                                       float radius, float height,
                                       float albedoR, float albedoG, float albedoB,
                                       float emission) {
    float gRadius = radius;

    // Top cap
    addGaussian(cx, cy + height, cz, 0, 1, 0, gRadius, albedoR, albedoG, albedoB, emission);
    // Bottom cap
    addGaussian(cx, cy, cz, 0, -1, 0, gRadius, albedoR, albedoG, albedoB, emission);

    // Lateral surface (4 points around)
    for (int i = 0; i < 4; i++) {
        float angle = PI * 2.0f * i / 4.0f;
        float nx = std::cos(angle);
        float nz = std::sin(angle);
        float px = cx + nx * radius;
        float pz = cz + nz * radius;
        float py = cy + height * 0.5f;  // Middle height

        addGaussian(px, py, pz, nx, 0, nz, gRadius, albedoR, albedoG, albedoB, emission);
    }
}

void GIGaussianField::placeOnCone(float cx, float cy, float cz,
                                   float radius, float height,
                                   float albedoR, float albedoG, float albedoB,
                                   float emission) {
    float gRadius = radius;

    // Base center
    addGaussian(cx, cy, cz, 0, -1, 0, gRadius, albedoR, albedoG, albedoB, emission);

    // Tip
    addGaussian(cx, cy + height, cz, 0, 1, 0, gRadius * 0.5f, albedoR, albedoG, albedoB, emission);

    // Lateral surface (4 points around at 1/3 height)
    float slantAngle = std::atan2(radius, height);
    float ny = std::sin(slantAngle);
    float nHoriz = std::cos(slantAngle);

    for (int i = 0; i < 4; i++) {
        float angle = PI * 2.0f * i / 4.0f;
        float nx = nHoriz * std::cos(angle);
        float nz = nHoriz * std::sin(angle);

        float t = 0.33f;  // 1/3 up the cone
        float r = radius * (1.0f - t);
        float px = cx + std::cos(angle) * r;
        float pz = cz + std::sin(angle) * r;
        float py = cy + height * t;

        addGaussian(px, py, pz, nx, ny, nz, gRadius * 0.7f, albedoR, albedoG, albedoB, emission);
    }
}

void GIGaussianField::placeOnTorus(float cx, float cy, float cz,
                                    float majorR, float minorR,
                                    float albedoR, float albedoG, float albedoB,
                                    float emission) {
    float gRadius = minorR * 1.5f;
    int majorSamples = 8;
    int minorSamples = 4;

    for (int i = 0; i < majorSamples; i++) {
        float majorAngle = PI * 2.0f * i / majorSamples;
        float ringCenterX = cx + majorR * std::cos(majorAngle);
        float ringCenterZ = cz + majorR * std::sin(majorAngle);

        for (int j = 0; j < minorSamples; j++) {
            float minorAngle = PI * 2.0f * j / minorSamples;

            // Normal in torus local frame
            float localNx = std::cos(minorAngle);
            float localNy = std::sin(minorAngle);

            // Transform to world
            float nx = localNx * std::cos(majorAngle);
            float ny = localNy;
            float nz = localNx * std::sin(majorAngle);

            float px = ringCenterX + minorR * localNx * std::cos(majorAngle);
            float py = cy + minorR * localNy;
            float pz = ringCenterZ + minorR * localNx * std::sin(majorAngle);

            addGaussian(px, py, pz, nx, ny, nz, gRadius, albedoR, albedoG, albedoB, emission);
        }
    }
}

void GIGaussianField::placeOnCSG(const CSGScene& scene, const MaterialLibrary& materials) {
    clear();

    const auto& prims = scene.primitives();
    const auto& nodes = scene.nodes();
    const auto& roots = scene.roots();

    // For each root, find the primitives and place Gaussians
    for (uint32_t rootIdx : roots) {
        const CSGNode& node = nodes[rootIdx];
        uint32_t materialId = node.materialId;

        // Get material properties
        float albedoR = 0.5f, albedoG = 0.5f, albedoB = 0.5f;
        float emission = 0.0f;

        if (materialId < materials.count()) {
            const Material& mat = materials.materials()[materialId];
            albedoR = mat.r;
            albedoG = mat.g;
            albedoB = mat.b;
            if (mat.type == 3) {  // Emissive
                emission = mat.emissive;
            }
        }

        // For now, only handle simple primitive nodes
        // TODO: Handle CSG operations by sampling the combined surface
        if (node.type == 0) {  // Primitive
            const CSGPrimitive& prim = prims[node.left];

            switch (prim.type) {
                case 0:  // Sphere
                    placeOnSphere(prim.x, prim.y, prim.z, prim.param0,
                                  albedoR, albedoG, albedoB, emission);
                    break;
                case 1:  // Box
                    placeOnBox(prim.x, prim.y, prim.z,
                               prim.param0, prim.param1, prim.param2,
                               albedoR, albedoG, albedoB, emission);
                    break;
                case 2:  // Cylinder
                    placeOnCylinder(prim.x, prim.y, prim.z,
                                    prim.param0, prim.param1,
                                    albedoR, albedoG, albedoB, emission);
                    break;
                case 3:  // Cone
                    placeOnCone(prim.x, prim.y, prim.z,
                                prim.param0, prim.param1,
                                albedoR, albedoG, albedoB, emission);
                    break;
                case 4:  // Torus
                    placeOnTorus(prim.x, prim.y, prim.z,
                                 prim.param0, prim.param1,
                                 albedoR, albedoG, albedoB, emission);
                    break;
            }
        }
    }
}

void GIGaussianField::computeDirectLighting(const SunLight& sun,
                                             const std::vector<Light>& pointLights,
                                             const std::vector<SpotLight>& spotLights) {
    // Get sun direction
    float sunDirX, sunDirY, sunDirZ;
    sun.getDirection(sunDirX, sunDirY, sunDirZ);

    for (auto& g : m_gaussians) {
        float r = 0, gr = 0, b = 0;

        // Get albedo
        float albedoR, albedoG, albedoB;
        g.getAlbedo(albedoR, albedoG, albedoB);

        // Sun contribution (simple N.L)
        float NdotL = g.normX * sunDirX + g.normY * sunDirY + g.normZ * sunDirZ;
        if (NdotL > 0 && sun.intensity > 0) {
            r += sun.r * sun.intensity * NdotL;
            gr += sun.g * sun.intensity * NdotL;
            b += sun.b * sun.intensity * NdotL;
        }

        // Point lights
        for (const auto& light : pointLights) {
            float lx = light.dirX - g.posX;  // dirX/Y/Z stores position for point lights
            float ly = light.dirY - g.posY;
            float lz = light.dirZ - g.posZ;
            float dist2 = lx*lx + ly*ly + lz*lz;
            float dist = std::sqrt(dist2);

            if (dist > 0.001f) {
                lx /= dist; ly /= dist; lz /= dist;
                NdotL = g.normX * lx + g.normY * ly + g.normZ * lz;
                if (NdotL > 0) {
                    float atten = light.intensity / (1.0f + dist2);
                    r += light.r * atten * NdotL;
                    gr += light.g * atten * NdotL;
                    b += light.b * atten * NdotL;
                }
            }
        }

        // Spot lights (simplified - no cone attenuation for now)
        for (const auto& spot : spotLights) {
            float lx = spot.posX - g.posX;
            float ly = spot.posY - g.posY;
            float lz = spot.posZ - g.posZ;
            float dist2 = lx*lx + ly*ly + lz*lz;
            float dist = std::sqrt(dist2);

            if (dist > 0.001f) {
                lx /= dist; ly /= dist; lz /= dist;

                // Check if in cone
                float cosAngle = -(lx * spot.dirX + ly * spot.dirY + lz * spot.dirZ);
                if (cosAngle > spot.cosOuter) {
                    NdotL = g.normX * lx + g.normY * ly + g.normZ * lz;
                    if (NdotL > 0) {
                        float atten = spot.intensity / (1.0f + dist2);

                        // Cone falloff
                        float t = (cosAngle - spot.cosOuter) / (spot.cosInner - spot.cosOuter);
                        t = std::clamp(t, 0.0f, 1.0f);
                        atten *= t;

                        r += spot.r * atten * NdotL;
                        gr += spot.g * atten * NdotL;
                        b += spot.b * atten * NdotL;
                    }
                }
            }
        }

        // Store radiance (albedo * irradiance / PI for diffuse BRDF)
        g.radR = albedoR * r / PI + g.emission * albedoR;
        g.radG = albedoG * gr / PI + g.emission * albedoG;
        g.radB = albedoB * b / PI + g.emission * albedoB;
    }
}

void GIGaussianField::propagate(int iterations) {
    if (m_gaussians.empty()) return;

    // Temporary buffer for next iteration
    std::vector<GIGaussian> next = m_gaussians;

    for (int iter = 0; iter < iterations; iter++) {
        for (size_t i = 0; i < m_gaussians.size(); i++) {
            GIGaussian& gi = next[i];
            const GIGaussian& g = m_gaussians[i];

            // Get albedo for re-radiation
            float albedoR, albedoG, albedoB;
            g.getAlbedo(albedoR, albedoG, albedoB);

            float inR = 0, inG = 0, inB = 0;

            // Gather from all other Gaussians
            for (size_t j = 0; j < m_gaussians.size(); j++) {
                if (i == j) continue;

                const GIGaussian& other = m_gaussians[j];

                float dx = other.posX - g.posX;
                float dy = other.posY - g.posY;
                float dz = other.posZ - g.posZ;
                float dist2 = dx*dx + dy*dy + dz*dz;
                float dist = std::sqrt(dist2);

                if (dist < 0.001f) continue;

                dx /= dist; dy /= dist; dz /= dist;

                // Form factor approximation
                float cosReceiver = g.normX * dx + g.normY * dy + g.normZ * dz;
                float cosEmitter = -(other.normX * dx + other.normY * dy + other.normZ * dz);

                if (cosReceiver > 0 && cosEmitter > 0) {
                    // Gaussian spatial falloff
                    float sigma = (g.radius + other.radius) * 0.5f;
                    float falloff = std::exp(-dist2 / (2.0f * sigma * sigma));

                    float weight = cosReceiver * cosEmitter * falloff / (dist2 + 1.0f);

                    inR += other.radR * weight;
                    inG += other.radG * weight;
                    inB += other.radB * weight;
                }
            }

            // Add incoming radiance (modulated by albedo)
            gi.radR = g.radR + albedoR * inR * 0.5f;  // Damping factor
            gi.radG = g.radG + albedoG * inG * 0.5f;
            gi.radB = g.radB + albedoB * inB * 0.5f;
        }

        m_gaussians = next;
    }
}

void GIGaussianField::getBounds(float& minX, float& minY, float& minZ,
                                 float& maxX, float& maxY, float& maxZ) const {
    if (m_gaussians.empty()) {
        minX = minY = minZ = -10.0f;
        maxX = maxY = maxZ = 10.0f;
        return;
    }

    minX = minY = minZ = 1e10f;
    maxX = maxY = maxZ = -1e10f;

    for (const auto& g : m_gaussians) {
        minX = std::min(minX, g.posX - g.radius);
        minY = std::min(minY, g.posY - g.radius);
        minZ = std::min(minZ, g.posZ - g.radius);
        maxX = std::max(maxX, g.posX + g.radius);
        maxY = std::max(maxY, g.posY + g.radius);
        maxZ = std::max(maxZ, g.posZ + g.radius);
    }
}

} // namespace parametric
