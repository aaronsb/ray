// ============================================================================
// csg/primitives/polygon.glsl - Ray-Polygon Interval
// ============================================================================
//
// Polygons in CSG are typically used as:
//   1. Planar polygons (2D shape extruded to prism)
//   2. Thin planar surfaces
//
// For now, we provide a triangle primitive. Complex polygons
// can be built from triangles or using half-space intersections.
//
// ============================================================================

// Triangle (infinitely thin)
// v0, v1, v2: triangle vertices (counter-clockwise for front face)
Interval rayTriangle(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2) {
    vec3 e1 = v1 - v0;
    vec3 e2 = v2 - v0;
    vec3 normal = normalize(cross(e1, e2));

    // Moller-Trumbore intersection
    vec3 h = cross(rd, e2);
    float a = dot(e1, h);

    if (abs(a) < 1e-6) return EMPTY_INTERVAL;

    float f = 1.0 / a;
    vec3 s = ro - v0;
    float u = f * dot(s, h);

    if (u < 0.0 || u > 1.0) return EMPTY_INTERVAL;

    vec3 q = cross(s, e1);
    float v = f * dot(rd, q);

    if (v < 0.0 || u + v > 1.0) return EMPTY_INTERVAL;

    float t = f * dot(e2, q);

    if (t < 0.0) return EMPTY_INTERVAL;

    // Triangle is infinitely thin - use tiny thickness for CSG
    float epsilon = 0.0001;
    vec3 n = (dot(rd, normal) < 0.0) ? normal : -normal;

    return Interval(t - epsilon, t + epsilon, n, -n);
}

// Prism (triangle extruded along Y-axis)
// v0, v1, v2: base triangle vertices (in XZ plane, at y=0)
// height: extrusion height
Interval rayPrism(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2, float height) {
    // The prism is the intersection of:
    //   - Three half-spaces (the three sides)
    //   - Two half-spaces (top and bottom caps)

    // Start with full ray
    Interval result = Interval(-1e30, 1e30, vec3(0), vec3(0));

    // Bottom cap (y = 0, normal pointing down into solid)
    Interval bottom = rayPlane(ro, rd, vec3(0, -1, 0), 0.0);
    result = opIntersect(result, bottom);
    if (isEmpty(result)) return EMPTY_INTERVAL;

    // Top cap (y = height, normal pointing up into solid)
    Interval top = rayPlane(ro, rd, vec3(0, 1, 0), -height);
    result = opIntersect(result, top);
    if (isEmpty(result)) return EMPTY_INTERVAL;

    // Three side faces
    vec3 verts[3] = vec3[3](v0, v1, v2);
    for (int i = 0; i < 3; i++) {
        vec3 a = verts[i];
        vec3 b = verts[(i + 1) % 3];

        // Edge direction in XZ plane
        vec3 edge = b - a;
        // Outward normal in XZ plane (perpendicular to edge)
        vec3 normal = normalize(vec3(-edge.z, 0, edge.x));

        // Half-space: normal pointing inward
        float offset = -dot(normal, a);
        Interval side = rayPlane(ro, rd, -normal, -offset);
        result = opIntersect(result, side);
        if (isEmpty(result)) return EMPTY_INTERVAL;
    }

    return result;
}
