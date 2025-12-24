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
    float b = 2.0 * (oc.x * rd.x + oc.z * rd.z - k2 * (height - oc.y) * (-rd.y));
    float c = oc.x * oc.x + oc.z * oc.z - k2 * (height - oc.y) * (height - oc.y);

    float disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return EMPTY_INTERVAL;

    float sqrtDisc = sqrt(disc);
    float t1 = (-b - sqrtDisc) / (2.0 * a);
    float t2 = (-b + sqrtDisc) / (2.0 * a);

    if (t1 > t2) {
        float tmp = t1; t1 = t2; t2 = tmp;
    }

    // Clip to valid height range [0, height]
    float tCap0 = (0.0 - oc.y) / rd.y;      // Base cap
    float tCap1 = (height - oc.y) / rd.y;   // Apex

    if (tCap0 > tCap1) {
        float tmp = tCap0; tCap0 = tCap1; tCap1 = tmp;
    }

    // Intersect cone body with height range
    float tEnter = max(t1, tCap0);
    float tExit = min(t2, tCap1);

    if (tEnter > tExit) return EMPTY_INTERVAL;

    // Compute normals
    vec3 nEnter, nExit;

    vec3 pEnter = ro + tEnter * rd;
    float yEnter = pEnter.y - center.y;
    if (abs(yEnter) < 0.001) {
        nEnter = vec3(0, -1, 0);  // Base cap
    } else {
        // Cone surface normal
        vec3 radial = normalize(vec3(pEnter.x - center.x, 0, pEnter.z - center.z));
        float slope = radius / height;
        nEnter = normalize(vec3(radial.x, slope, radial.z));
    }

    vec3 pExit = ro + tExit * rd;
    float yExit = pExit.y - center.y;
    if (abs(yExit) < 0.001) {
        nExit = vec3(0, -1, 0);  // Base cap
    } else {
        vec3 radial = normalize(vec3(pExit.x - center.x, 0, pExit.z - center.z));
        float slope = radius / height;
        nExit = normalize(vec3(radial.x, slope, radial.z));
    }

    return Interval(tEnter, tExit, nEnter, nExit);
}
