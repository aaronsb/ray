# Materials Guide

This guide covers the material system in Ray's Bouncy Castle, including material types, properties, and usage in scene files.

## Overview

Materials define how light interacts with surfaces. The path tracer supports physically-based materials with properties like:
- **Albedo**: Surface color
- **Roughness**: Surface smoothness (mirror vs matte)
- **Metallic**: Conductor vs dielectric
- **IOR**: Index of refraction (for glass)
- **Emissive**: Self-illumination

## Material Types

### Diffuse

Lambertian diffuse reflection (matte surfaces).

**Properties**:
- `rgb`: Albedo color (0-1 range)

**Typical uses**:
- Matte paint, paper, clay
- Rough stone, concrete
- Cloth, fabrics

**Examples**:

```lisp
; Matte red
(material red (type diffuse) (rgb 0.9 0.2 0.2))

; White paper
(material paper (type diffuse) (rgb 0.95 0.95 0.95))

; Dark stone
(material stone (type diffuse) (rgb 0.3 0.3 0.3))

; Green grass
(material grass (type diffuse) (rgb 0.2 0.6 0.2))
```

**Behavior**:
- Scatters light uniformly in all directions
- No specular highlights
- Energy-conserving (reflects up to albedo amount)

### Metal

Conductor with configurable roughness (Fresnel + microfacet BRDF).

**Properties**:
- `rgb`: Albedo (metallic tint)
- `roughness`: Surface roughness (0=mirror, 1=rough)

**Typical uses**:
- Polished metals (gold, silver, copper, aluminum)
- Brushed steel, anodized aluminum
- Chrome, mirrors

**Examples**:

```lisp
; Mirror-like gold
(material gold (type metal) (rgb 0.83 0.69 0.22) (roughness 0.02))

; Brushed steel
(material steel (type metal) (rgb 0.78 0.78 0.78) (roughness 0.3))

; Polished silver
(material silver (type metal) (rgb 0.95 0.95 0.97) (roughness 0.02))

; Copper
(material copper (type metal) (rgb 0.95 0.64 0.54) (roughness 0.05))

; Rough aluminum
(material aluminum (type metal) (rgb 0.91 0.92 0.92) (roughness 0.5))
```

**Behavior**:
- Fresnel reflectance (grazing angles reflect more)
- Microfacet distribution (roughness controls highlight size)
- Energy-conserving GGX distribution
- No transmission (fully opaque)

**Roughness guide**:
- `0.0 - 0.05`: Mirror-like (chrome, polished metal)
- `0.1 - 0.3`: Glossy (polished wood, car paint)
- `0.4 - 0.6`: Satin (brushed metal, anodized surfaces)
- `0.7 - 1.0`: Rough (oxidized metal, rust)

### Glass

