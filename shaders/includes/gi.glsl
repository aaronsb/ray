// Gaussian-based Global Illumination
// See ADR-012 for design rationale

#ifndef GI_GLSL
#define GI_GLSL

// GPU-compatible Gaussian structure (48 bytes, matches CPU)
struct GIGaussian {
    vec3 position;      // 12 bytes
    float radius;       // 4 bytes

    vec3 normal;        // 12 bytes
    float emission;     // 4 bytes

    vec3 radiance;      // 12 bytes
    uint albedoPacked;  // 4 bytes (RGB565)
};

// Unpack albedo from RGB565
vec3 unpackAlbedo(uint packed) {
    float r = float((packed >> 11) & 0x1Fu) / 31.0;
    float g = float((packed >> 5) & 0x3Fu) / 63.0;
    float b = float(packed & 0x1Fu) / 31.0;
    return vec3(r, g, b);
}

// Query the Gaussian field for indirect illumination at a surface point
// p: surface position
// n: surface normal
// albedo: surface albedo (for BRDF)
// Returns: indirect radiance
vec3 queryGaussianGI(vec3 p, vec3 n, vec3 albedo) {
    vec3 indirect = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0; i < pc.numGaussians; i++) {
        GIGaussian g = gaussians[i];

        vec3 toG = g.position - p;
        float dist2 = dot(toG, toG);
        float dist = sqrt(dist2);

        if (dist < 0.01) continue;  // Skip self

        vec3 dir = toG / dist;

        // Gaussian spatial falloff
        float sigma = g.radius;
        float spatial = exp(-dist2 / (2.0 * sigma * sigma));

        // Hemisphere visibility (receiver normal)
        float cosReceiver = max(0.0, dot(n, dir));

        // Emitter orientation (Gaussian radiates in its normal hemisphere)
        float cosEmitter = max(0.0, dot(g.normal, -dir));

        // Combined weight
        float weight = spatial * cosReceiver * cosEmitter;

        // Distance attenuation (softer than 1/r^2)
        weight /= (1.0 + dist2 * 0.1);

        indirect += g.radiance * weight;
        totalWeight += weight;
    }

    // Normalize and apply surface BRDF
    if (totalWeight > 0.001) {
        indirect /= totalWeight;
    }

    return (albedo / 3.14159265) * indirect;
}

// Simplified AO query - just check if Gaussians are nearby and facing away
float queryGaussianAO(vec3 p, vec3 n) {
    float occlusion = 0.0;

    for (uint i = 0; i < pc.numGaussians; i++) {
        GIGaussian g = gaussians[i];

        vec3 toG = g.position - p;
        float dist2 = dot(toG, toG);
        float dist = sqrt(dist2);

        if (dist < 0.01) continue;

        vec3 dir = toG / dist;

        // Only occlude if Gaussian is in our hemisphere
        float cosReceiver = dot(n, dir);
        if (cosReceiver < 0.0) continue;

        // Gaussian spatial influence
        float sigma = g.radius * 2.0;  // Wider for AO
        float spatial = exp(-dist2 / (2.0 * sigma * sigma));

        // Facing away from us = more occlusion
        float cosEmitter = dot(g.normal, -dir);
        float facing = max(0.0, -cosEmitter);  // Facing towards us = occludes

        occlusion += spatial * cosReceiver * facing;
    }

    return clamp(1.0 - occlusion * 0.3, 0.0, 1.0);
}

#endif // GI_GLSL
