// ============================================================================
// csg/primitives/plane.glsl - Ray-Plane Interval (Half-Space)
// ============================================================================

// normal: plane normal (pointing into the solid)
// offset: distance from origin
Interval rayPlane(vec3 ro, vec3 rd, vec3 normal, float offset) {
    float denom = dot(rd, normal);
    if (abs(denom) < 1e-6) {
        // Ray parallel to plane
        if (dot(ro, normal) + offset < 0.0) {
            return Interval(-1e30, 1e30, normal, -normal);  // Inside
        } else {
            return EMPTY_INTERVAL;  // Outside
        }
    }

    float t = -(dot(ro, normal) + offset) / denom;

    if (denom < 0.0) {
        // Entering the half-space
        return Interval(t, 1e30, normal, -normal);
    } else {
        // Exiting the half-space
        return Interval(-1e30, t, -normal, normal);
    }
}
