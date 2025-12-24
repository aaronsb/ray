// ============================================================================
// csg/primitives/cone.glsl - Ray-Cone Interval
// ============================================================================

// center: center of cone base
// radius: base radius
// height: cone height (apex is at center.y + height)
Interval rayCone(vec3 ro, vec3 rd, vec3 center, float radius, float height) {
    vec3 oc = ro - center;

    // Cone equation: x^2 + z^2 = (r/h)^2 * (h - y)^2
    float k = radius / height;
    float k2 = k * k;

    // Substitute ray equation
    float a = rd.x * rd.x + rd.z * rd.z - k2 * rd.y * rd.y;
    float b = 2.0 * (oc.x * rd.x + oc.z * rd.z + k2 * (height - oc.y) * rd.y);
    float c = oc.x * oc.x + oc.z * oc.z - k2 * (height - oc.y) * (height - oc.y);

    float disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return EMPTY_INTERVAL;

    // Handle degenerate case (ray parallel to cone surface)
    if (abs(a) < 1e-6) return EMPTY_INTERVAL;

    float sqrtDisc = sqrt(disc);
    float t1 = (-b - sqrtDisc) / (2.0 * a);
    float t2 = (-b + sqrtDisc) / (2.0 * a);

    if (t1 > t2) {
        float tmp = t1; t1 = t2; t2 = tmp;
    }

    // Early reject: validate t values
    if (isinf(t1) || isinf(t2) || isnan(t1) || isnan(t2)) return EMPTY_INTERVAL;
    if (t2 < 0.0) return EMPTY_INTERVAL;

    // CRITICAL: Immediately validate y-coordinates to reject double-cone artifacts
    // The quadratic gives intersections with BOTH halves of the infinite double cone
    float y1 = oc.y + t1 * rd.y;
    float y2 = oc.y + t2 * rd.y;

    // Exclude apex region (singularity) - use small epsilon
    float apexEps = 0.001;
    float maxY = height - apexEps;

    // Check if each t is in valid y range [0, maxY]
    bool t1Valid = (y1 >= 0.0 && y1 <= maxY);
    bool t2Valid = (y2 >= 0.0 && y2 <= maxY);

    // If neither intersection is valid, check for cap intersections
    if (!t1Valid && !t2Valid) {
        // Ray might enter/exit through caps only
        if (abs(rd.y) < 1e-6) return EMPTY_INTERVAL;  // Horizontal ray missing cone

        float tBase = -oc.y / rd.y;
        float tTop = (maxY - oc.y) / rd.y;

        if (tBase > tTop) {
            float tmp = tBase; tBase = tTop; tTop = tmp;
        }

        // Check if base cap hit is inside radius
        vec3 pBase = oc + tBase * rd;
        float distBase = length(vec2(pBase.x, pBase.z));
        if (distBase > radius) return EMPTY_INTERVAL;

        // Check if top cap hit is inside radius at that height
        vec3 pTop = oc + tTop * rd;
        float distTop = length(vec2(pTop.x, pTop.z));
        float radiusAtTop = radius * (1.0 - maxY / height);
        if (distTop > radiusAtTop) return EMPTY_INTERVAL;

        float tEnter = max(0.0, tBase);
        float tExit = tTop;
        if (tEnter >= tExit) return EMPTY_INTERVAL;

        vec3 nEnter = vec3(0, -1, 0);  // Base cap
        vec3 nExit = vec3(0, 1, 0);    // Top cap
        return Interval(tEnter, tExit, nEnter, nExit);
    }

    // Compute height-clipped t values
    float tCap0, tCap1;
    if (abs(rd.y) < 1e-6) {
        if (oc.y < 0.0 || oc.y > maxY) return EMPTY_INTERVAL;
        tCap0 = -1e30;
        tCap1 = 1e30;
    } else {
        tCap0 = -oc.y / rd.y;
        tCap1 = (maxY - oc.y) / rd.y;
    }

    if (tCap0 > tCap1) {
        float tmp = tCap0; tCap0 = tCap1; tCap1 = tmp;
    }

    // Final interval: intersect cone surface with height bounds
    float tEnter = max(t1, tCap0);
    float tExit = min(t2, tCap1);

    if (tEnter > tExit) return EMPTY_INTERVAL;
    if (tExit < 0.0) return EMPTY_INTERVAL;
    tEnter = max(0.0, tEnter);

    // Compute normals
    float cosAngle = height / sqrt(height * height + radius * radius);
    float sinAngle = radius / sqrt(height * height + radius * radius);

    vec3 pEnter = ro + tEnter * rd;
    float yEnter = pEnter.y - center.y;
    vec3 nEnter;

    if (abs(yEnter) < 0.01) {
        nEnter = vec3(0, -1, 0);  // Base cap
    } else if (yEnter >= maxY - 0.01) {
        nEnter = vec3(0, 1, 0);   // Top cap
    } else {
        vec2 radialXZ = vec2(pEnter.x - center.x, pEnter.z - center.z);
        float radialLen = length(radialXZ);
        if (radialLen < 1e-6) {
            nEnter = vec3(0, 1, 0);
        } else {
            vec2 radialDir = radialXZ / radialLen;
            nEnter = normalize(vec3(radialDir.x * cosAngle, sinAngle, radialDir.y * cosAngle));
        }
    }

    vec3 pExit = ro + tExit * rd;
    float yExit = pExit.y - center.y;
    vec3 nExit;

    if (abs(yExit) < 0.01) {
        nExit = vec3(0, -1, 0);  // Base cap
    } else if (yExit >= maxY - 0.01) {
        nExit = vec3(0, 1, 0);   // Top cap
    } else {
        vec2 radialXZ = vec2(pExit.x - center.x, pExit.z - center.z);
        float radialLen = length(radialXZ);
        if (radialLen < 1e-6) {
            nExit = vec3(0, 1, 0);
        } else {
            vec2 radialDir = radialXZ / radialLen;
            nExit = normalize(vec3(radialDir.x * cosAngle, sinAngle, radialDir.y * cosAngle));
        }
    }

    return Interval(tEnter, tExit, nEnter, nExit);
}
