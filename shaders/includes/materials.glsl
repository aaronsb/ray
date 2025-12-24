// Material utilities
// Requires: random.glsl (rand()), PI constant

// Fresnel reflectance (Schlick approximation)
float fresnel(float cosTheta, float ior) {
    float r0 = (1.0 - ior) / (1.0 + ior);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosTheta, 5.0);
}

// GGX microfacet importance sampling for rough reflections
// Returns half-vector sampled from GGX distribution
vec3 sampleGGX(vec3 N, float roughness) {
    float a = roughness * roughness;

    float r1 = rand();
    float r2 = rand();

    float phi = 2.0 * PI * r1;
    float cosTheta = sqrt((1.0 - r2) / (1.0 + (a*a - 1.0) * r2));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to cartesian (tangent space)
    vec3 H = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    // Tangent space to world space
    vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// Beer's law absorption for transparent materials
// absorptionCoeff: per-channel absorption (higher = more absorbed)
// distance: path length through medium
vec3 applyAbsorption(vec3 throughput, vec3 absorptionCoeff, float distance) {
    return throughput * exp(-absorptionCoeff * distance);
}
