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

// Sky/environment color (simple gradient)
vec3 skyGradient(vec3 dir) {
    float t = 0.5 * (dir.y + 1.0);
    return mix(vec3(1.0), vec3(0.5, 0.7, 1.0), t);
}
