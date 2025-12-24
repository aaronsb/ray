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
