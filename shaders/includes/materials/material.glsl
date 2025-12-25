// ============================================================================
// materials/material.glsl - Material Structure and Buffer Access
// ============================================================================
// Include this after binding the materials buffer

#include "types.glsl"
#include "sampling.glsl"

// GPU Material - 32 bytes, matches C++ Material struct
struct Material {
    float r, g, b;          // albedo
    uint type;              // MAT_DIFFUSE, MAT_METAL, MAT_GLASS, MAT_EMISSIVE
    float roughness;        // 0=mirror, 1=matte
    float metallic;         // 0=dielectric, 1=conductor
    float ior;              // index of refraction
    float emissive;         // emission multiplier
};

// Get material albedo as vec3
vec3 materialAlbedo(Material m) {
    return vec3(m.r, m.g, m.b);
}

// Check if material is reflective (metal or smooth dielectric)
bool isReflective(Material m) {
    return m.type == MAT_METAL || (m.type == MAT_GLASS && m.roughness < 0.5);
}

// Check if material is transmissive
bool isTransmissive(Material m) {
    return m.type == MAT_GLASS;
}
