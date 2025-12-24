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

// Kajiya's 2-plane formulation: reduces 3 unknowns (u,v,t) to 2 (u,v)
// More stable than the 3x3 Jacobian approach
bool tryNewtonKajiya(vec3 cp[16], vec3 ro, vec3 rd,
                     vec3 N1, vec3 N2, float d1, float d2,
                     float tMin, float tMax,
                     float startU, float startV,
                     out float hitT, out float hitU, out float hitV, out vec3 hitN) {
    float u = startU;
    float v = startV;

    const int MAX_ITER = 12;
    const float EPSILON = 1e-5;

    for (int iter = 0; iter < MAX_ITER; iter++) {
        vec3 S = evalBezierPatch(cp, u, v);
        vec3 Su = evalBezierPatchDu(cp, u, v);
        vec3 Sv = evalBezierPatchDv(cp, u, v);

        // F(u,v) = [N1·S + d1, N2·S + d2] - point on ray iff both zero
        float f1 = dot(N1, S) + d1;
        float f2 = dot(N2, S) + d2;

        // Check convergence
        if (abs(f1) < EPSILON && abs(f2) < EPSILON) {
            // Compute t from converged surface point
            float t = dot(S - ro, rd) / dot(rd, rd);

            if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0 && t >= tMin && t <= tMax) {
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

        // 2x2 Jacobian: J = [N1·Su, N1·Sv; N2·Su, N2·Sv]
        float j11 = dot(N1, Su), j12 = dot(N1, Sv);
        float j21 = dot(N2, Su), j22 = dot(N2, Sv);

        float det = j11 * j22 - j12 * j21;
        if (abs(det) < 1e-10) return false;

        float invDet = 1.0 / det;
        float du = invDet * (j22 * f1 - j12 * f2);
        float dv = invDet * (-j21 * f1 + j11 * f2);

        u -= du;
        v -= dv;

        // Early exit if diverging far from valid domain
        if (u < -0.5 || u > 1.5 || v < -0.5 || v > 1.5) {
            return false;
        }
    }

    // Check if we got close enough after max iterations
    float fu = clamp(u, 0.0, 1.0);
    float fv = clamp(v, 0.0, 1.0);
    if (abs(u - fu) < 0.05 && abs(v - fv) < 0.05) {
        vec3 S = evalBezierPatch(cp, fu, fv);
        float f1 = dot(N1, S) + d1;
        float f2 = dot(N2, S) + d2;
        if (abs(f1) < EPSILON * 10.0 && abs(f2) < EPSILON * 10.0) {
            float t = dot(S - ro, rd) / dot(rd, rd);
            if (t >= tMin && t <= tMax) {
                hitT = t;
                hitU = fu;
                hitV = fv;
                vec3 Su = evalBezierPatchDu(cp, fu, fv);
                vec3 Sv = evalBezierPatchDv(cp, fu, fv);
                vec3 n = normalize(cross(Su, Sv));
                if (dot(n, rd) > 0.0) n = -n;
                hitN = n;
                return true;
            }
        }
    }

    return false;
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

    float bestT = tMax + 1.0;
    float bestU, bestV;
    vec3 bestN;
    bool found = false;

    // Try multiple starting points
    float startPoints[5*2] = float[](
        0.5, 0.5,   // center
        0.2, 0.2,   // corners (inset from edges)
        0.8, 0.2,
        0.2, 0.8,
        0.8, 0.8
    );

    for (int i = 0; i < 5; i++) {
        float t, u, v;
        vec3 n;
        if (tryNewtonKajiya(cp, ro, rd, N1, N2, d1, d2, tMin, bestT,
                            startPoints[i*2], startPoints[i*2+1], t, u, v, n)) {
            if (t < bestT) {
                bestT = t;
                bestU = u;
                bestV = v;
                bestN = n;
                found = true;
            }
        }
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
