// ============================================================================
// sdf/intersect.glsl - Ray-SDF Intersection
// ============================================================================
//
// Newton iteration with sphere tracing for robust ray-SDF intersection.
// Same approach as our Bezier patch solver.
//
// USAGE:
//   1. Define your scene: float myScene(vec3 p) { return sdSphere(p, 1.0); }
//   2. Generate intersection: DEFINE_HIT_SDF(myScene)
//   3. Call: hitSDF_myScene(ro, rd, tMin, tMax, hitT, hitN)
//
// ============================================================================

// --- Tuning Parameters ---

#ifndef SDF_MAX_ITER
#define SDF_MAX_ITER 32
#endif

#ifndef SDF_EPSILON
#define SDF_EPSILON 1e-4
#endif

#ifndef SDF_GRADIENT_EPS
#define SDF_GRADIENT_EPS 1e-4
#endif

// --- Numerical Gradient ---
// Computes surface normal from SDF

#define SDF_GRADIENT(p, sceneSDF) normalize(vec3( \
    sceneSDF((p) + vec3(SDF_GRADIENT_EPS, 0, 0)) - sceneSDF((p) - vec3(SDF_GRADIENT_EPS, 0, 0)), \
    sceneSDF((p) + vec3(0, SDF_GRADIENT_EPS, 0)) - sceneSDF((p) - vec3(0, SDF_GRADIENT_EPS, 0)), \
    sceneSDF((p) + vec3(0, 0, SDF_GRADIENT_EPS)) - sceneSDF((p) - vec3(0, 0, SDF_GRADIENT_EPS)) \
))

// --- Ray Intersection Generator ---
//
// Math: Find t where SDF(ray_origin + t * ray_dir) = 0
//   f(t) = SDF(O + t*D)
//   f'(t) = dot(∇SDF, D)
//   Newton: t_next = t - f(t) / f'(t)

#define DEFINE_HIT_SDF(sceneSDF) \
bool hitSDF_##sceneSDF(vec3 ro, vec3 rd, float tMin, float tMax, \
                       out float hitT, out vec3 hitN) { \
    float t = tMin; \
    \
    for (int i = 0; i < SDF_MAX_ITER; i++) { \
        vec3 p = ro + t * rd; \
        float d = sceneSDF(p); \
        \
        if (abs(d) < SDF_EPSILON) { \
            hitT = t; \
            hitN = SDF_GRADIENT(p, sceneSDF); \
            return true; \
        } \
        \
        /* Sphere trace when far, Newton when close */ \
        if (d > SDF_EPSILON * 10.0) { \
            t += d * 0.9; \
        } else { \
            vec3 grad = SDF_GRADIENT(p, sceneSDF); \
            float ddt = dot(grad, rd); \
            if (abs(ddt) > 1e-6) { \
                t -= d / ddt; \
            } else { \
                t += abs(d); \
            } \
        } \
        \
        if (t > tMax) return false; \
        t = max(t, tMin); \
    } \
    \
    return false; \
}
