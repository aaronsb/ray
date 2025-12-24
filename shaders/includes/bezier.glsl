// Bicubic Bezier patch evaluation and ray intersection
// Using Newton-Raphson iteration for direct ray-surface intersection

// ============================================================================
// TUNING PARAMETERS
// ============================================================================
#ifndef NEWTON_MAX_ITER
#define NEWTON_MAX_ITER 10      // Max Newton iterations (6-12 typical)
#endif

#ifndef NEWTON_EPSILON
#define NEWTON_EPSILON 1e-5     // Convergence threshold (1e-4 to 1e-6)
#endif

#ifndef NEWTON_DIVERGE_CHECK
#define NEWTON_DIVERGE_CHECK 1        // Enable early exit on divergence (0 = disabled)
#endif

#ifndef NEWTON_DIVERGE_THRESHOLD
#define NEWTON_DIVERGE_THRESHOLD 2.0  // Early exit if step > this
#endif

// Runtime override (set before calling hitBezierPatch if adaptive iterations needed)
int g_newtonMaxIter = NEWTON_MAX_ITER;
// ============================================================================

// Bernstein basis polynomials for cubic (degree 3)
float bernstein3(int i, float t) {
    float s = 1.0 - t;
    if (i == 0) return s * s * s;
    if (i == 1) return 3.0 * s * s * t;
    if (i == 2) return 3.0 * s * t * t;
    return t * t * t;  // i == 3
}

// Derivative of Bernstein basis
float bernstein3_deriv(int i, float t) {
    float s = 1.0 - t;
    if (i == 0) return -3.0 * s * s;
    if (i == 1) return 3.0 * s * s - 6.0 * s * t;
    if (i == 2) return 6.0 * s * t - 3.0 * t * t;
    return 3.0 * t * t;  // i == 3
}

// Evaluate bicubic Bezier patch at (u, v)
// controlPoints: 16 vec3s in row-major order
vec3 evalBezierPatch(vec3 cp[16], float u, float v) {
    vec3 result = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        float bi = bernstein3(i, u);
        for (int j = 0; j < 4; j++) {
            float bj = bernstein3(j, v);
            result += cp[i * 4 + j] * bi * bj;
        }
    }
    return result;
}

// Partial derivative with respect to u
vec3 evalBezierPatchDu(vec3 cp[16], float u, float v) {
    vec3 result = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        float dbi = bernstein3_deriv(i, u);
        for (int j = 0; j < 4; j++) {
            float bj = bernstein3(j, v);
            result += cp[i * 4 + j] * dbi * bj;
        }
    }
    return result;
}

// Partial derivative with respect to v
vec3 evalBezierPatchDv(vec3 cp[16], float u, float v) {
    vec3 result = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        float bi = bernstein3(i, u);
        for (int j = 0; j < 4; j++) {
            float dbj = bernstein3_deriv(j, v);
            result += cp[i * 4 + j] * bi * dbj;
        }
    }
    return result;
}

// Compute patch normal at (u, v)
vec3 patchNormal(vec3 cp[16], float u, float v) {
    vec3 du = evalBezierPatchDu(cp, u, v);
    vec3 dv = evalBezierPatchDv(cp, u, v);
    return normalize(cross(du, dv));
}

// Compute axis-aligned bounding box for patch
void patchAABB(vec3 cp[16], out vec3 bmin, out vec3 bmax) {
    bmin = cp[0];
    bmax = cp[0];
    for (int i = 1; i < 16; i++) {
        bmin = min(bmin, cp[i]);
        bmax = max(bmax, cp[i]);
    }
}

// Ray-AABB intersection for early rejection
bool hitAABB(vec3 bmin, vec3 bmax, vec3 ro, vec3 rd, float tMin, float tMax) {
    vec3 invD = 1.0 / rd;
    vec3 t0 = (bmin - ro) * invD;
    vec3 t1 = (bmax - ro) * invD;
    vec3 tNear = min(t0, t1);
    vec3 tFar = max(t0, t1);
    float tEnter = max(max(tNear.x, tNear.y), tNear.z);
    float tExit = min(min(tFar.x, tFar.y), tFar.z);
    return tExit >= tEnter && tExit >= tMin && tEnter <= tMax;
}

// Parameter transformation (Tang et al. 2023): maps unbounded (α,β) to bounded (u,v) ∈ (0,1)
// u = 1/(e^(-4α) + 1), v = 1/(e^(-4β) + 1)
// This prevents parameters from escaping [0,1] during Newton iteration
float alphaToU(float alpha) {
    return 1.0 / (exp(-4.0 * alpha) + 1.0);
}
float uToAlpha(float u) {
    return -log(1.0 / u - 1.0) / 4.0;
}
// Derivative: du/dα = 4u(1-u)
float duDalpha(float u) {
    return 4.0 * u * (1.0 - u);
}

