// ============================================================================
// sdf/csg.glsl - Constructive Solid Geometry Operations
// ============================================================================
//
// Combine SDF primitives to create complex shapes.
// All operations take two SDF distance values and return combined distance.
//
// ============================================================================

// --- Hard Boolean Operations ---

// Union: A ∪ B (either A or B)
float opUnion(float a, float b) {
    return min(a, b);
}

// Intersection: A ∩ B (both A and B)
float opIntersect(float a, float b) {
    return max(a, b);
}

// Subtraction: A - B (A but not B)
float opSubtract(float a, float b) {
    return max(a, -b);
}

// --- Smooth Boolean Operations ---
// k: controls the smoothing radius (larger = more blend)

// Smooth union - blends surfaces together
float opSmoothUnion(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// Smooth intersection - rounds the inside edge
float opSmoothIntersect(float a, float b, float k) {
    float h = clamp(0.5 - 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) + k * h * (1.0 - h);
}

// Smooth subtraction - rounds the cut edge
float opSmoothSubtract(float a, float b, float k) {
    float h = clamp(0.5 - 0.5 * (a + b) / k, 0.0, 1.0);
    return mix(a, -b, h) + k * h * (1.0 - h);
}
