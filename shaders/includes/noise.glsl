// Procedural noise functions
// Minimal implementation for bump mapping

float hash3D(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yxz + 19.19);
    return fract((p.x + p.y) * p.z);
}

float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);  // Smoothstep

    return mix(
        mix(mix(hash3D(i + vec3(0,0,0)), hash3D(i + vec3(1,0,0)), f.x),
            mix(hash3D(i + vec3(0,1,0)), hash3D(i + vec3(1,1,0)), f.x), f.y),
        mix(mix(hash3D(i + vec3(0,0,1)), hash3D(i + vec3(1,0,1)), f.x),
            mix(hash3D(i + vec3(0,1,1)), hash3D(i + vec3(1,1,1)), f.x), f.y),
        f.z);
}

// Compute perturbed normal from noise gradient (bump mapping)
// scale: noise frequency, strength: bump intensity
vec3 bumpNormal(vec3 p, vec3 n, float scale, float strength) {
    float eps = 0.01;
    float nx = noise3D(p * scale + vec3(eps, 0, 0)) - noise3D(p * scale - vec3(eps, 0, 0));
    float ny = noise3D(p * scale + vec3(0, eps, 0)) - noise3D(p * scale - vec3(0, eps, 0));
    float nz = noise3D(p * scale + vec3(0, 0, eps)) - noise3D(p * scale - vec3(0, 0, eps));

    vec3 grad = vec3(nx, ny, nz) * strength;
    return normalize(n + grad - n * dot(n, grad));
}