// Simple Newton-Raphson with Kajiya's 2-plane formulation
// No fancy transformations - just basic clamped iteration
bool tryNewtonKajiya(vec3 cp[16], vec3 ro, vec3 rd,
                     vec3 N1, vec3 N2, float d1, float d2,
                     float tMin, float tMax,
                     float startU, float startV,
                     out float hitT, out float hitU, out float hitV, out vec3 hitN) {

    float u = startU;
    float v = startV;

    for (int iter = 0; iter < g_newtonMaxIter; iter++) {
        vec3 S = evalBezierPatch(cp, u, v);
        vec3 Su = evalBezierPatchDu(cp, u, v);
        vec3 Sv = evalBezierPatchDv(cp, u, v);

        // F(u,v) = [N1·S + d1, N2·S + d2] - point on ray iff both zero
        float f1 = dot(N1, S) + d1;
        float f2 = dot(N2, S) + d2;

        // Check convergence
        if (abs(f1) < NEWTON_EPSILON && abs(f2) < NEWTON_EPSILON) {
            // Verify (u,v) is in valid range
            if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) {
                return false;  // Converged outside patch
            }

            // Compute t from converged surface point
            float t = dot(S - ro, rd) / dot(rd, rd);

            if (t >= tMin && t <= tMax) {
                hitT = t;
                hitU = u;
                hitV = v;
                vec3 n = normalize(cross(Su, Sv));
                if (dot(n, rd) > 0.0) n = -n;
                hitN = n;
                return true;
            }
            return false;
        }

        // Standard Jacobian (no transformation)
        float j11 = dot(N1, Su), j12 = dot(N1, Sv);
        float j21 = dot(N2, Su), j22 = dot(N2, Sv);

        float det = j11 * j22 - j12 * j21;
        if (abs(det) < 1e-10) return false;

        float invDet = 1.0 / det;
        float du = invDet * (j22 * f1 - j12 * f2);
        float dv = invDet * (-j21 * f1 + j11 * f2);

#if NEWTON_DIVERGE_CHECK
        // Early exit if clearly diverging (wrong patch)
        if (abs(du) + abs(dv) > NEWTON_DIVERGE_THRESHOLD) {
            return false;
        }
#endif

        u -= du;
        v -= dv;

        // Clamp to [0,1] - if we go outside, we're looking at wrong patch
        u = clamp(u, 0.0, 1.0);
        v = clamp(v, 0.0, 1.0);
    }

    return false;  // Didn't converge
}

// Ray-Bezier patch intersection using Kajiya's 2-plane Newton with multiple guesses
bool hitBezierPatch(vec3 cp[16], vec3 ro, vec3 rd, float tMin, float tMax,
                    out float hitT, out float hitU, out float hitV, out vec3 hitN) {

    // Build two planes containing the ray (Kajiya's formulation)
    vec3 N1, N2;
    if (abs(rd.x) > abs(rd.y) && abs(rd.x) > abs(rd.z)) {
        N1 = normalize(vec3(rd.y, -rd.x, 0.0));
    } else {
        N1 = normalize(vec3(0.0, rd.z, -rd.y));
    }
    N2 = normalize(cross(N1, rd));
    float d1 = -dot(N1, ro);
    float d2 = -dot(N2, ro);

    float bestT = tMax;  // Use actual tMax, not tMax + 1!
    float bestU, bestV;
    vec3 bestN;
    bool found = false;

    float t, u, v;
    vec3 n;

    // Single starting point at center - sufficient for well-subdivided patches
    if (tryNewtonKajiya(cp, ro, rd, N1, N2, d1, d2, tMin, bestT,
                        0.5, 0.5, t, u, v, n)) {
        bestT = t;
        bestU = u;
        bestV = v;
        bestN = n;
        found = true;
    }

    if (found) {
        hitT = bestT;
        hitU = bestU;
        hitV = bestV;
        hitN = bestN;
        return true;
    }
    return false;
}

// Version with pre-computed AABB for early rejection
bool hitBezierPatchWithAABB(vec3 cp[16], vec3 aabbMin, vec3 aabbMax,
                             vec3 ro, vec3 rd, float tMin, float tMax,
                             out float hitT, out float hitU, out float hitV, out vec3 hitN) {
    // Early rejection with pre-computed AABB
    if (!hitAABB(aabbMin, aabbMax, ro, rd, tMin, tMax)) {
        return false;
    }
    return hitBezierPatch(cp, ro, rd, tMin, tMax, hitT, hitU, hitV, hitN);
}
