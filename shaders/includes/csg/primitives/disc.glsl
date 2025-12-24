// ============================================================================
// csg/primitives/disc.glsl - Ray-Disc Interval
// ============================================================================

// center: disc center
// radius: disc radius
// normal: disc facing direction
Interval rayDisc(vec3 ro, vec3 rd, vec3 center, float radius, vec3 normal) {
    float denom = dot(rd, normal);
    if (abs(denom) < 1e-6) return EMPTY_INTERVAL;

    float t = dot(center - ro, normal) / denom;
    if (t < 0.0) return EMPTY_INTERVAL;

    // Check if hit point is within radius
    vec3 p = ro + t * rd;
    if (length(p - center) > radius) return EMPTY_INTERVAL;

    // Disc is infinitely thin - use tiny thickness for CSG
    float epsilon = 0.0001;
    vec3 n = (denom < 0.0) ? normal : -normal;

    return Interval(t - epsilon, t + epsilon, n, -n);
}
