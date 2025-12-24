// ============================================================================
// csg/primitives/torus.glsl - Ray-Torus Interval
// ============================================================================

// center: torus center
// majorRadius: distance from center to tube center
// minorRadius: tube radius
// Uses Newton iteration to solve quartic
Interval rayTorus(vec3 ro, vec3 rd, vec3 center, float majorRadius, float minorRadius) {
    vec3 oc = ro - center;

    // Torus: (sqrt(x^2 + z^2) - R)^2 + y^2 = r^2
    float R = majorRadius;
    float r = minorRadius;

    // Quartic coefficients
    float ox = oc.x, oy = oc.y, oz = oc.z;
    float dx = rd.x, dy = rd.y, dz = rd.z;

    float sum_d2 = dx*dx + dy*dy + dz*dz;
    float sum_od = ox*dx + oy*dy + oz*dz;
    float sum_o2 = ox*ox + oy*oy + oz*oz;
    float R2 = R * R;
    float r2 = r * r;

    float k = sum_o2 - R2 - r2;

    float a4 = sum_d2 * sum_d2;
    float a3 = 4.0 * sum_d2 * sum_od;
    float a2 = 2.0 * sum_d2 * k + 4.0 * sum_od * sum_od + 4.0 * R2 * dy * dy;
    float a1 = 4.0 * k * sum_od + 8.0 * R2 * oy * dy;
    float a0 = k * k - 4.0 * R2 * (r2 - oy * oy);

    // Find roots using Newton iteration
    float roots[4];
    int numRoots = 0;

    #define TORUS_EVAL(t) (a4*(t)*(t)*(t)*(t) + a3*(t)*(t)*(t) + a2*(t)*(t) + a1*(t) + a0)
    #define TORUS_DERIV(t) (4.0*a4*(t)*(t)*(t) + 3.0*a3*(t)*(t) + 2.0*a2*(t) + a1)

    // Multiple starting points
    float tMax = 100.0;
    for (int start = 0; start < 8; start++) {
        float t = float(start) * tMax / 8.0;

        // Newton iteration
        for (int i = 0; i < 16; i++) {
            float f = TORUS_EVAL(t);
            float df = TORUS_DERIV(t);
            if (abs(df) < 1e-10) break;
            float dt = f / df;
            t -= dt;
            if (abs(dt) < 1e-6) break;
        }

        // Validate and deduplicate roots
        if (t > 0.0 && abs(TORUS_EVAL(t)) < 1e-4) {
            bool isNew = true;
            for (int j = 0; j < numRoots; j++) {
                if (abs(roots[j] - t) < 0.01) {
                    isNew = false;
                    break;
                }
            }
            if (isNew && numRoots < 4) {
                roots[numRoots++] = t;
            }
        }
    }

    #undef TORUS_EVAL
    #undef TORUS_DERIV

    if (numRoots < 2) return EMPTY_INTERVAL;

    // Sort roots
    for (int i = 0; i < numRoots - 1; i++) {
        for (int j = i + 1; j < numRoots; j++) {
            if (roots[j] < roots[i]) {
                float tmp = roots[i];
                roots[i] = roots[j];
                roots[j] = tmp;
            }
        }
    }

    float tEnter = roots[0];
    float tExit = roots[1];

    // Compute normals (gradient of torus implicit function)
    vec3 pEnter = ro + tEnter * rd;
    vec3 pExit = ro + tExit * rd;

    vec3 nEnter, nExit;

    vec3 q1 = pEnter - center;
    float len1 = length(vec2(q1.x, q1.z));
    if (len1 < 1e-6) {
        nEnter = vec3(0, sign(q1.y), 0);  // Degenerate - point on axis
    } else {
        nEnter = normalize(vec3(q1.x * (1.0 - R/len1), q1.y, q1.z * (1.0 - R/len1)));
    }

    vec3 q2 = pExit - center;
    float len2 = length(vec2(q2.x, q2.z));
    if (len2 < 1e-6) {
        nExit = vec3(0, sign(q2.y), 0);  // Degenerate - point on axis
    } else {
        nExit = normalize(vec3(q2.x * (1.0 - R/len2), q2.y, q2.z * (1.0 - R/len2)));
    }

    return Interval(tEnter, tExit, nEnter, nExit);
}
