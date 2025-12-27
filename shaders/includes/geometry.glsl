// Geometry utilities
// Note: Ray struct should be defined by the including shader

// Floor plane intersection (horizontal plane at given y)
// Requires: Ray struct with origin and direction
bool hitFloorPlane(vec3 rayOrigin, vec3 rayDir, float y, float tMin, float tMax, out float hitT, out vec3 hitN) {
    if (abs(rayDir.y) < 0.0001) return false;

    float t = (y - rayOrigin.y) / rayDir.y;
    if (t < tMin || t > tMax) return false;

    hitT = t;
    hitN = vec3(0, 1, 0);
    return true;
}

// Checker pattern for floor
vec3 checkerPattern(vec3 p, float scale, vec3 color1, vec3 color2) {
    int ix = int(floor(p.x * scale));
    int iz = int(floor(p.z * scale));
    bool white = ((ix + iz) & 1) == 0;
    return white ? color1 : color2;
}

// Rayleigh scattering sky model
// Based on simplified atmospheric scattering with day/night support
vec3 skyGradient(vec3 dir) {
    vec3 unitDir = normalize(dir);

    // Sun direction (configurable, default afternoon sun)
    vec3 sunDir = normalize(vec3(0.4, 0.3, 0.5));

    // Rayleigh scattering coefficients (wavelength^-4 dependence)
    // Blue scatters more than red
    vec3 betaR = vec3(0.18, 0.41, 1.0);

    // Sun altitude determines overall light level
    float sunAlt = sunDir.y;
    float daylight = smoothstep(-0.1, 0.1, sunAlt);

    // View-dependent scattering
    float viewAltitude = max(unitDir.y, 0.001);
    float viewOpticalDepth = exp(-viewAltitude * 4.0) / viewAltitude;
    viewOpticalDepth = min(viewOpticalDepth, 40.0);  // Clamp for stability

    // Rayleigh phase function: 3/4 * (1 + cos^2 theta)
    float cosAngle = dot(unitDir, sunDir);
    float phase = 0.75 * (1.0 + cosAngle * cosAngle);

    // Scattered light
    vec3 scatter = (vec3(1.0) - exp(-betaR * viewOpticalDepth * 0.4)) * phase;
    vec3 skyColor = scatter * daylight * 1.5;

    // Sun glow (simplified Mie scattering)
    float sunProximity = max(0.0, cosAngle);
    float mieGlow = pow(sunProximity, 32.0) * 2.0;
    vec3 sunColor = vec3(1.0, 0.95, 0.8) * mieGlow * daylight;

    // Horizon glow
    float horizonGlow = exp(-viewAltitude * 8.0);
    vec3 horizonColor = vec3(0.7, 0.5, 0.3) * horizonGlow * 0.3 * daylight;

    return skyColor + sunColor + horizonColor;
}
