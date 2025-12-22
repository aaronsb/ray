// Random number generation (PCG-based)

// PCG random number generator state
uint rngState;

// PCG hash
uint pcg_hash(uint val) {
    uint state = val * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Initialize RNG from pixel coordinates and frame number
// Note: requires pc.width and pc.height to be defined (push constants)
void initRNG(uvec2 pixel, uint frame) {
    rngState = pcg_hash(pixel.x + pixel.y * pc.width + frame * pc.width * pc.height);
}

float rand() {
    rngState = pcg_hash(rngState);
    return float(rngState) / float(0xFFFFFFFFu);
}

vec2 rand2() {
    return vec2(rand(), rand());
}

vec3 rand3() {
    return vec3(rand(), rand(), rand());
}

// Random direction in unit sphere
vec3 randomInUnitSphere() {
    vec3 p;
    do {
        p = rand3() * 2.0 - 1.0;
    } while (dot(p, p) >= 1.0);
    return p;
}

// Random unit vector (for Lambertian)
// Note: requires PI to be defined
vec3 randomUnitVector() {
    float z = rand() * 2.0 - 1.0;
    float a = rand() * 2.0 * PI;
    float r = sqrt(1.0 - z * z);
    return vec3(r * cos(a), r * sin(a), z);
}

// Random in hemisphere
vec3 randomInHemisphere(vec3 normal) {
    vec3 inUnitSphere = randomInUnitSphere();
    if (dot(inUnitSphere, normal) > 0.0)
        return inUnitSphere;
    else
        return -inUnitSphere;
}
