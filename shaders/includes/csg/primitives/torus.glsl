// ============================================================================
// csg/primitives/torus.glsl - Ray-Torus Interval
// ============================================================================

// Torus SDF
float torusSDF(vec3 p, float R, float r) {
    vec2 q = vec2(length(p.xz) - R, p.y);
    return length(q) - r;
}

// center: torus center
// majorRadius: distance from center to tube center
// minorRadius: tube radius
// Uses sphere tracing - guaranteed to find surface
Interval rayTorus(vec3 ro, vec3 rd, vec3 center, float majorRadius, float minorRadius) {
    float R = majorRadius;
    float r = minorRadius;

    vec3 O = ro - center;
    vec3 D = rd;

    // Bounding sphere check
    float boundR = R + r;
    float aDot = dot(D, D);
    float bDot = 2.0 * dot(O, D);
    float cDot = dot(O, O) - boundR * boundR;
    float disc = bDot * bDot - 4.0 * aDot * cDot;

    if (disc < 0.0) return EMPTY_INTERVAL;

    float sqrtDisc = sqrt(disc);
    float t0 = (-bDot - sqrtDisc) / (2.0 * aDot);
    float t1 = (-bDot + sqrtDisc) / (2.0 * aDot);

    if (t1 < 0.001) return EMPTY_INTERVAL;

    float tStart = max(t0, 0.001);
    float tEnd = t1;

    // Sphere trace to find entry point
    float tEnter = -1.0;
    float t = tStart;

    for (int i = 0; i < 128; i++) {
        vec3 p = O + t * D;
        float d = torusSDF(p, R, r);

        if (d < 0.0001) {
            tEnter = t;
            break;
        }

        t += d;
        if (t > tEnd) break;
    }

    if (tEnter < 0.0) return EMPTY_INTERVAL;

    // Find exit by sphere tracing from the far side backwards
    // Start from where ray exits bounding sphere, trace backwards
    float tExit = -1.0;
    t = tEnd;

    for (int i = 0; i < 128; i++) {
        vec3 p = O + t * D;
        float d = torusSDF(p, R, r);

        if (d < 0.0001) {
            tExit = t;
            break;
        }

        t -= d;  // Step backwards
        if (t <= tEnter + 0.001) break;
    }

    // If backwards trace didn't find exit, the ray might exit through the hole
    // In that case, march forward to find where we exit
    if (tExit < 0.0 || tExit <= tEnter) {
        t = tEnter + 0.0001;
        for (int i = 0; i < 256; i++) {
            vec3 p = O + t * D;
            float d = torusSDF(p, R, r);

            if (d > 0.0) {
                // Bisect to find exact exit
                float lo = tEnter;
                float hi = t;
                for (int j = 0; j < 20; j++) {
                    float mid = 0.5 * (lo + hi);
                    float dm = torusSDF(O + mid * D, R, r);
                    if (dm < 0.0) lo = mid;
                    else hi = mid;
                }
                tExit = 0.5 * (lo + hi);
                break;
            }

            // Use negative SDF as step (distance to nearest surface when inside)
            t += max(-d, 0.0001);
            if (t > tEnd) break;
        }
    }

    if (tExit < 0.0 || tExit <= tEnter) return EMPTY_INTERVAL;

    // Compute normals (gradient of torus implicit function)
    vec3 pEnter = O + tEnter * D;
    vec3 pExit = O + tExit * D;

    vec3 nEnter, nExit;

    float sqrtXZ1 = sqrt(pEnter.x * pEnter.x + pEnter.z * pEnter.z + 1e-8);
    nEnter = normalize(vec3(
        (sqrtXZ1 - R) * pEnter.x / sqrtXZ1,
        pEnter.y,
        (sqrtXZ1 - R) * pEnter.z / sqrtXZ1
    ));

    float sqrtXZ2 = sqrt(pExit.x * pExit.x + pExit.z * pExit.z + 1e-8);
    nExit = normalize(vec3(
        (sqrtXZ2 - R) * pExit.x / sqrtXZ2,
        pExit.y,
        (sqrtXZ2 - R) * pExit.z / sqrtXZ2
    ));

    return Interval(tEnter, tExit, nEnter, nExit);
}
