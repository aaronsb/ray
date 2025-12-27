# ADR-011: Path Scattering Model (Internal vs External)

Status: Proposed
Date: 2025-12-27
Deciders: @aaron

## Context

The current path tracer uses a single bounce counter (`MAX_BOUNCES`) for all ray interactions. This conflates two fundamentally different types of scattering:

1. **Surface scattering** - ray bounces off an external surface (diffuse, specular reflection)
2. **Internal traversal** - ray travels through a medium (glass refraction, subsurface)

### Current Problems

With `FEATURE_DIFFUSE_BOUNCE` enabled:

- Glass objects appear opaque white in scenes with bright skies
- Internal refraction consumes bounce budget meant for surface interactions
- A ray entering glass, bouncing internally, and exiting counts as 2-3 bounces
- When `MAX_BOUNCES` exhausted, remaining throughput adds sky color

**Observed behavior:**
- Cornell box (black sky): works correctly - non-terminated rays add black
- Demo scene (bright sky): glass appears white - non-terminated rays add sky gradient

### Root Cause

A ray path like:
```
camera → glass enter (bounce 1) → glass exit (bounce 2) → diffuse (bounce 3) → diffuse bounce (bounce 4) → MAX_BOUNCES reached
```

Exhausts the budget before properly terminating, leaving high throughput that gets multiplied by sky color.

## Options Considered

### Option 1: Separate Bounce Counters

Track external and internal bounces separately:

```glsl
int externalBounces = 0;
int internalBounces = 0;
bool insideMedium = false;

for (...) {
    if (hit surface) {
        if (insideMedium) {
            internalBounces++;  // No limit or high limit
        } else {
            externalBounces++;
            if (externalBounces >= MAX_EXTERNAL_BOUNCES) break;
        }
    }
}
```

**Pros:** Clear separation, easy to reason about
**Cons:** Doesn't handle nested media well, arbitrary limits still needed

### Option 2: Stochastic Path Termination (Throughput-Based)

Probabilistically terminate paths based on remaining throughput (sometimes called "Russian roulette" in literature):

```glsl
// After each bounce
float maxThroughput = max(throughput.r, max(throughput.g, throughput.b));
float survivalProb = min(maxThroughput, 0.95);  // Cap at 95% to guarantee termination

if (rand() > survivalProb) {
    terminated = true;
    break;
}
throughput /= survivalProb;  // Boost survivors to maintain unbiased estimator
```

**Pros:**
- Unbiased Monte Carlo estimator
- Naturally terminates low-contribution paths early
- High-contribution paths (bright glass) survive longer
- Industry standard technique

**Cons:**
- Adds variance to image
- Needs minimum bounce count before termination kicks in

### Option 3: Media Stack Architecture

Track what medium the ray is currently in:

```glsl
struct MediumState {
    uint mediumId;      // 0 = air, 1+ = material index
    float ior;          // Current IOR
    vec3 absorption;    // Beer's law coefficient
};

MediumState mediaStack[MAX_MEDIA_DEPTH];
int mediaDepth = 0;
```

When entering glass: `push(glassMedium)`
When exiting glass: `pop()`

**Pros:**
- Correct nested dielectric handling (glass in water)
- Foundation for volumetric scattering
- Subsurface scattering support
- Physically accurate

**Cons:**
- More complex state management
- Stack overflow potential with pathological geometry

### Option 4: Hybrid Approach

Combine Russian roulette with media awareness:

1. Use media stack to track internal/external state
2. Apply Russian roulette only to external bounces
3. Allow unlimited internal traversal (within medium)
4. Minimum bounces before RR (e.g., 2-3 external bounces guaranteed)

## Decision

**Recommended: Option 4 (Hybrid)** implemented in phases:

### Phase 1: Stochastic Termination (Immediate)

Add throughput-based termination to `FEATURE_DIFFUSE_BOUNCE`:

```glsl
#define MIN_BOUNCES_BEFORE_TERMINATION 2

// After minimum bounces, apply stochastic termination
if (bounceCount >= MIN_BOUNCES_BEFORE_TERMINATION) {
    float survivalProb = min(max(throughput.r, max(throughput.g, throughput.b)), 0.95);
    if (rand() > survivalProb) {
        terminated = true;
        break;
    }
    throughput /= survivalProb;
}
```

### Phase 2: Media State Tracking (Future)

Add simple inside/outside tracking:

```glsl
bool insideDielectric = false;
float currentIOR = 1.0;

// On glass hit:
if (entering) {
    insideDielectric = true;
    currentIOR = mat.ior;
} else {
    insideDielectric = false;
    currentIOR = 1.0;
}
```

### Phase 3: Full Media Stack (Future)

For nested dielectrics and volumetrics:

```glsl
uint mediaStack[4];  // Material IDs
int mediaDepth = 0;
```

## Consequences

### Positive

- Glass objects will render correctly with diffuse GI enabled
- Low-contribution paths terminate early (performance)
- High-contribution paths get full budget (quality)
- Foundation for subsurface scattering
- Physically-based, unbiased rendering

### Negative

- Additional variance from stochastic termination (needs more samples)
- Complexity in tracking media state
- Potential edge cases with non-manifold geometry

### Neutral

- May want `FEATURE_STOCHASTIC_TERMINATION` as separate flag
- Need to tune minimum bounces and survival probability cap
- Firefly clamp becomes more important with stochastic paths

## References

- Veach 1997: "Robust Monte Carlo Methods for Light Transport Simulation" (stochastic termination)
- Pharr et al. 2016: "Physically Based Rendering" (media tracking, BSSRDF)
- PBRT Chapter 11: Volume Scattering
- Christensen 2016: "Path Tracing in Production" (practical termination strategies)
