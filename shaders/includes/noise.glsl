// Procedural noise functions

// Hash function for noise
vec3 hash3(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453123);
}

float hash1(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123);
}

// Perlin-style gradient noise
float gradientNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);  // smoothstep

    return mix(mix(mix(dot(hash3(i + vec3(0,0,0)) * 2.0 - 1.0, f - vec3(0,0,0)),
                       dot(hash3(i + vec3(1,0,0)) * 2.0 - 1.0, f - vec3(1,0,0)), u.x),
                   mix(dot(hash3(i + vec3(0,1,0)) * 2.0 - 1.0, f - vec3(0,1,0)),
                       dot(hash3(i + vec3(1,1,0)) * 2.0 - 1.0, f - vec3(1,1,0)), u.x), u.y),
               mix(mix(dot(hash3(i + vec3(0,0,1)) * 2.0 - 1.0, f - vec3(0,0,1)),
                       dot(hash3(i + vec3(1,0,1)) * 2.0 - 1.0, f - vec3(1,0,1)), u.x),
                   mix(dot(hash3(i + vec3(0,1,1)) * 2.0 - 1.0, f - vec3(0,1,1)),
                       dot(hash3(i + vec3(1,1,1)) * 2.0 - 1.0, f - vec3(1,1,1)), u.x), u.y), u.z);
}

// Fractal Brownian Motion - layered noise
float fbm(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for (int i = 0; i < octaves; i++) {
        value += amplitude * gradientNoise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

// Worley (cellular) noise - returns distance to nearest cell point
float worleyNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);

    float minDist = 1.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            for (int z = -1; z <= 1; z++) {
                vec3 neighbor = vec3(x, y, z);
                vec3 cellPoint = hash3(i + neighbor);
                vec3 diff = neighbor + cellPoint - f;
                float dist = length(diff);
                minDist = min(minDist, dist);
            }
        }
    }
    return minDist;
}

// Turbulence - absolute value of noise layers (sharp creases)
float turbulence(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for (int i = 0; i < octaves; i++) {
        value += amplitude * abs(gradientNoise(p * frequency));
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}
