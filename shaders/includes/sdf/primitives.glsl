// ============================================================================
// sdf/primitives.glsl - Signed Distance Function Primitives
// ============================================================================
//
// All primitives are centered at origin.
// Convention: negative = inside, positive = outside, zero = surface
//
// ============================================================================

// --- Sphere ---
float sdSphere(vec3 p, float radius) {
    return length(p) - radius;
}

// --- Box ---
float sdBox(vec3 p, vec3 halfExtents) {
    vec3 q = abs(p) - halfExtents;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

// --- Cylinder (Y-axis) ---
float sdCylinder(vec3 p, float radius, float halfHeight) {
    vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(radius, halfHeight);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// --- Torus (XZ plane) ---
float sdTorus(vec3 p, float majorRadius, float minorRadius) {
    vec2 q = vec2(length(p.xz) - majorRadius, p.y);
    return length(q) - minorRadius;
}

// --- Capsule (Y-axis) ---
float sdCapsule(vec3 p, float radius, float halfHeight) {
    p.y -= clamp(p.y, -halfHeight, halfHeight);
    return length(p) - radius;
}

// --- Plane ---
float sdPlane(vec3 p, vec3 normal, float offset) {
    return dot(p, normal) + offset;
}

// --- Cone (tip at origin, base at -Y) ---
float sdCone(vec3 p, float radius, float height) {
    vec2 q = vec2(length(p.xz), -p.y);
    vec2 tip = vec2(radius, height);
    vec2 w = q - tip * clamp(dot(q, tip) / dot(tip, tip), 0.0, 1.0);
    vec2 b = q - tip * vec2(clamp(q.x / tip.x, 0.0, 1.0), 1.0);
    float d = min(dot(w, w), dot(b, b));
    float s = max(q.x * height - q.y * radius, q.y - height);
    return sqrt(d) * sign(s);
}
