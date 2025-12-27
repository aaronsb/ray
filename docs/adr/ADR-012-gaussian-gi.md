# ADR-012: Gaussian Splatting for Global Illumination

Status: Proposed
Date: 2025-12-27
Deciders: @aaron, @claude

## Context

Our current GI implementation uses brute-force path tracing with multiple systems:
- `FEATURE_DIFFUSE_BOUNCE` for multi-bounce indirect lighting
- Stochastic termination to manage path lengths
- Separate sampling for area lights, point lights, spotlights, sun
- Sky ambient as a fallback
- Throughput tracking across bounces

This creates complexity and slow convergence. Each diffuse bounce requires:
1. Generate random hemisphere direction
2. Trace ray through full scene (BVH traversal)
3. Evaluate hit, recurse
4. Accumulate with Russian roulette / stochastic termination

We also have analytical SDFs for CSG primitives that we don't leverage for GI.

**Question**: Could a Gaussian-based representation unify and simplify our GI solution?

## Proposed Approach

Replace multi-bounce path tracing with a **Gaussian radiance field**:

### Gaussian Representation

Each Gaussian stores:
```c
struct GIGaussian {
    vec3 position;      // Surface point
    vec3 normal;        // Surface orientation
    vec3 radiance;      // Outgoing light (direct + indirect)
    float radius;       // Influence extent
    float emission;     // For emissive surfaces
};
```

### Placement Strategy

| Geometry | Gaussian Placement |
|----------|-------------------|
| CSG Sphere | 1 at center, radius = sphere radius |
| CSG Box | 6 on faces, or 1 at center |
| CSG Cylinder | Caps + lateral samples |
| Bezier Patch | 1-4 per patch at surface samples |
| Floor | Regular grid within camera frustum |
| Emissive surfaces | Same as above, with emission value |

Estimate: 1,000 - 10,000 Gaussians for typical scene (~50KB - 500KB).

### Light Propagation

Offline or first-frame computation:
```
Initialize:
    For each Gaussian g:
        g.radiance = computeDirectLight(g.position, g.normal)
        g.radiance += g.emission

Propagate (N iterations):
    For each Gaussian g:
        For each other Gaussian h:
            if (visible(g, h)):  // Optional - can skip for diffuse
                g.radiance += transfer(h, g) * h.radiance

Where transfer(h, g) accounts for:
    - Distance falloff (Gaussian)
    - Normal alignment (form factor)
    - BRDF (diffuse = albedo/pi)
```

This is essentially **radiosity with Gaussian basis functions**.

### Runtime Query

```glsl
vec3 indirectLight(vec3 p, vec3 n, vec3 albedo) {
    vec3 indirect = vec3(0);
    float totalWeight = 0.0;

    for (int i = 0; i < numGaussians; i++) {
        Gaussian g = gaussians[i];
        vec3 toG = g.position - p;
        float dist = length(toG);
        vec3 dir = toG / dist;

        // Gaussian spatial falloff
        float spatial = exp(-dist * dist / (2.0 * g.radius * g.radius));

        // Hemisphere visibility (receiver side)
        float cosReceiver = max(0.0, dot(n, dir));

        // Emitter orientation
        float cosEmitter = max(0.0, dot(g.normal, -dir));

        float weight = spatial * cosReceiver * cosEmitter;
        indirect += g.radiance * weight;
        totalWeight += weight;
    }

    return (albedo / PI) * indirect / max(totalWeight, 0.001);
}
```

### Acceleration

For 10K Gaussians, brute-force query is expensive. Options:
1. **Spatial hash** - Only query nearby Gaussians
2. **BVH over Gaussians** - Cull distant clusters
3. **Hierarchical Gaussians** - LOD for distant regions
4. **Screen-space tiles** - Group Gaussians by screen region

## Comparison

| Aspect | Current (Path Tracing) | Proposed (Gaussian) |
|--------|------------------------|---------------------|
| Bounce computation | Recursive ray trace | Single buffer query |
| Convergence | Slow (stochastic) | Immediate (deterministic) |
| Soft shadows | Monte Carlo sampling | Natural Gaussian falloff |
| Code complexity | High (many features) | Lower (one system) |
| Memory | Minimal | ~500KB for Gaussians |
| Dynamic scenes | Automatic | Need Gaussian updates |
| Specular indirect | Handled | Needs separate path |
| Quality ceiling | Ground truth | Approximation |

## What We'd Remove

- `FEATURE_DIFFUSE_BOUNCE` flag and logic
- Stochastic termination code
- Multi-bounce throughput tracking
- Cosine-weighted hemisphere sampling (for GI)
- Complex bounce loop in shader

