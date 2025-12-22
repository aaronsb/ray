# Vulkan Path Tracer

A real-time path tracer using Vulkan compute shaders and Qt for the frontend.

![Path Tracer Screenshot](docs/media/screenshot.png)

## Features

- **Real-time path tracing** with progressive accumulation
- **Physically-based materials**:
  - Diffuse (Lambertian)
  - Metal with configurable roughness
  - Dielectric (glass) with refraction
  - Rough dielectric (frosted glass)
  - Emissive
  - Dichroic coated glass with tunable spectrum (thin-film interference)
  - Procedural textures: marble, wood, swirl, checker
- **Physically-based sky**:
  - Rayleigh scattering for blue sky and sunset colors
  - Mie scattering for sun glow
  - Day/night cycle with twilight transitions
  - Procedural stars at night
- **Geometry**: Spheres and axis-aligned boxes
- **Spotlights with gobo patterns**:
  - Configurable cone angles with smooth falloff
  - 6 gobo patterns: solid, stripes, grid, circles, dots, star
  - Pattern rotation and scale controls
- **Next Event Estimation (NEE)** for efficient direct lighting
- **Interactive camera**: Orbit, pan, zoom with mouse
- **Adaptive sampling**: Targets 60fps, scales sample count to use available GPU headroom
- **Auto-convergence**: Stops rendering after ~200 frames to let GPU rest

## Controls

- **Left mouse drag**: Orbit camera
- **Right mouse drag**: Pan camera
- **Scroll wheel**: Zoom
- **`[` / `]`**: Adjust sun azimuth
- **`{` / `}`**: Adjust sun elevation (full day/night cycle)
- **`,` / `.`**: Cycle spotlight gobo patterns (includes "off")
- **`S`**: Save screenshot

## Building

Requires:
- Vulkan SDK
- Qt 6
- CMake 3.16+
- glslc (for shader compilation)

```bash
mkdir build && cd build
cmake ..
cmake --build .
./raydemo
```

## Technical Notes

- Compute shader based (no hardware RT required)
- 7-wavelength spectral sampling for dichroic materials
- Hybrid accumulation: temporal averaging when moving, proper accumulation when stationary
- Framerate-adaptive multi-sampling: more samples when GPU has headroom
- Precision-optimized ground plane intersection to avoid floating-point artifacts
- Auto-idle after convergence to reduce GPU power consumption
