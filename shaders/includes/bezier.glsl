// Bicubic Bezier patch evaluation and ray intersection
// Using Newton-Raphson iteration for direct ray-surface intersection

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

// Ray-Bezier patch intersection using Newton-Raphson
// Optimized for pre-subdivided small patches where center guess works
bool hitBezierPatch(vec3 cp[16], vec3 ro, vec3 rd, float tMin, float tMax,
                    out float hitT, out float hitU, out float hitV, out vec3 hitN) {
    // Initial guess: center of parameter space
    float u = 0.5;
    float v = 0.5;

    // Estimate initial t from patch center
    vec3 centerPt = evalBezierPatch(cp, 0.5, 0.5);
    vec3 toCenter = centerPt - ro;
    float t = dot(toCenter, rd) / dot(rd, rd);

    // Clamp initial t to valid range
    t = clamp(t, tMin, tMax);

    // For pre-subdivided patches, 8 iterations is plenty
    const int MAX_ITER = 8;
    const float EPSILON = 1e-5;

    for (int iter = 0; iter < MAX_ITER; iter++) {
        // Evaluate surface and derivatives
        vec3 S = evalBezierPatch(cp, u, v);
        vec3 Su = evalBezierPatchDu(cp, u, v);
        vec3 Sv = evalBezierPatchDv(cp, u, v);

        // F = S(u,v) - (ro + t * rd)
        vec3 F = S - (ro + t * rd);

        // Check convergence
        float err = dot(F, F);
        if (err < EPSILON * EPSILON) {
            // Verify parameters in valid range
            if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0 && t >= tMin && t <= tMax) {
                hitT = t;
                hitU = u;
                hitV = v;
                hitN = patchNormal(cp, u, v);
                return true;
            }
            return false;
        }

        // Jacobian: [Su | Sv | -rd]
        // Solve J * delta = -F using Cramer's rule
        vec3 c0 = Su;
        vec3 c1 = Sv;
        vec3 c2 = -rd;

        float det = dot(c0, cross(c1, c2));
        if (abs(det) < 1e-10) {
            return false;  // Singular - ray parallel to surface
        }

        float invDet = 1.0 / det;
        float du = dot(-F, cross(c1, c2)) * invDet;
        float dv = dot(c0, cross(-F, c2)) * invDet;
        float dt = dot(c0, cross(c1, -F)) * invDet;

        // Update with damping for stability
        u += du;
        v += dv;
        t += dt;

        // Early exit if parameters diverge (no intersection)
        if (u < -0.5 || u > 1.5 || v < -0.5 || v > 1.5) {
            return false;
        }
    }

    // Final check after max iterations
    if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0 && t >= tMin && t <= tMax) {
        vec3 S = evalBezierPatch(cp, u, v);
        vec3 F = S - (ro + t * rd);
        if (dot(F, F) < EPSILON * 10.0) {  // Slightly relaxed tolerance
            hitT = t;
            hitU = u;
            hitV = v;
            hitN = patchNormal(cp, u, v);
            return true;
        }
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
