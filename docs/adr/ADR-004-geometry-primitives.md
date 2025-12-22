# ADR-004: Geometry Primitives

Status: Accepted
Date: 2025-12-21
Deciders: @aaron, @claude

## Context

The path tracer needs to support various geometry types for rendering. Currently we have:
- Spheres (analytic intersection)
- Axis-aligned boxes (analytic intersection)

We want to expand geometry support while keeping the renderer performant and the codebase hackable. Key considerations:
- Analytic primitives are fast but limited in shape
- Triangle meshes are flexible but need acceleration structures (BVH)
- Voxel chunks offer a middle ground with interesting properties

## Decision

Support three tiers of geometry primitives:

### Tier 1: Analytic Primitives (Current + Near-term)

Fast, exact intersections via closed-form math. No acceleration structure needed.

| Primitive | Status | Notes |
|-----------|--------|-------|
| **Sphere** | ✅ Implemented | Quadratic intersection |
| **Box (AABB)** | ✅ Implemented | Slab method |
| **Plane** | ✅ Implemented | Ground plane with precision fix |
| **Cylinder** | ✅ Implemented | Quadratic + caps |
| **Cone** | ✅ Implemented | Quadratic + base cap |
| **Torus** | Planned | Quartic (can use Newton iteration) |
| **Disk** | Planned | Plane + radius check |

```cpp
class Sphere : public Geometry {
    vec3 center;
    float radius;
    uint32_t materialId;
};

class Cylinder : public Geometry {
    vec3 base;
    vec3 axis;      // normalized direction
    float radius;
    float height;
    uint32_t materialId;
};
```

### Tier 2: Voxel Chunks (Target Primitive)

Discrete volume data with optional smooth surface reconstruction.

```cpp
class VoxelChunk : public Geometry {
    static constexpr uint32_t SIZE = 32;  // 32x32x32

    vec3 origin;
    float voxelSize;

    // Density field (0 = empty, 255 = solid)
    // Can interpolate for smooth surfaces
    uint8_t density[SIZE][SIZE][SIZE];

    // Per-voxel material (or palette index)
    uint8_t material[SIZE][SIZE][SIZE];
};
```

**Intersection**: DDA (Digital Differential Analyzer) raymarching through the grid.

**Smooth surfaces**: Trilinear interpolation of density values, then:
- Treat interpolated density as SDF (find zero-crossing)
- Compute normals via central differences of density gradient

**Why voxels?**
- Natural GPU parallelism (uniform grid)
- Bounded complexity (always 32³ regardless of shape)
- Can represent arbitrary topology
- Procedural generation friendly (noise, metaballs, etc.)
- Smooth curves between voxels via interpolation

### Tier 3: Triangle Meshes (Future)

Arbitrary geometry loaded from files (OBJ, glTF, etc.).

```cpp
class TriangleMesh : public Geometry {
    std::vector<vec3> vertices;
    std::vector<vec3> normals;
    std::vector<vec2> uvs;
    std::vector<uint32_t> indices;

    // GPU-side BVH
    BVH bvh;
};
```

**Requires**: BVH acceleration structure (significant implementation effort).

### CSG Operations (Future Consideration)

Constructive Solid Geometry on analytic primitives:
- Union, Intersection, Difference
- Evaluated during intersection (no mesh generation)
- Natural fit for analytic primitives

```cpp
class CSGNode : public Geometry {
    enum Op { Union, Intersection, Difference };
    Op operation;
    std::shared_ptr<Geometry> left;
    std::shared_ptr<Geometry> right;
};
```

## Shader Interface

All geometry types share a common intersection interface:

```glsl
struct HitRecord {
    float t;          // ray parameter
    vec3 point;       // hit position
    vec3 normal;      // surface normal
    uint materialId;  // material index
    bool frontFace;   // hit front or back
};

// Dispatch based on geometry type
bool intersect(Ray ray, out HitRecord rec) {
    // Test each primitive type's buffer
    // Return closest hit
}
```

## Implementation Priority

1. **Phase 1**: Refactor existing Sphere/Box into Geometry base class
2. **Phase 2**: Add Cylinder, Cone (useful for spotlights, gobos)
3. **Phase 3**: Implement VoxelChunk with DDA + smooth normals
4. **Phase 4**: Add CSG for analytic primitives
5. **Phase 5**: Triangle meshes + BVH (major effort)

## Consequences

### Positive
- Clear taxonomy of geometry types
- Analytic primitives stay fast
- Voxels provide flexibility without BVH complexity
- Path toward arbitrary meshes

### Negative
- Multiple intersection code paths in shader
- Voxel memory can be large (32KB per 32³ chunk)
- BVH is significant implementation effort

### Neutral
- May want LOD for voxels (octree vs flat grid)
- Voxel chunk size is tunable (16³, 32³, 64³)

## Related

- ADR-003: Modular Architecture (geometry module structure)

## Open Questions

- Voxel chunk size: 16³ (4KB) vs 32³ (32KB) vs 64³ (256KB)?
- Smooth voxels: trilinear vs dual contouring vs marching cubes?
- Should voxel materials be per-voxel or palette-indexed?
- BVH construction: CPU or GPU? SAH or LBVH?