Dielectric with refraction (Fresnel + Snell's law).

**Properties**:
- `rgb`: Tint color (0-1 range)
- `ior`: Index of refraction

**Typical uses**:
- Glass, crystal
- Water, ice
- Transparent plastics, acrylic
- Gemstones (diamond, sapphire)

**Examples**:

```lisp
; Clear glass
(material glass (type glass) (rgb 0.95 0.95 0.95) (ior 1.5))

; Water
(material water (type glass) (rgb 0.9 0.95 1.0) (ior 1.33))

; Diamond
(material diamond (type glass) (rgb 1.0 1.0 1.0) (ior 2.42))

; Emerald (tinted)
(material emerald (type glass) (rgb 0.3 0.8 0.5) (ior 1.57))

; Acrylic plastic
(material acrylic (type glass) (rgb 0.98 0.98 0.98) (ior 1.49))
```

**Behavior**:
- Fresnel reflectance (angle-dependent)
- Refraction via Snell's law
- Total internal reflection at grazing angles
- Dispersion not yet implemented (future: chromatic aberration)

**IOR values**:
| Material | IOR |
|----------|-----|
| Vacuum | 1.0 |
| Air | 1.0003 |
| Water | 1.33 |
| Glass (crown) | 1.52 |
| Glass (flint) | 1.6-1.7 |
| Sapphire | 1.77 |
| Diamond | 2.42 |

### Emissive

Self-illuminating surface (light source).

**Properties**:
- `rgb`: Emission color
- `emissive`: Emission multiplier (brightness)

**Typical uses**:
- Area lights, light panels
- Glowing objects (neon, plasma)
- Sky dome (future)

**Examples**:

```lisp
; White light (bright)
(material light (type emissive) (rgb 1.0 1.0 1.0) (emissive 10.0))

; Warm light (incandescent)
(material warm_light (type emissive) (rgb 1.0 0.9 0.7) (emissive 5.0))

; Blue neon
(material neon_blue (type emissive) (rgb 0.2 0.5 1.0) (emissive 8.0))

; Dim red glow
(material red_glow (type emissive) (rgb 1.0 0.2 0.2) (emissive 2.0))
```

**Behavior**:
- Does not reflect light (pure emission)
- Contributes to global illumination
- Visible in reflections and refractions
- No shadows cast (not a directional light)

**Emission guide**:
- `1.0 - 3.0`: Dim glow (ambient lighting)
- `5.0 - 10.0`: Moderate light (indoor lighting)
- `10.0 - 50.0`: Bright light (outdoor, sun)
- `100.0+`: Very bright (sun, explosions)

## Material Properties

### RGB / Albedo

Surface color in linear RGB space (0-1 range).

```lisp
(rgb 0.9 0.2 0.2)     ; Red
(rgb 0.2 0.8 0.3)     ; Green
(rgb 0.2 0.4 0.9)     ; Blue
(rgb 0.9 0.9 0.9)     ; White
(rgb 0.1 0.1 0.1)     ; Dark gray
```

**Notes**:
- Values are linear (not sRGB)
- Values > 1.0 are clamped
- For emissive, use high `emissive` multiplier instead of high RGB

### Roughness

Surface microsurface roughness (0-1 range).

```lisp
(roughness 0.0)    ; Perfect mirror
(roughness 0.05)   ; Very shiny (chrome)
(roughness 0.2)    ; Glossy (polished wood)
(roughness 0.5)    ; Satin (brushed metal)
(roughness 1.0)    ; Completely rough (matte)
```

**Applies to**:
- `metal` (specular highlight size)
- Future: `rough-glass` (frosted glass)

### Metallic

Not currently used (reserved for future PBR workflow).

In a full PBR system, this would control:
- 0.0: Dielectric (non-metal)
- 1.0: Conductor (metal)

Currently, use `type metal` or `type diffuse` instead.

### IOR (Index of Refraction)

Refraction amount for glass materials.

```lisp
(ior 1.0)     ; No refraction (air)
(ior 1.33)    ; Water
(ior 1.5)     ; Glass
(ior 1.77)    ; Sapphire
(ior 2.42)    ; Diamond
```

**Applies to**:
- `glass` (refraction strength)

**Effect on appearance**:
- Low IOR (1.0-1.3): Subtle refraction (water, thin glass)
- Medium IOR (1.5-1.7): Normal glass, crystals
- High IOR (2.0+): Strong refraction (diamond), high reflectance

### Emissive

Emission multiplier for light-emitting materials.

```lisp
(emissive 0.0)     ; No emission
(emissive 5.0)     ; Moderate light
(emissive 10.0)    ; Bright light
(emissive 50.0)    ; Very bright
```

**Applies to**:
- `emissive` (brightness control)

## Material Library

Define materials once, reuse in multiple shapes:

```lisp
; Material definitions
(material red_metal (type metal) (rgb 0.9 0.2 0.2) (roughness 0.05))
(material glass (type glass) (rgb 0.95 0.95 0.95) (ior 1.5))

; Use materials
(shape (sphere (at -2 1 0) (r 1.0)) red_metal)
(shape (sphere (at  2 1 0) (r 1.0)) glass)

(shape (subtract
         (sphere (at 0 1 0) (r 1.2))
         (box (at 0 1 0) (half 0.6 1.5 0.6)))
       red_metal)
```

### Reusable Material Libraries

Create a shared material library:

```lisp
; materials.scene
(material white (type diffuse) (rgb 0.9 0.9 0.9))
(material black (type diffuse) (rgb 0.1 0.1 0.1))

(material gold (type metal) (rgb 0.83 0.69 0.22) (roughness 0.02))
(material silver (type metal) (rgb 0.95 0.95 0.97) (roughness 0.02))
(material copper (type metal) (rgb 0.95 0.64 0.54) (roughness 0.05))

(material glass (type glass) (rgb 0.95 0.95 0.95) (ior 1.5))
(material water (type glass) (rgb 0.9 0.95 1.0) (ior 1.33))
```

Include in scenes:

```lisp
; demo.scene
(include "materials.scene")

(shape (sphere (at 0 1 0) (r 1.0)) gold)
(shape (box (at 2 1 0) (half 0.8 0.8 0.8)) glass)
```

## Common Material Presets

### Metals

```lisp
; Gold (shiny)
(material gold (type metal) (rgb 0.83 0.69 0.22) (roughness 0.02))

; Silver (mirror-like)
(material silver (type metal) (rgb 0.95 0.95 0.97) (roughness 0.02))

; Copper (polished)
(material copper (type metal) (rgb 0.95 0.64 0.54) (roughness 0.05))

; Brushed aluminum
(material aluminum (type metal) (rgb 0.91 0.92 0.92) (roughness 0.4))

; Chrome (perfect mirror)
(material chrome (type metal) (rgb 0.95 0.95 0.95) (roughness 0.0))

; Iron (rough)
(material iron (type metal) (rgb 0.56 0.57 0.58) (roughness 0.5))
```

### Dielectrics

```lisp
; Clear glass (window)
(material clear_glass (type glass) (rgb 0.95 0.95 0.95) (ior 1.52))

; Water
(material water (type glass) (rgb 0.9 0.95 1.0) (ior 1.33))

; Diamond
(material diamond (type glass) (rgb 1.0 1.0 1.0) (ior 2.42))

; Ruby (tinted glass)
(material ruby (type glass) (rgb 0.9 0.2 0.2) (ior 1.77))

; Sapphire (tinted glass)
(material sapphire (type glass) (rgb 0.2 0.3 0.8) (ior 1.77))

; Ice
(material ice (type glass) (rgb 0.95 0.98 1.0) (ior 1.31))
```

### Diffuse

```lisp
; White matte
(material white_matte (type diffuse) (rgb 0.9 0.9 0.9))

; Red clay
(material clay (type diffuse) (rgb 0.7 0.3 0.2))

; Concrete
(material concrete (type diffuse) (rgb 0.5 0.5 0.5))

; Wood (light)
(material wood (type diffuse) (rgb 0.6 0.4 0.2))

; Grass
(material grass (type diffuse) (rgb 0.2 0.6 0.2))

; Sky blue (for backgrounds)
(material sky (type diffuse) (rgb 0.5 0.7 1.0))
```

### Emissive

```lisp
; White light (bright)
(material white_light (type emissive) (rgb 1.0 1.0 1.0) (emissive 10.0))

; Warm light (incandescent)
(material warm_light (type emissive) (rgb 1.0 0.8 0.6) (emissive 8.0))

; Cool light (fluorescent)
(material cool_light (type emissive) (rgb 0.9 0.95 1.0) (emissive 6.0))

; Red neon
(material red_neon (type emissive) (rgb 1.0 0.1 0.1) (emissive 5.0))

; Blue glow
(material blue_glow (type emissive) (rgb 0.2 0.5 1.0) (emissive 3.0))
```

## Shader Implementation

Materials are evaluated in `shaders/includes/materials.glsl`:

### Material Structure

```glsl
struct Material {
    float r, g, b;      // albedo
    uint type;          // MaterialType enum
    float roughness;    // 0=mirror, 1=rough
    float metallic;     // (reserved)
    float ior;          // index of refraction
    float emissive;     // emission multiplier
};
```

### Material Types (GLSL)

```glsl
#define MAT_DIFFUSE    0
#define MAT_METAL      1
#define MAT_GLASS      2
#define MAT_EMISSIVE   3
```

### BRDF Evaluation

```glsl
vec3 scatter(Material mat, vec3 rayDir, vec3 normal, inout uint rngState) {
    if (mat.type == MAT_DIFFUSE) {
        // Lambertian diffuse
        return cosineSampleHemisphere(normal, rngState);
    }
    else if (mat.type == MAT_METAL) {
        // Specular reflection + GGX microfacet
        vec3 reflected = reflect(rayDir, normal);
        return sampleGGX(reflected, mat.roughness, rngState);
    }
    else if (mat.type == MAT_GLASS) {
        // Fresnel + refraction
        float fresnel = schlickFresnel(rayDir, normal, mat.ior);
        if (random(rngState) < fresnel) {
            return reflect(rayDir, normal);
        } else {
            return refract(rayDir, normal, mat.ior);
        }
    }
    else if (mat.type == MAT_EMISSIVE) {
        // No scattering (absorbed)
        return vec3(0);
    }
}
```

## Best Practices

### Physical Accuracy

1. **Use realistic albedo values**:
   - Most materials have albedo < 0.9
   - Dark materials: 0.02 - 0.2
   - Mid-tone: 0.3 - 0.6
   - Light: 0.7 - 0.9

2. **Use realistic IOR values**:
   - Reference tables for accurate refraction
   - Diamond IOR is 2.42, not 3.0+

3. **Balance roughness**:
   - Perfect mirrors (0.0) are rare in nature
   - Most metals have some roughness (0.02 - 0.2)

### Performance

1. **Limit glass nesting**:
   - Nested glass objects cause many refraction bounces
   - Use max 2-3 levels of glass nesting

2. **Use emissive sparingly**:
   - Too many emissive surfaces slow convergence
   - Prefer 1-3 area lights per scene

3. **Rough metals converge faster**:
   - Roughness 0.2+ converges faster than mirrors
   - Perfect mirrors (0.0) need many samples

### Artistic Control

1. **Material contrast**:
   - Mix rough and smooth materials
   - Vary albedo for visual interest

2. **Color harmony**:
   - Use complementary colors for appeal
   - Tint glass slightly for realism

3. **Lighting setup**:
   - Use emissive surfaces for soft lighting
   - Combine with metallic reflectors

## Future Material Types

Planned additions:

### Rough Glass (Frosted Glass)

```lisp
(material frosted (type rough-glass) (rgb 0.95 0.95 0.95) (ior 1.5) (roughness 0.3))
```

Combines refraction with microfacet scattering.

### Anisotropic Metal (Brushed)

```lisp
(material brushed_steel (type anisotropic-metal) (rgb 0.78 0.78 0.78) (roughness 0.3) (anisotropy 0.8))
```

Directional roughness (brushed metal, hair).

### Subsurface Scattering

```lisp
(material skin (type sss) (rgb 0.9 0.7 0.6) (scatter-depth 0.5))
```

For skin, wax, marble.

### Procedural Textures

```lisp
(material marble (type procedural) (pattern marble) (scale 1.0))
(material wood (type procedural) (pattern wood) (scale 2.0))
(material checker (type procedural) (pattern checker) (scale 0.5))
```

Already implemented in shader, needs scene format support.

## Related Documentation

- [Scene Format Reference](scene-format.md) - Material syntax in scene files
- [Architecture Guide](architecture.md) - Material system implementation
- [ADR-002: Path Tracer Optimizations](adr/ADR-002-path-tracer-optimizations.md) - Material sampling strategies

## References

- [Physically Based Rendering (PBR) Book](https://www.pbr-book.org/)
- [Real Shading in Unreal Engine 4](https://blog.selfshadow.com/publications/s2013-shading-course/)
- [Fresnel Equations](https://en.wikipedia.org/wiki/Fresnel_equations)
- [GGX Microfacet Distribution](https://www.graphics.cornell.edu/~bjw/microfacetbsdf.pdf)
