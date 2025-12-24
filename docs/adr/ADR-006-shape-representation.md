# ADR-006: Shape Representation Strategies

Status: Proposed
Date: 2025-12-23
Deciders: @aaron, @claude

## Context

We have a working path tracer with analytic primitives (sphere, box, cylinder, cone, torus). These provide mathematically perfect surfaces - exact normals, no faceting, elegant intersection math.

We want to expand to more complex shapes while preserving as much mathematical purity as possible:
- Arbitrary smooth surfaces (not just quadrics)
- CSG boolean operations (union, intersection, difference)
- Physics-compatible collision geometry
- Shapes like cut gems, organic forms, CAD models

The question: **how should arbitrary geometry be represented and rendered?**

## Shape Representation Options

### 1. Analytic Primitives (Current)

**What we have**: Sphere, box, cylinder, cone, torus with closed-form ray intersection.

| Aspect | Assessment |
|--------|------------|
| Surfaces | Mathematically perfect |
| Normals | Exact, continuous |
| CSG | Requires interval arithmetic (complex) |
| Transforms | Scale via ray transformation |
| Flexibility | Limited to specific shapes |
| Performance | Fast (direct solve) |

**Best for**: Perfect spheres, boxes, optical elements, simple scenes.

### 2. Signed Distance Functions (SDF) + Raymarching

**Concept**: Each shape defined by distance function. Raymarch to find surface.

```glsl
float sdSphere(vec3 p, float r) { return length(p) - r; }
float sdBox(vec3 p, vec3 b) { return length(max(abs(p) - b, 0.0)); }

// CSG is trivial
float opUnion(float a, float b) { return min(a, b); }
float opSubtract(float a, float b) { return max(a, -b); }
float opIntersect(float a, float b) { return max(a, b); }
```

| Aspect | Assessment |
|--------|------------|
| Surfaces | Mathematically defined |
| Normals | Computed via gradient (3 extra samples) |
| CSG | Trivial (min/max operations) |
| Transforms | Transform query point (elegant) |
| Flexibility | Any shape with distance function |
| Performance | Iterative (many steps per ray) |

**GPU challenge**: Branching on primitive type kills performance. Solution: flatten CSG tree to linear buffer, evaluate all ops uniformly.

**Best for**: CSG, procedural shapes, smooth blending.

### 3. Triangle Meshes

**Concept**: Approximate any shape with triangles. Standard approach.

| Aspect | Assessment |
|--------|------------|
| Surfaces | Piecewise planar (faceted) |
| Normals | Interpolated vertex normals (smooth shading) |
| CSG | Mesh boolean libraries exist (CGAL, Manifold) |
| Transforms | Transform vertices or use BVH |
| Flexibility | Any shape (universal) |
| Performance | Fast with BVH acceleration |

**Faceting issue**: Silhouettes show polygon edges. Smooth normals help shading but not silhouettes.

**Best for**: Imported models, baked CSG results, cut gems (flat facets are correct).

### 4. Direct NURBS/Bezier Ray Tracing

**Concept**: Intersect rays with parametric surfaces via Newton iteration.

```
Ray: O + t*D
Surface: S(u,v)
Solve: O + t*D = S(u,v)  →  find (t, u, v)
```

| Aspect | Assessment |
|--------|------------|
| Surfaces | Mathematically perfect |
| Normals | Exact (surface derivatives) |
| CSG | Complex (surface-surface intersection) |
| Transforms | Transform control points |
| Flexibility | Arbitrary smooth surfaces |
| Performance | Newton iteration per ray (moderate) |

