# ADR-002: Path Tracer Optimizations - Discovery Log

Status: Active
Date: 2025-12-21
Purpose: Document and explore path tracing optimizations incrementally

## Context

Our naive path tracer works but converges slowly. This ADR tracks optimizations as we implement and evaluate them - an "artificial discovery ledger" for learning.

## Baseline

- Pure Monte Carlo path tracing
- Uniform hemisphere sampling for diffuse
- Random light discovery (rays must accidentally hit emissive surfaces)
- PCG random number generator
- Russian roulette termination (implemented)

**Baseline convergence**: ~30,000+ samples for clean image

---

## Optimization Catalog

### 1. Next Event Estimation (NEE) / Direct Light Sampling
**Status**: [ ] Not started

**Problem**: Rays randomly bounce hoping to hit small light sources. Most miss.

**Solution**: At each surface hit, explicitly cast a shadow ray toward the light and add its contribution directly.

**Expected improvement**: 10-50x faster convergence for direct illumination

**Implementation**:
```glsl
// At each hit point:
vec3 lightDir = normalize(lightPos - hitPoint);
float lightDist = length(lightPos - hitPoint);
Ray shadowRay = Ray(hitPoint, lightDir);
if (!anyHit(shadowRay, lightDist)) {
    // Light is visible - add direct contribution
    directLight += lightIntensity * BRDF * dot(normal, lightDir);
}
```

**Tradeoff**: One extra ray per bounce, but massive noise reduction

---

### 2. Cosine-Weighted Hemisphere Sampling
**Status**: [ ] Not started

**Problem**: Uniform hemisphere sampling wastes rays on grazing angles that contribute little (Lambert's cosine law).

**Solution**: Sample proportional to cos(θ) - more rays toward normal, fewer at edges.

**Expected improvement**: ~2x variance reduction for diffuse surfaces

**Implementation**:
```glsl
// Instead of uniform random on hemisphere:
vec3 cosineWeightedSample(vec3 normal) {
    float r1 = rand();
    float r2 = rand();
    float phi = 2.0 * PI * r1;
    float cosTheta = sqrt(r2);  // <-- this is the key change
    float sinTheta = sqrt(1.0 - r2);
    // ... transform to world space around normal
}
```

**Tradeoff**: Negligible cost, pure improvement

---

### 3. Low-Discrepancy Sequences (Sobol, Halton)
**Status**: [ ] Not started

**Problem**: Pure random numbers can clump, leaving gaps in sample coverage.

**Solution**: Use quasi-random sequences that fill space more evenly.

**Expected improvement**: ~1.5-2x better convergence, especially visible in early samples

**Implementation**: Replace `rand()` with Sobol sequence indexed by frame + pixel

**Tradeoff**: Slightly more complex RNG, need to handle correlation across pixels

---

### 4. Multiple Importance Sampling (MIS)
**Status**: [ ] Not started

**Problem**: Different sampling strategies work better for different scenarios (BRDF sampling vs light sampling).

**Solution**: Combine multiple sampling strategies, weight by their probability densities.

**Expected improvement**: Robust convergence across all material/light combinations

**Implementation**: Balance heuristic (Veach's power heuristic)

**Tradeoff**: More complex, requires tracking PDFs

---

### 5. Adaptive Sampling
**Status**: [ ] Not started

**Problem**: Uniform samples everywhere, even in already-converged regions.

**Solution**: Track variance per pixel, focus samples on noisy areas.

**Expected improvement**: 2-4x effective samples in difficult areas

**Tradeoff**: Need variance tracking buffer, more complex dispatch

---

### 6. Stratified Sampling
**Status**: [ ] Not started

**Problem**: Random samples within a pixel can cluster.

**Solution**: Divide pixel into grid, take one sample per stratum with jitter.

**Expected improvement**: Reduced aliasing, more even coverage

**Tradeoff**: Need to track stratum index across frames

---

### 7. Blue Noise Dithering
**Status**: [ ] Not started

**Problem**: White noise is visually harsh; patterns are perceptible.

**Solution**: Use blue noise texture to distribute error in visually pleasing way.

**Expected improvement**: Looks better at same sample count (perceptual, not mathematical)

**Tradeoff**: Need blue noise texture, slightly more memory

---

### 8. Temporal Reprojection
**Status**: [ ] Not started

**Problem**: Moving camera resets accumulation, losing all converged data.

**Solution**: Reproject previous frame's data to new camera position, blend with new samples.

**Expected improvement**: Near-instant convergence when camera moves slowly

**Tradeoff**: Complex, needs motion vectors, can cause ghosting

---

## Implementation Order (Suggested)

1. **NEE** - Biggest bang for buck, easiest to understand
2. **Cosine-weighted sampling** - Simple math change, visible improvement
3. **Stratified sampling** - Low complexity, good AA improvement
4. **Low-discrepancy sequences** - Replace RNG, systematic improvement
5. **MIS** - Combine NEE + BRDF sampling properly
6. **Adaptive sampling** - Optimize sample distribution
7. **Temporal reprojection** - Quality of life for camera movement
8. **Blue noise** - Polish

---

## Results Log

### Baseline (2025-12-21)
- Samples to "clean": ~30,000+
- Frame time: ~Xms at 1080p
- Notes: Noisy shadows, slow indirect convergence

### After NEE
- [ ] TODO: Record results

### After Cosine-Weighted
- [ ] TODO: Record results

(continue for each optimization...)

---

## References

- "Ray Tracing in One Weekend" series - Peter Shirley
- "Physically Based Rendering" (PBRT) - Pharr, Jakob, Humphreys
- "Ray Tracing Gems" - Haines, Akenine-Möller (free online)
- Veach's thesis on MIS (1997) - foundational importance sampling work
