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

// Rayleigh scattering sky model with visible sun disc
// Based on simplified atmospheric scattering with day/night support
// sunDir: normalized direction TO the sun
// sunColor: sun light color
// sunIntensity: sun brightness multiplier
// sunAngularRadius: angular radius of sun disc in radians (~0.009 for real sun)
vec3 skyGradientWithSun(vec3 dir, vec3 sunDir, vec3 sunColor, float sunIntensity, float sunAngularRadius) {
    vec3 unitDir = normalize(dir);
    sunDir = normalize(sunDir);

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

    // Scattered light (tinted by sun color for atmosphere effects)
    vec3 scatter = (vec3(1.0) - exp(-betaR * viewOpticalDepth * 0.4)) * phase;
    vec3 skyCol = scatter * daylight * sunIntensity * 1.5;

    // Sun disc rendering - sharp edge with limb darkening
    float angularDist = acos(clamp(cosAngle, -1.0, 1.0));
    float sunDisc = 0.0;
    if (angularDist < sunAngularRadius * 1.5) {
        // Limb darkening: center is brighter than edge
        float u = angularDist / sunAngularRadius;
        if (u < 1.0) {
            // Quadratic limb darkening
            float limbDarkening = 1.0 - 0.6 * u * u;
            sunDisc = limbDarkening * 50.0 * sunIntensity;  // Bright sun
        } else {
            // Corona/glow falloff outside disc
            sunDisc = exp(-(u - 1.0) * 8.0) * 5.0 * sunIntensity;
        }
    }
    vec3 sunDiscColor = sunColor * sunDisc * daylight;

    // Horizon glow (warm tones at low sun angles)
    float horizonGlow = exp(-viewAltitude * 8.0);
    float sunsetFactor = smoothstep(0.3, -0.1, sunAlt);  // More glow at sunset
    vec3 horizonColor = mix(vec3(0.5, 0.4, 0.3), vec3(0.9, 0.4, 0.2), sunsetFactor)
                       * horizonGlow * 0.4 * daylight * sunIntensity;

    return skyCol + sunDiscColor + horizonColor;
}

// Convenience wrapper using global light buffer
// Requires: lights[] buffer and pc.sunAngularRadius to be defined
vec3 skyGradient(vec3 dir) {
    Light sun = lights[0];
    return skyGradientWithSun(dir, sun.direction, sun.color, sun.intensity, pc.sunAngularRadius);
}
