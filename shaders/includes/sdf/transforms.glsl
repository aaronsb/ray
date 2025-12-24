// ============================================================================
// sdf/transforms.glsl - Transform Operations for SDFs
// ============================================================================
//
// Apply to query point BEFORE evaluating the SDF.
// Example: sdSphere(opTranslate(p, vec3(1,0,0)), 0.5)
//
// ============================================================================

// --- Translation ---
vec3 opTranslate(vec3 p, vec3 offset) {
    return p - offset;
}

// --- Rotation around X axis ---
vec3 opRotateX(vec3 p, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec3(p.x, c * p.y - s * p.z, s * p.y + c * p.z);
}

// --- Rotation around Y axis ---
vec3 opRotateY(vec3 p, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

// --- Rotation around Z axis ---
vec3 opRotateZ(vec3 p, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

// --- Uniform Scale ---
// Usage: sdSphere(p / scale, radius) * scale
// Note: Non-uniform scale distorts distances - use with care
