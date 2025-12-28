// 3D spatial hashing for caustic map
// Shared between ray.comp (sampling) and caustics.comp (writing)

#ifndef SPATIAL_HASH_GLSL
#define SPATIAL_HASH_GLSL

// Must match in both shaders
const float CAUSTIC_CELL_SIZE = 0.08;  // World units per cell (~8cm)
const uint CAUSTIC_HASH_SIZE = 4194304;  // 4M cells for hash table

// Get 3D cell coordinates for a world position
ivec3 causticGetCell3D(vec3 pos) {
    return ivec3(floor(pos / CAUSTIC_CELL_SIZE));
}

// 3D spatial hash from cell coordinates
uint cellHash3D(ivec3 cell) {
    uint hash = uint(cell.x) * 73856093u ^
                uint(cell.y) * 19349663u ^
                uint(cell.z) * 83492791u;
    return hash % CAUSTIC_HASH_SIZE;
}

// Cell signature: pack cell coords for collision validation
// High bit set to distinguish from empty cells
uint cellSignature(ivec3 cell) {
    uint px = uint(cell.x + 512) & 0x3FFu;
    uint py = uint(cell.y + 512) & 0x3FFu;
    uint pz = uint(cell.z + 512) & 0x3FFu;
    return (px) | (py << 10) | (pz << 20) | 0x80000000u;
}

#endif // SPATIAL_HASH_GLSL
