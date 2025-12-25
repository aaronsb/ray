// ============================================================================
// sdf/intersect.glsl - Ray-SDF Intersection
// ============================================================================
//
// Sphere tracing for SDF ray intersection.
// Best suited for: volumetrics, clouds, soft organic shapes.
// NOT ideal for: hard-edged CSG on solids (use interval arithmetic instead).
//
// USAGE:
//   1. Define your scene: float myScene(vec3 p) { return sdSphere(p, 1.0); }
//   2. Generate intersection: DEFINE_HIT_SDF(myScene)
//   3. Call: hitSDF_myScene(ro, rd, tMin, tMax, hitT, hitN)
//
// ============================================================================

// --- Tuning Parameters ---

#ifndef SDF_MAX_ITER
#define SDF_MAX_ITER 64
#endif

#ifndef SDF_EPSILON
#define SDF_EPSILON 1e-4
#endif

#ifndef SDF_GRADIENT_EPS
#define SDF_GRADIENT_EPS 1e-4
#endif

// --- Numerical Gradient ---

#define SDF_GRADIENT(p, sceneSDF) normalize(vec3( \
    sceneSDF((p) + vec3(SDF_GRADIENT_EPS, 0, 0)) - sceneSDF((p) - vec3(SDF_GRADIENT_EPS, 0, 0)), \
    sceneSDF((p) + vec3(0, SDF_GRADIENT_EPS, 0)) - sceneSDF((p) - vec3(0, SDF_GRADIENT_EPS, 0)), \
    sceneSDF((p) + vec3(0, 0, SDF_GRADIENT_EPS)) - sceneSDF((p) - vec3(0, 0, SDF_GRADIENT_EPS)) \
))

// --- Simple Sphere Tracing ---

#define DEFINE_HIT_SDF(sceneSDF) \
bool hitSDF_##sceneSDF(vec3 ro, vec3 rd, float tMin, float tMax, \
                       out float hitT, out vec3 hitN) { \
    float t = tMin; \
    \
    for (int i = 0; i < SDF_MAX_ITER; i++) { \
        vec3 p = ro + t * rd; \
        float d = sceneSDF(p); \
        \
        if (d < SDF_EPSILON) { \
            hitT = t; \
            hitN = SDF_GRADIENT(p, sceneSDF); \
            return true; \
        } \
        \
        t += d; \
        if (t > tMax) return false; \
    } \
    \
    return false; \
}
