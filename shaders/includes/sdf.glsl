// ============================================================================
// sdf.glsl - Signed Distance Functions for CSG Ray Tracing
// ============================================================================
//
// OVERVIEW:
//   This library provides SDF primitives, CSG operations, and ray intersection
//   using Newton iteration (same approach as our Bezier patch solver).
//
// USAGE:
//   1. Include this file
//   2. Define your scene SDF function: float myScene(vec3 p) { ... }
//   3. Use DEFINE_HIT_SDF(myScene) to generate intersection function
//   4. Call hitSDF_myScene(ro, rd, tMin, tMax, hitT, hitN)
//
// ============================================================================

// ============================================================================
// SECTION 1: TUNING PARAMETERS
// ============================================================================

#ifndef SDF_MAX_ITER
#define SDF_MAX_ITER 32
#endif

#ifndef SDF_EPSILON
#define SDF_EPSILON 1e-4
#endif

#ifndef SDF_GRADIENT_EPS
#define SDF_GRADIENT_EPS 1e-4
#endif

// ============================================================================
// SECTION 2: PRIMITIVE SDFs
// ============================================================================
// All primitives are centered at origin. Use transforms to position them.
// Convention: negative = inside, positive = outside, zero = surface

// --- Sphere ---
// radius: sphere radius
float sdSphere(vec3 p, float radius) {
    return length(p) - radius;
}

// --- Box ---
// halfExtents: half-size in each dimension
float sdBox(vec3 p, vec3 halfExtents) {
    vec3 q = abs(p) - halfExtents;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// --- Cylinder (Y-axis) ---
// radius: cylinder radius
// halfHeight: half the total height
float sdCylinder(vec3 p, float radius, float halfHeight) {
    vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(radius, halfHeight);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// --- Torus (XZ plane) ---
// majorRadius: distance from center to tube center
// minorRadius: tube radius
float sdTorus(vec3 p, float majorRadius, float minorRadius) {
    vec2 q = vec2(length(p.xz) - majorRadius, p.y);
    return length(q) - minorRadius;
}

// --- Capsule (Y-axis) ---
// radius: capsule radius
// halfHeight: half the cylinder portion height
float sdCapsule(vec3 p, float radius, float halfHeight) {
    p.y -= clamp(p.y, -halfHeight, halfHeight);
    return length(p) - radius;
}

// --- Plane ---
// normal: plane normal (should be normalized)
// offset: distance from origin along normal
float sdPlane(vec3 p, vec3 normal, float offset) {
    return dot(p, normal) + offset;
}

// --- Cone (tip at origin, base at -Y) ---
// radius: base radius
// height: cone height
float sdCone(vec3 p, float radius, float height) {
    vec2 q = vec2(length(p.xz), -p.y);
    vec2 tip = vec2(radius, height);
    vec2 w = q - tip * clamp(dot(q, tip) / dot(tip, tip), 0.0, 1.0);
    vec2 b = q - tip * vec2(clamp(q.x / tip.x, 0.0, 1.0), 1.0);
    float d = min(dot(w, w), dot(b, b));
    float s = max(q.x * height - q.y * radius, q.y - height);
    return sqrt(d) * sign(s);
}

// ============================================================================
// SECTION 3: CSG OPERATIONS
// ============================================================================
// Combine SDFs to create complex shapes

// --- Boolean Operations ---

float opUnion(float a, float b) {
    return min(a, b);
}

float opIntersect(float a, float b) {
    return max(a, b);
}

float opSubtract(float a, float b) {
    return max(a, -b);
}

// --- Smooth Boolean Operations ---
// k: smoothing radius

float opSmoothUnion(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float opSmoothIntersect(float a, float b, float k) {
    float h = clamp(0.5 - 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) + k * h * (1.0 - h);
}

float opSmoothSubtract(float a, float b, float k) {
    float h = clamp(0.5 - 0.5 * (a + b) / k, 0.0, 1.0);
    return mix(a, -b, h) + k * h * (1.0 - h);
}

// ============================================================================
// SECTION 4: TRANSFORM OPERATIONS
// ============================================================================
// Apply to query point BEFORE evaluating SDF

vec3 opTranslate(vec3 p, vec3 offset) {
    return p - offset;
}

vec3 opRotateX(vec3 p, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec3(p.x, c * p.y - s * p.z, s * p.y + c * p.z);
}

vec3 opRotateY(vec3 p, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

vec3 opRotateZ(vec3 p, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

// For uniform scale: evaluate SDF(p/s) * s
// Non-uniform scale distorts distances - use with care

// ============================================================================
// SECTION 5: GRADIENT COMPUTATION
// ============================================================================
// Numerical gradient for Newton iteration and surface normals

#define SDF_GRADIENT(p, sceneSDF) normalize(vec3( \
    sceneSDF((p) + vec3(SDF_GRADIENT_EPS, 0, 0)) - sceneSDF((p) - vec3(SDF_GRADIENT_EPS, 0, 0)), \
    sceneSDF((p) + vec3(0, SDF_GRADIENT_EPS, 0)) - sceneSDF((p) - vec3(0, SDF_GRADIENT_EPS, 0)), \
    sceneSDF((p) + vec3(0, 0, SDF_GRADIENT_EPS)) - sceneSDF((p) - vec3(0, 0, SDF_GRADIENT_EPS)) \
))

// ============================================================================
// SECTION 6: RAY-SDF INTERSECTION
// ============================================================================
// Newton iteration with sphere tracing fallback
//
// Math: Find t where SDF(ray_origin + t * ray_dir) = 0
//   f(t) = SDF(O + t*D)
//   f'(t) = dot(∇SDF, D)
//   t_next = t - f(t) / f'(t)

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
        /* Sphere trace step (safe) */ \
        if (d > SDF_EPSILON * 10.0) { \
            t += d * 0.9; \
        } else { \
            /* Newton step when close */ \
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
