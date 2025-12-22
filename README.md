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
- **Next Event Estimation (NEE)** for efficient direct lighting
- **Interactive camera**: Orbit, pan, zoom with mouse

## Controls

- **Left mouse drag**: Orbit camera
- **Right mouse drag**: Pan camera
- **Scroll wheel**: Zoom
- **`[` / `]`**: Adjust sun azimuth
- **`{` / `}`**: Adjust sun elevation (full day/night cycle)
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
- Precision-optimized ground plane intersection to avoid floating-point artifacts
