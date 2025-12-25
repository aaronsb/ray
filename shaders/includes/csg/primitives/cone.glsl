// ============================================================================
// csg/primitives/cone.glsl - Ray-Cone Interval
// ============================================================================

// center: center of cone base
// radius: base radius
// height: cone height (apex is at center.y + height)
Interval rayCone(vec3 ro, vec3 rd, vec3 center, float radius, float height) {
    vec3 oc = ro - center;

    // Cone surface: x^2 + z^2 = (r/h)^2 * (h - y)^2
    float k = radius / height;
    float k2 = k * k;

    float a = rd.x * rd.x + rd.z * rd.z - k2 * rd.y * rd.y;
    float b = 2.0 * (oc.x * rd.x + oc.z * rd.z + k2 * (height - oc.y) * rd.y);
    float c = oc.x * oc.x + oc.z * oc.z - k2 * (height - oc.y) * (height - oc.y);

    float disc = b * b - 4.0 * a * c;

    // Collect valid t values (cone surface + base cap)
    float tCone1 = 1e30, tCone2 = 1e30;
    float tBase = 1e30;

    // Base cap intersection
    if (abs(rd.y) > 1e-6) {
        float t = -oc.y / rd.y;
        if (t > 1e-6) {
            vec3 p = oc + t * rd;
            if (p.x*p.x + p.z*p.z <= radius*radius) {
                tBase = t;
            }
        }
    }

    // Cone surface intersections
    if (disc >= 0.0 && abs(a) > 1e-6) {
        float sqrtDisc = sqrt(disc);
        float t1 = (-b - sqrtDisc) / (2.0 * a);
        float t2 = (-b + sqrtDisc) / (2.0 * a);

        // Validate each cone hit BEFORE using it
        // Valid means: t > 0, y in [0, height), and on correct nappe
        for (int i = 0; i < 2; i++) {
            float t = (i == 0) ? t1 : t2;
            if (t > 1e-6 && !isinf(t) && !isnan(t)) {
                vec3 p = oc + t * rd;

                // Double-cone check: (p - apex) · V > 0
                // apex = (0, height, 0), V = (0, -1, 0)
                // (p - apex) · V = -(p.y - height) = height - p.y
                float vDotP = height - p.y;

                // Also verify point is on cone surface: r = k*(h-y)
                float r = length(vec2(p.x, p.z));
                float expectedR = k * (height - p.y);

                if (vDotP >= -0.01 && p.y >= -0.01 && p.y <= height + 0.01 && abs(r - expectedR) < 0.1) {
                    if (tCone1 > 1e20) tCone1 = t;
                    else tCone2 = t;
                }
            }
        }
    }

    // Sort cone hits
    if (tCone1 > tCone2) {
        float tmp = tCone1; tCone1 = tCone2; tCone2 = tmp;
    }

    // Determine entry and exit from valid hits
    float tEnter = 1e30, tExit = -1e30;
    int enterType = -1, exitType = -1;  // 0=cone, 1=base

    // Consider all combinations
    if (tCone1 < 1e20 && tCone2 < 1e20) {
        // Two cone surface hits
        tEnter = tCone1;
        tExit = tCone2;
        enterType = 0;
        exitType = 0;
    } else if (tCone1 < 1e20 && tBase < 1e20) {
        // One cone + base cap
        if (tCone1 < tBase) {
            tEnter = tCone1; enterType = 0;
            tExit = tBase; exitType = 1;
        } else {
            tEnter = tBase; enterType = 1;
            tExit = tCone1; exitType = 0;
        }
    } else if (tBase < 1e20) {
        // Only base cap hit - ray might be going through apex region
        // Check if ray passes through cone interior (apex to base)
        if (abs(rd.y) > 1e-6) {
            float tApex = (height - oc.y) / rd.y;  // t at apex height
            if (tApex > 0.0 && tApex < tBase) {
                // Ray enters at apex height, exits at base
                vec3 pApex = oc + tApex * rd;
                // Check if apex entry point is near the axis (inside cone)
                float distFromAxis = length(vec2(pApex.x, pApex.z));
                if (distFromAxis < 0.1) {  // Near apex
                    tEnter = tApex;
                    tExit = tBase;
                    enterType = 0;  // Treat as cone surface (apex)
                    exitType = 1;   // Base cap
                } else {
                    return EMPTY_INTERVAL;
                }
            } else {
                return EMPTY_INTERVAL;
            }
        } else {
            return EMPTY_INTERVAL;
        }
    } else {
        return EMPTY_INTERVAL;
    }

    if (tEnter >= tExit || tExit < 0.0) return EMPTY_INTERVAL;
    tEnter = max(0.0, tEnter);

    // Compute normals
    float cosAngle = height / sqrt(height * height + radius * radius);
    float sinAngle = radius / sqrt(height * height + radius * radius);

    vec3 pEnter = oc + tEnter * rd;
    vec3 pExit = oc + tExit * rd;

    vec3 nEnter, nExit;

    if (enterType == 1) {
        nEnter = vec3(0, -1, 0);  // Base cap
    } else {
        float radialLen = length(vec2(pEnter.x, pEnter.z));
        if (radialLen < 1e-6) {
            nEnter = vec3(0, 1, 0);
        } else {
            vec2 radialDir = vec2(pEnter.x, pEnter.z) / radialLen;
            nEnter = normalize(vec3(radialDir.x * cosAngle, sinAngle, radialDir.y * cosAngle));
        }
    }

    if (exitType == 1) {
        nExit = vec3(0, -1, 0);  // Base cap
    } else {
        float radialLen = length(vec2(pExit.x, pExit.z));
        if (radialLen < 1e-6) {
            nExit = vec3(0, 1, 0);
        } else {
            vec2 radialDir = vec2(pExit.x, pExit.z) / radialLen;
            nExit = normalize(vec3(radialDir.x * cosAngle, sinAngle, radialDir.y * cosAngle));
        }
    }

    return Interval(tEnter, tExit, nEnter, nExit);
}

// X-axis aligned cone (for testing) - apex points in +X direction
Interval rayConeX(vec3 ro, vec3 rd, vec3 center, float radius, float height) {
    vec3 roSwap = vec3(ro.y, ro.x, ro.z);
    vec3 rdSwap = vec3(rd.y, rd.x, rd.z);
    vec3 centerSwap = vec3(center.y, center.x, center.z);

    Interval result = rayCone(roSwap, rdSwap, centerSwap, radius, height);
    if (result.tEnter >= result.tExit) return result;

    result.nEnter = vec3(result.nEnter.y, result.nEnter.x, result.nEnter.z);
    result.nExit = vec3(result.nExit.y, result.nExit.x, result.nExit.z);
    return result;
}
