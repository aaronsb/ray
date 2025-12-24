# ADR-008: Parametric Primitives Library

Status: Accepted
Date: 2025-12-24
Deciders: @aaron, @claude

## Context

ADR-006 proposed multiple shape representation strategies including direct NURBS/Bezier ray tracing. We have now **implemented** Bezier patch ray tracing successfully with the Utah teapot:

- **32 original patches** → 2000+ subdivided sub-patches
- **Performance:** 93 fps (800x600), 45 fps (7680x2160 ultrawide)
- **Technique:** Newton-Raphson + De Casteljau subdivision + BVH acceleration

This validates the NURBS/Bezier approach from ADR-006. Now we need a clean architecture to integrate parametric surfaces alongside existing analytic primitives.

### Current Organization Issues

All primitives are flat in `geometry.h`, all intersection code in `geometry.glsl`. This makes it difficult to:
- Add new primitive types without touching multiple files
- Reuse primitives across different renderers
- Maintain clear ownership of each primitive's implementation
- Integrate Bezier patches (currently isolated in `src/apps/teapot/`)

## Decision

Treat all shapes (analytic and parametric) as members of a unified "parametric primitives" library with clear separation:

### Proposed Structure

```
src/parametric/
├── primitive.h              # Common types (Ray, HitRecord, AABB)
├── sphere/
│   └── sphere.h            # Sphere struct + CPU utilities
├── box/
│   └── box.h
├── cylinder/
│   └── cylinder.h
├── cone/
│   └── cone.h
├── torus/
│   └── torus.h
└── bezier/
    ├── patch.h             # BezierPatch struct
    ├── subdivision.h       # De Casteljau subdivision
    └── bvh.h               # BVH acceleration

shaders/includes/primitives/
├── common.glsl             # Ray, HitRecord, AABB intersection
├── sphere.glsl
├── box.glsl
├── cylinder.glsl
├── cone.glsl
├── torus.glsl
├── bezier.glsl             # Newton-Raphson intersection
└── dispatch.glsl           # hitScene() aggregator
```

### Key Principle

All shapes are "parametric" in the sense that they define surfaces mathematically:
- **Analytic:** Implicit equations (sphere: |p-c|² = r²)
- **Parametric:** Explicit equations (Bezier: S(u,v) = Σ Bᵢⱼ(u,v)·Pᵢⱼ)

Both yield exact surfaces and exact normals - no tessellation artifacts.

## Implementation: Bezier Patch Ray Tracing

The Utah teapot demonstrates the complete pipeline:

### CPU Preprocessing
1. **De Casteljau subdivision** (`bezier_subdiv.h`)
   - Recursively subdivides patches until "flat enough"
   - Flatness threshold: 0.05 (AABB diagonal)
   - Max depth: 4 levels (256x subdivision per patch max)

2. **BVH construction** (`bvh.h`)
   - Median-split SAH-style BVH
   - Max 4 patches per leaf node
   - 64-byte aligned nodes for GPU

### GPU Ray Tracing
1. **BVH traversal** - stack-based, no recursion
2. **Newton-Raphson intersection** (`bezier.glsl`)
   - Kajiya's 2-plane formulation: reduces (u,v,t) to (u,v)
   - 10 iterations with ε = 1e-5 convergence
   - Single starting point (0.5, 0.5) - sufficient for well-subdivided patches

### Performance Insight

**Single starting point is the key optimization:**

| Approach | Iterations/test | FPS |
|----------|-----------------|-----|
| 5 starting points | 50 (5×10) | 16 fps |
| 1 starting point | 10 | 93 fps |

Well-subdivided patches are nearly flat, so Newton converges reliably from the center. The CPU subdivision does the heavy lifting; the GPU can use simple iteration.

## Consequences

### Positive
- Each primitive is self-contained and testable
- Easy to add new parametric types (NURBS, subdivision surfaces)
- Shader code mirrors C++ structure
- Bezier patches integrate naturally alongside analytic primitives
- Clear path to instancing (same patch data, multiple transforms)

### Negative
- More files to navigate
- Need careful include management in shaders
- Refactoring existing code requires updates in multiple locations

### Neutral
- Scene class still aggregates all primitives
- Renderer still manages all GPU buffers
- No immediate performance impact (structural change only)

## Future Work

1. **Refactor existing primitives** into `src/parametric/` structure
2. **Move teapot code** from `src/apps/teapot/` to `src/parametric/bezier/`
3. **Integrate into main renderer** - add Bezier buffers to `raytrace.comp`
4. **Add instancing** - transform matrices per primitive instance
5. **Extend to NURBS** - trimmed surfaces, rational basis functions

## References

- ADR-006: Shape Representation Strategies
- [Kajiya 1982: Ray Tracing Parametric Patches](https://dl.acm.org/doi/10.1145/800064.801284)
- [Utah: Practical ray tracing of trimmed NURBS](https://www2.cs.utah.edu/gdc/publications/papers/raynurbs.pdf)
