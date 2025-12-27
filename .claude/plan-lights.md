# Plan: Scene-Defined Lighting System

## Goal
Add dynamic sun/light definitions to scene files, replacing hardcoded shader lighting.

## Scene Format

```lisp
; Sun with azimuth/elevation (like real solar positioning)
(sun (azimuth 45) (elevation 30) (color 1.0 0.98 0.9) (intensity 1.2))

; Or simple direction vector
(sun (direction 0.4 0.6 0.3) (color 1.0 0.95 0.8) (intensity 1.0))

; Point lights (future)
(light point (at 0 5 0) (color 1 1 1) (intensity 10))
```

## Implementation Checklist

- [x] Create Light struct and LightList (lights.h)
- [x] Add LightList to SceneData
- [x] Parse (sun ...) in scene loader
- [x] Add light buffer to renderer
- [x] Add push constant for sun angular radius
- [x] Update shader:
  - [x] Add light buffer binding
  - [x] Use sun direction for skyGradient()
  - [x] Use sun direction for diffuse/shadow lighting
  - [x] Render visible sun disc (with limb darkening)
  - [x] Day/night based on sun elevation

## Shader Changes

```glsl
// New push constants
float sunAngularRadius;  // For disc rendering
uint numLights;

// New buffer
layout(std430, binding = 11) readonly buffer LightBuffer {
    Light lights[];  // lights[0] is always sun
};

// Sky uses lights[0].direction
// Diffuse shading uses lights[0].direction
// Sun disc rendered when ray direction near sun direction
```

## Files Modified

1. `src/parametric/lights/lights.h` - Light/SunLight/LightList structs ✓
2. `src/parametric/scene/scene_loader.h` - Added LightList to SceneData, processSun() ✓
3. `src/apps/ray/ray_renderer.h` - Added light buffer, push constants ✓
4. `src/apps/ray/ray_renderer.cpp` - createLightBuffer(), cleanup, push constants ✓
5. `shaders/ray.comp` - Light struct, buffer binding 11, dynamic diffuse ✓
6. `shaders/includes/geometry.glsl` - skyGradientWithSun() with disc rendering ✓

## Status: COMPLETE

The scene-defined lighting system is fully implemented. The sun can be configured in scene files with:
- `(sun (azimuth 45) (elevation 30))` - solar positioning
- `(sun (direction x y z))` - direct vector
- `(sun (color r g b) (intensity 1.2))` - appearance
- `(sun (radius 0.53))` - angular size in degrees

The shader now uses dynamic sun for:
- Sky gradient (Rayleigh scattering)
- Visible sun disc with limb darkening
- Diffuse shading (floor, CSG, Bezier)
- Shadow rays
- Day/night transitions (based on sun elevation)