**References**:
- [Direct ray tracing of NURBS surfaces](https://www.researchgate.net/publication/232644373_Direct_and_fast_ray_tracing_of_NURBS_surfaces)
- [Utah: Practical ray tracing of trimmed NURBS](https://www2.cs.utah.edu/gdc/publications/papers/raynurbs.pdf)

**Best for**: CAD models, mathematically precise curved surfaces.

### 5. Subdivision Surfaces with Limit Evaluation

**Concept**: Coarse control mesh defines smooth limit surface. Tessellate for BVH, but evaluate true limit surface at hit points.

| Aspect | Assessment |
|--------|------------|
| Surfaces | Smooth limit surface |
| Normals | Exact from limit evaluation |
| CSG | Mesh-based (on control mesh) |
| Transforms | Transform control mesh |
| Flexibility | Arbitrary topology |
| Performance | Good (mesh BVH + limit eval) |

**Key insight**: BVH uses triangles for acceleration, but hit point/normal come from mathematical limit surface. No visible faceting.

**References**:
- [NVIDIA OptiX 9 Mega Geometry](https://developer.nvidia.com/blog/fast-ray-tracing-of-dynamic-scenes-using-nvidia-optix-9-and-nvidia-rtx-mega-geometry/) (2025)
- [Ray Tracing of Subdivision Surfaces (Kobbelt)](https://www.graphics.rwth-aachen.de/media/papers/tight.pdf)

**Best for**: Organic shapes, animation, production rendering.

## Hybrid Approach

The emerging pattern in production: **convert between representations as needed**.

```
Design         →  Bake          →  Render
─────────────────────────────────────────────
NURBS/SubD     →  SDF           →  Raymarch
CAD model      →  Mesh          →  BVH trace
Procedural     →  Keep as SDF   →  Raymarch
Cut gem        →  Mesh          →  BVH trace (facets are correct)
Smooth sphere  →  Keep analytic →  Direct intersect
```

### Proposed Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    CPU: Scene Definition                │
├─────────────────────────────────────────────────────────┤
│  Analytic      NURBS/Bezier     SDF Tree      Meshes   │
│  Primitives    Patches          (CSG ops)     (OBJ/etc)│
└────────┬────────────┬──────────────┬────────────┬──────┘
         │            │              │            │
         ▼            ▼              ▼            ▼
┌─────────────────────────────────────────────────────────┐
│                  CPU: Bake/Compile                      │
├─────────────────────────────────────────────────────────┤
│  Keep as-is    Tessellate or    Flatten to    Build   │
│                Newton params    linear ops    BVH      │
└────────┬────────────┬──────────────┬────────────┬──────┘
         │            │              │            │
         ▼            ▼              ▼            ▼
┌─────────────────────────────────────────────────────────┐
│                    GPU: Unified Rendering               │
├─────────────────────────────────────────────────────────┤
│  hitScene() dispatches to appropriate intersection:     │
│  - Analytic: direct solve                              │
│  - NURBS: Newton iteration                             │
│  - SDF: raymarch (no branching)                        │
│  - Mesh: BVH traversal + triangle test                 │
└─────────────────────────────────────────────────────────┘
```

## Recommendations

### Keep Analytic Primitives
Perfect spheres, boxes matter for optical accuracy. Don't lose this.

### Add Triangle Mesh Support
Universal fallback. Enables:
- Imported models
- Baked CSG results
- Cut gems (where facets are correct)
- Physics collision shapes

Implementation: Möller-Trumbore intersection, BVH acceleration.

### Explore SDF for CSG
Revisit SDF raymarching with proper flattening:
- CSG tree compiled to linear op buffer on CPU
- No branching in shader - uniform evaluation
- Transforms via query point manipulation

### Consider NURBS for Precision
For CAD-like workflows where mathematical exactness matters:
- Newton-based ray-patch intersection
- Exact normals from surface derivatives
- Design in Rhino/CAD, render with precision

## Physics Integration

Any representation needs collision geometry:

| Representation | Physics Approach |
|----------------|------------------|
| Analytic | Direct (sphere, box, capsule colliders) |
| SDF | Point queries, distance field |
| Mesh | Convex decomposition, GJK/EPA |
| NURBS | Tessellate for collision |

The mesh path is most compatible with physics engines (Bullet, Jolt, PhysX).

## Decision

**No single representation is best.** The path forward:

1. **Keep analytic primitives** for optical perfection
2. **Add triangle mesh support** as universal fallback
3. **Prototype SDF CSG** with flattened evaluation (no branching)
4. **Evaluate NURBS** for CAD precision use cases

Priority order: Mesh first (most practical), then SDF CSG, then NURBS.

## Consequences

### Positive
- Flexibility to choose best representation per shape
- Analytic primitives remain mathematically perfect
- Path to arbitrary geometry (meshes)
- Path to CSG (SDF or mesh booleans)

### Negative
- Multiple code paths in renderer
- Complexity of supporting multiple representations
- Need to decide "which representation?" for each shape

### Neutral
- Physics integration favors meshes regardless
- Modern GPUs handle the complexity well
- Can start simple (mesh) and add others incrementally

## Open Questions

1. **BVH library**: Build our own or use existing (Embree, custom)?
2. **SDF branching**: Can we truly eliminate it with flattening?
3. **NURBS scope**: Full trimmed NURBS or just Bezier patches?
4. **Subdivision**: Worth the complexity for this project?

## References

- [NVIDIA OptiX 9 Mega Geometry](https://developer.nvidia.com/blog/fast-ray-tracing-of-dynamic-scenes-using-nvidia-optix-9-and-nvidia-rtx-mega-geometry/)
- [Direct ray tracing of NURBS surfaces](https://www.researchgate.net/publication/232644373_Direct_and_fast_ray_tracing_of_NURBS_surfaces)
- [Utah: Practical ray tracing of trimmed NURBS](https://www2.cs.utah.edu/gdc/publications/papers/raynurbs.pdf)
- [Ray Tracing of Subdivision Surfaces](https://www.graphics.rwth-aachen.de/media/papers/tight.pdf)
- [Inigo Quilez SDF functions](https://iquilezles.org/articles/distfunctions/)
