# Plan: Area Light Sampling

## Goal
Implement proper area light sampling for emissive objects, enabling:
- Shadows cast by emissive surfaces (Cornell box light panel)
- Multiple emissive light sources
- Unbiased direct lighting from area lights

## Current State
- Emissive materials only contribute via path tracing bounces
- No direct sampling of emissive surfaces
- Shadow rays only trace toward sun (directional light)

## Implementation Approach

### Phase 1: Emissive Object Tracking ✓
- [x] Track which CSG roots have emissive materials
- [x] Build list of emissive primitives with surface area
- [x] Pass emissive light info to shader (binding 12)

### Phase 2: Area Light Sampling (Primitives) ✓
- [x] Implement surface point sampling for CSG primitives:
  - [x] Box: uniform sampling on 6 faces
  - [x] Sphere: uniform hemisphere/sphere sampling
  - [x] Cylinder: sampling on curved surface + caps
  - [x] Cone: sampling on lateral surface + base
  - [x] Torus: rejection sampling for uniform distribution
- [x] Calculate PDFs for each primitive type (1/area)
- [x] Sample random point on emissive surface

### Phase 3: Direct Lighting Integration ✓
- [x] For diffuse surfaces, sample emissive objects
- [x] Cast shadow ray toward sampled point
- [x] Weight by geometry term and PDF
- [x] Combine with sun contribution
- [x] Added FEATURE_AREA_LIGHTS flag for control

### Phase 4: Multiple Importance Sampling (Optional)
- [ ] Balance BRDF sampling vs light sampling
- [ ] Proper MIS weights for reduced variance

## Shader Changes

```glsl
// Sample a point on an emissive primitive
vec3 sampleEmissivePrimitive(uint primIdx, out vec3 lightNormal, out float pdf);

// Direct lighting from emissive objects
vec3 sampleEmissiveLights(vec3 hitPoint, vec3 normal, vec3 viewDir) {
    vec3 result = vec3(0.0);

    for (uint i = 0; i < numEmissivePrims; i++) {
        vec3 lightPos, lightNormal;
        float pdf;
        lightPos = sampleEmissivePrimitive(emissivePrims[i], lightNormal, pdf);

        vec3 lightDir = lightPos - hitPoint;
        float dist = length(lightDir);
        lightDir /= dist;

        // Geometry term
        float NdotL = max(dot(normal, lightDir), 0.0);
        float LNdotL = max(dot(-lightNormal, lightDir), 0.0);
        float G = NdotL * LNdotL / (dist * dist);

        // Shadow test
        if (!inShadow(hitPoint, lightDir, dist)) {
            result += emissiveColor * G / pdf;
        }
    }
    return result;
}
```

## Data Structures

```cpp
// Emissive light info for GPU
struct EmissiveLight {
    uint32_t primitiveIndex;  // CSG primitive index
    uint32_t nodeIndex;       // CSG node for material lookup
    float area;               // Surface area for PDF
    float _pad;
};
```

## Files to Modify

1. `src/parametric/lights/lights.h` - Add EmissiveLight struct
2. `src/parametric/scene/scene_loader.h` - Track emissive objects
3. `src/apps/ray/ray_renderer.cpp` - Create emissive light buffer
4. `shaders/ray.comp` - Add emissive light sampling
5. `shaders/includes/sampling.glsl` - New file for primitive sampling

## Test Scenes

- `cornell.scene` - Verify light panel casts shadows
- Create scene with multiple emissive spheres

## References

- "Physically Based Rendering" Chapter 12 (Light Sources)
- "Real-Time Rendering" Chapter 9.3 (Area Lights)
