// ============================================================================
// csg/primitives/sphere.glsl - Ray-Sphere Interval
// ============================================================================

// center: sphere center
// radius: sphere radius
Interval raySphere(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3 oc = ro - center;
    float a = dot(rd, rd);
    float b = 2.0 * dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - 4.0 * a * c;

    if (disc < 0.0) return EMPTY_INTERVAL;

    float sqrtDisc = sqrt(disc);
    float t1 = (-b - sqrtDisc) / (2.0 * a);
    float t2 = (-b + sqrtDisc) / (2.0 * a);

    vec3 p1 = ro + t1 * rd;
    vec3 p2 = ro + t2 * rd;
    vec3 n1 = normalize(p1 - center);
    vec3 n2 = normalize(p2 - center);

    return Interval(t1, t2, n1, n2);
}
