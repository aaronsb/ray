// ============================================================================
// csg/primitives/cylinder.glsl - Ray-Cylinder Interval
// ============================================================================

// center: center of cylinder base
// radius: cylinder radius
// height: cylinder height
Interval rayCylinder(vec3 ro, vec3 rd, vec3 center, float radius, float height) {
    // Transform to cylinder local space (base at origin)
    vec3 oc = ro - center;

    // Infinite cylinder (XZ plane)
    float a = rd.x * rd.x + rd.z * rd.z;
    float b = 2.0 * (oc.x * rd.x + oc.z * rd.z);
    float c = oc.x * oc.x + oc.z * oc.z - radius * radius;

    float disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return EMPTY_INTERVAL;

    float sqrtDisc = sqrt(disc);
    float t1 = (-b - sqrtDisc) / (2.0 * a);
    float t2 = (-b + sqrtDisc) / (2.0 * a);

    // Check caps
    float tCap0 = (0.0 - oc.y) / rd.y;      // Bottom cap
    float tCap1 = (height - oc.y) / rd.y;   // Top cap

    if (tCap0 > tCap1) {
        float tmp = tCap0; tCap0 = tCap1; tCap1 = tmp;
    }

    // Intersect cylinder body with cap range
    float tEnter = max(t1, tCap0);
    float tExit = min(t2, tCap1);

    if (tEnter > tExit) return EMPTY_INTERVAL;

    // Compute normals
    vec3 nEnter, nExit;

    vec3 pEnter = ro + tEnter * rd;
    if (abs(pEnter.y - center.y) < 0.001) {
        nEnter = vec3(0, -1, 0);
    } else if (abs(pEnter.y - center.y - height) < 0.001) {
        nEnter = vec3(0, 1, 0);
    } else {
        nEnter = normalize(vec3(pEnter.x - center.x, 0, pEnter.z - center.z));
    }

    vec3 pExit = ro + tExit * rd;
    if (abs(pExit.y - center.y) < 0.001) {
        nExit = vec3(0, -1, 0);
    } else if (abs(pExit.y - center.y - height) < 0.001) {
        nExit = vec3(0, 1, 0);
    } else {
        nExit = normalize(vec3(pExit.x - center.x, 0, pExit.z - center.z));
    }

    return Interval(tEnter, tExit, nEnter, nExit);
}
