// ============================================================================
// csg/primitives/box.glsl - Ray-Box Interval (AABB)
// ============================================================================

// center: box center
// halfExtents: half-size in each dimension
Interval rayBox(vec3 ro, vec3 rd, vec3 center, vec3 halfExtents) {
    vec3 m = 1.0 / rd;
    vec3 n = m * (ro - center);
    vec3 k = abs(m) * halfExtents;

    vec3 t1 = -n - k;
    vec3 t2 = -n + k;

    float tEnter = max(max(t1.x, t1.y), t1.z);
    float tExit = min(min(t2.x, t2.y), t2.z);

    if (tEnter > tExit) return EMPTY_INTERVAL;

    // Determine which face we hit
    vec3 nEnter, nExit;
    if (t1.x > t1.y && t1.x > t1.z) {
        nEnter = vec3(-sign(rd.x), 0, 0);
    } else if (t1.y > t1.z) {
        nEnter = vec3(0, -sign(rd.y), 0);
    } else {
        nEnter = vec3(0, 0, -sign(rd.z));
    }

    if (t2.x < t2.y && t2.x < t2.z) {
        nExit = vec3(sign(rd.x), 0, 0);
    } else if (t2.y < t2.z) {
        nExit = vec3(0, sign(rd.y), 0);
    } else {
        nExit = vec3(0, 0, sign(rd.z));
    }

    return Interval(tEnter, tExit, nEnter, nExit);
}