## What We'd Keep

- Primary ray tracing (visibility)
- Direct lighting (sun, point, spot, area)
- Shadow rays
- Specular reflection/refraction paths
- Path tracing for glass caustics

## What We'd Add

- Gaussian buffer (SSBO)
- Gaussian placement (CPU, at scene load)
- Light propagation pass (compute shader, once or periodic)
- Gaussian query in main shader

## Consequences

### Positive
- Simpler mental model: "surfaces radiate light, query nearby radiators"
- Deterministic indirect lighting (no noise for diffuse)
- Faster convergence for diffuse GI
- Natural soft falloff without sampling
- Reduced shader complexity

### Negative
- Approximation, not ground truth
- Specular indirect still needs ray tracing
- Dynamic geometry needs Gaussian updates
- Gaussian placement heuristics may need tuning
- Additional memory for Gaussian buffer

### Neutral
- Different quality characteristics (smooth but possibly less accurate)
- May complement rather than replace path tracing (hybrid approach)
- Research needed on optimal Gaussian density/placement

## Alternatives Considered

### 1. Voxel-based GI (Voxel Cone Tracing)
- Store radiance in 3D voxel grid
- Trace cones through grid for indirect
- **Rejected**: Higher memory, less adaptive than Gaussians

### 2. Screen-space GI (SSGI)
- Use screen buffer for indirect
- **Rejected**: View-dependent artifacts, can't handle off-screen bounces

### 3. Irradiance probes (Light probes)
- Regular grid of spherical harmonics
- **Rejected**: Less adaptive than Gaussians, more complex basis

### 4. Keep current path tracing + denoising
- Add temporal/spatial denoiser (see ADR-010)
- **Still valid**: Could be combined with Gaussian approach

## Open Questions

1. **Optimal Gaussian density** - How many per surface?
2. **Propagation iterations** - How many bounces to bake?
3. **Update strategy** - Full rebuild vs incremental?
4. **Specular handling** - Separate path or glossy Gaussians?
5. **Validation** - Compare quality vs path traced reference

## Implementation Phases

### Phase 1: Proof of Concept
- CPU Gaussian placement for CSG only
- Simple propagation (direct light only)
- Query in shader, compare to current GI

### Phase 2: Full Geometry
- Add Bezier patch Gaussians
- Floor grid Gaussians
- Multi-bounce propagation

### Phase 3: Optimization
- Spatial acceleration structure
- GPU propagation compute shader
- Quality/performance tuning

### Phase 4: Hybrid Mode
- Gaussians for diffuse GI
- Path tracing for specular
- Best of both worlds

## References

### Directly Relevant Papers

1. **RadiosityGS** (SIGGRAPH Asia 2025) - Closest to our proposed approach
   - Adapts classical radiosity to Gaussian surfels
   - Operates entirely in spherical harmonics coefficient space
   - Claims "hundreds of FPS" for GI effects
   - Key insight: Gaussians naturally fit radiosity (no mesh discretization needed)
   - Paper: https://arxiv.org/abs/2509.18497
   - Code: https://github.com/RaymondJiangkw/RadiosityGS

2. **GI-GS** (ICLR 2025) - Deferred shading + path tracing hybrid
   - Two-pass: G-buffer with PBR direct, then lightweight path tracing for indirect
   - 5x faster than TensoIR baseline on Mip-NeRF 360 dataset
   - Paper: https://arxiv.org/abs/2410.02619
   - Code: https://github.com/stopaimme/GI-GS

3. **PRTGS** (ACM MM 2024) - Precomputed Radiance Transfer
   - 30+ fps real-time relighting at 1080p
   - One-bounce ray tracing with self-transfer precomputation
   - Paper: https://arxiv.org/pdf/2408.03538v1

4. **3D Gaussian Ray Tracing** (SIGGRAPH Asia 2024, NVIDIA)
   - 55-190 fps with full ray tracing (secondary rays)
   - Enables reflections, refractions, shadows, GI
   - Site: https://gaussiantracer.github.io/
   - Code: https://github.com/nv-tlabs/3dgrut

### Related Background

5. **Spherical Gaussians for Light Transport**
   - Efficient one-bounce interreflection via analytic formulas
   - Tutorial: https://mynameismjp.wordpress.com/2016/10/09/sg-series-part-2-spherical-gaussians-101/

### Known Pitfalls (from literature)

- **Aliasing**: Small Gaussians cause severe aliasing at distance
- **Indirect lighting cost**: Multiple samples per Gaussian needed
- **Baked vs dynamic tradeoff**: Baking limits dynamic relighting
- **Decomposition ambiguity**: Hard to separate materials from lighting
