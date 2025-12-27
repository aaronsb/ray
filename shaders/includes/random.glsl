// Random number generation (PCG-based)
// Minimal, fast implementation

uint rngState;

uint pcgHash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand() {
    rngState = pcgHash(rngState);
    return float(rngState) / 4294967295.0;
}

// Initialize RNG from pixel coordinates
// Note: excluding frame index for stable roughness (use temporal accumulation for convergence)
void initRNG(ivec2 pixel) {
    rngState = pcgHash(uint(pixel.x) + uint(pixel.y) * 10000u);
}

// Optional: include frame for temporal variation (noisy but converging)
void initRNGTemporal(ivec2 pixel, uint frame) {
    rngState = pcgHash(uint(pixel.x) + uint(pixel.y) * 10000u + frame * 100000000u);
}

// Include camera position to vary seed when camera moves (avoids repeating patterns)
void initRNGWithCamera(ivec2 pixel, uint frame, vec3 camPos) {
    uint camHash = pcgHash(floatBitsToUint(camPos.x) ^ floatBitsToUint(camPos.y) ^ floatBitsToUint(camPos.z));
    rngState = pcgHash(uint(pixel.x) + uint(pixel.y) * 10000u + frame * 100000000u + camHash);
}

// Cosine-weighted hemisphere sampling for diffuse bounces
// Returns direction distributed with PDF proportional to cos(theta)
vec3 cosineWeightedHemisphere(vec3 normal) {
    float u1 = rand();
    float u2 = rand();

    // Cosine-weighted point on hemisphere (tangent space)
    float r = sqrt(u1);
    float theta = 2.0 * 3.14159265 * u2;
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(1.0 - u1);  // cos(theta) = sqrt(1-u1) for cosine weighting

    // Build orthonormal basis from normal
    vec3 up = abs(normal.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    // Transform to world space
    return normalize(tangent * x + bitangent * y + normal * z);
}
