# RayVen Feature Matrix

Capability coverage for the ray tracing engine.

## Geometry × Material Support

Which material types work on which geometry:

| Material | Bezier Patches | CSG Primitives | Floor Plane |
|----------|----------------|----------------|-------------|
| **Diffuse** | ✓ Sun, point, area lights + shadows | ✓ Sun, point, area lights + shadows | ✓ Sun, point, area lights + shadows |
| **Metal** | ✓ Reflection, roughness, bump | ✓ Reflection, roughness | ✗ Not applicable |
| **Glass** | ✓ Refraction, Fresnel, absorption | ✓ Refraction, Fresnel, absorption | ✗ Not applicable |
| **Emissive** | ✓ Self-illumination | ✓ Self-illumination + area light source | ✗ Not implemented |
| **Checker** | ✗ Not implemented | ✗ Not implemented | ✓ Two-color pattern with scale |

## Lighting Features

| Feature | Description | Status |
|---------|-------------|--------|
| **Sun (Directional)** | Scene-defined directional light with color/intensity | ✓ Implemented |
| **Sun Disc** | Visible sun with limb darkening | ✓ Implemented |
| **Rayleigh Sky** | Physically-based sky gradient | ✓ Implemented |
| **Horizon Glow** | Warm tones at low sun angles | ✓ Implemented |
| **Sky Ambient** | Scene-defined ambient from sky `(sun (ambient X))` | ✓ Implemented |
| **Sun Shadows** | Hard shadows from directional light | ✓ All geometry types |
| **Area Lights** | Soft shadows from emissive surfaces | ✓ CSG emissives only |
| **Point Lights** | Omnidirectional with inverse-square falloff | ✓ Implemented |
| **Spot Lights** | Cone lights with inner/outer angle falloff | ✓ Implemented |
| **Gobo Patterns** | Procedural spotlight masks (bars, grid, dots, radial, noise) | ✓ Implemented |

### Historical Features (Removed)

Features that existed in earlier versions but were removed during the clean-slate refactor:

| Feature | Description | Removal Reason |
|---------|-------------|----------------|
| **Day/Night Cycle** | Sun position-based twilight transitions | Simplified to always-day |
| **Star Field** | Procedural stars at night | Removed with day/night |
| **NEE Sphere Lights** | Hardcoded emissive sphere sampling | Replaced by area lights |

## Shadow Coverage

Who casts shadows on whom:

| Caster ↓ / Receiver → | Bezier | CSG | Floor |
|-----------------------|--------|-----|-------|
| **Bezier Patches** | ✓ | ✓ | ✓ |
| **CSG Primitives** | ✓ | ✓ | ✓ |
| **Floor Plane** | — | — | — |

## Material Features

| Feature | Diffuse | Metal | Glass | Emissive | Checker |
|---------|---------|-------|-------|----------|---------|
| **Base Color** | ✓ Albedo | ✓ Tint | ✓ Tint + absorption | ✓ Light color | ✓ Two colors |
| **Roughness** | — | ✓ GGX microfacet | — | — | — |
| **Metallic/Bump** | — | ✓ Bump strength | — | — | — |
| **IOR** | — | — | ✓ Refraction index | — | ✓ (as color2.b) |
| **Emissive** | — | — | — | ✓ Intensity multiplier | ✓ (as scale) |
| **Fresnel** | — | — | ✓ Schlick approx | — | — |
| **Absorption** | — | — | ✓ Beer's law | — | — |

## CSG Primitive Types

| Primitive | Ray Intersection | Surface Sampling | Area Calculation |
|-----------|------------------|------------------|------------------|
| **Sphere** | ✓ Quadratic | ✓ Uniform spherical | ✓ 4πr² |
| **Box** | ✓ Slab method | ✓ Area-weighted faces | ✓ 8(xy+yz+zx) |
| **Cylinder** | ✓ Quadratic + caps | ✓ Caps + lateral | ✓ 2πr² + 2πrh |
| **Cone** | ✓ Quadratic + cap | ✓ Base + lateral | ✓ πr² + πr√(r²+h²) |
| **Torus** | ✓ Quartic solver | ✓ Rejection sampling | ✓ 4π²Rr |

## CSG Operations

| Operation | Description | Status |
|-----------|-------------|--------|
| **Union** | Combine two shapes | ✓ Interval merge |
| **Intersect** | Keep overlap only | ✓ Interval intersect |
| **Subtract** | Remove B from A | ✓ Interval complement |
| **BVH Acceleration** | Spatial hierarchy for CSG roots | ✓ Implemented |

## Rendering Features

| Feature | Flag | Description |
|---------|------|-------------|
| **Reflection** | `FEATURE_REFLECTION` | Mirror/metal bounce rays |
| **Refraction** | `FEATURE_REFRACTION` | Glass transmission |
| **Fresnel** | `FEATURE_FRESNEL` | Angle-dependent reflection |
| **Shadows** | `FEATURE_SHADOWS` | Shadow rays for diffuse |
| **Absorption** | `FEATURE_ABSORPTION` | Beer's law colored glass |
| **Roughness** | `FEATURE_ROUGHNESS` | GGX microfacet distribution |
| **Bump Mapping** | `FEATURE_BUMP` | Procedural normal perturbation |
| **Emission** | `FEATURE_EMISSION` | Self-illuminating materials |
| **Area Lights** | `FEATURE_AREA_LIGHTS` | Direct sampling of emissives |
| **Point Lights** | `FEATURE_POINT_LIGHTS` | Point light evaluation |
| **Firefly Clamp** | `FEATURE_FIREFLY_CLAMP` | Adaptive luminance clamping for early frames |
| **Accumulation** | `FEATURE_ACCUMULATION` | Progressive refinement |
| **Jitter** | `FEATURE_JITTER` | Subpixel anti-aliasing |
| **CSG** | `FEATURE_CSG` | Constructive solid geometry |
| **Dispersion** | `FEATURE_DISPERSION` | Chromatic aberration (TODO) |

## Known Limitations

| Limitation | Description | Possible Fix |
|------------|-------------|--------------|
| Bezier emissives not area lights | Patch groups can't be sampled as lights | Add parametric surface sampling |
| Floor can't be emissive | Infinite plane has infinite area | Limit to visible region |
| No texture mapping | Only procedural patterns (checker) | Add UV coordinates + texture sampling |
| Single bounce path tracing | Limited global illumination | Add path tracing with MIS |
| Hard shadows from sun | No soft shadow penumbra | Sample sun disc for soft shadows |
| No motion blur | Static scenes only | Add temporal sampling |
| No depth of field | Pinhole camera | Add aperture sampling |

## Future Considerations

- **Multiple Importance Sampling (MIS)** - Balance BRDF vs light sampling
- **Next Event Estimation** - Explicit light sampling at each bounce
- **Russian Roulette** - Probabilistic path termination
- **Denoising** - AI or filter-based noise reduction (see ADR-010)
- **Volumetrics** - Fog, smoke, subsurface scattering
- **Instancing** - Multiple copies of same geometry
- **Transforms** - Rotation/scale for CSG primitives
