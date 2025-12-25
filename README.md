# Ray's Bouncy Castle

A GPU-accelerated path tracer using Vulkan compute shaders with support for CSG primitives, Bezier patches (Utah teapot), and physically-based materials. Scenes are defined using a clean S-expression format.

![Path Tracer Screenshot](docs/media/screenshot.png)

## Features

- **GPU Path Tracing**: Real-time progressive rendering using Vulkan compute shaders
- **CSG Primitives**: Sphere, box, cylinder, cone, torus with boolean operations (union, subtract, intersect)
- **Bezier Patches**: Bicubic parametric surfaces with Newton-Raphson ray tracing (Utah teapot included)
- **Physically-Based Materials**: Diffuse, metal, glass, emissive with configurable properties
- **S-Expression Scene Format**: Clean, declarative scene definition with includes support
- **Interactive Camera**: Orbit controls with mouse (drag to rotate, scroll to zoom)

## Quick Start

```bash
# Build
mkdir build && cd build
cmake ..
cmake --build .

# Run with demo scene
./ray --scene ../scenes/demo.scene

# Take screenshot after 100 frames
./ray --scene ../scenes/teapot.scene --screenshot teapot.png --frames 100
```

## Requirements

- **CMake** 3.20 or higher
- **Qt6** (Core, Gui, Widgets)
- **Vulkan SDK** 1.2 or higher
- **glslc** shader compiler (included with Vulkan SDK)
- C++17 compatible compiler

### Installing Dependencies

**Arch Linux:**
```bash
sudo pacman -S cmake qt6-base vulkan-devel
```

**Ubuntu/Debian:**
```bash
sudo apt install cmake qt6-base-dev vulkan-sdk
```

**macOS:**
```bash
brew install cmake qt6 vulkan-sdk
```

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

For optimized release build:
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

## Usage

### Command Line Options

```bash
./ray [OPTIONS]

Options:
  -s, --scene <file>       Load scene file (.scene format)
  --screenshot <file>      Take screenshot and optionally exit
  --frames <count>         Frames to accumulate before screenshot (default: 30)
  --no-exit               Don't exit after screenshot (keep window open)
  -d, --debug             Enable Vulkan validation layers
  -h, --help              Show help message
  -v, --version           Show version
```

### Examples

```bash
# Load and display a scene
./ray --scene scenes/demo.scene

# Render scene to image (headless mode)
./ray --scene scenes/demo.scene --screenshot output.png --frames 200

# Debug mode with validation layers
./ray --scene scenes/demo.scene --debug

# Interactive mode with screenshot option
./ray --scene scenes/demo.scene --screenshot output.png --no-exit
```

### Interactive Controls

- **Left mouse drag**: Orbit camera around scene
- **Right mouse drag**: Pan camera
- **Scroll wheel**: Zoom in/out
- **S**: Save screenshot to `screenshot.png`
- **Esc**: Quit application

## Scene Files

Scenes are defined using S-expressions. Here's a minimal example:

```lisp
; Define materials
(material red (type metal) (rgb 0.9 0.2 0.2) (roughness 0.05))
(material glass (type glass) (rgb 0.95 0.95 0.95) (ior 1.5))

; Add primitives
(shape (sphere (at 0 1 0) (r 1.0)) red)
(shape (box (at 3 1 0) (half 0.8 0.8 0.8)) glass)

; CSG operations
(shape (subtract
         (sphere (at -3 1 0) (r 1.2))
         (box (at -3 1 0) (half 0.6 1.5 0.6)))
       red)

; Bezier patch instances (Utah teapot)
(include "teapot.scene")
(instance teapot (at 0 0 -5) (scale 0.5) (rotate 0 45 0) glass)
```

See [docs/scene-format.md](docs/scene-format.md) for complete syntax reference.

## Project Structure

```
raydemo/
├── src/
│   ├── core/                   # Core rendering library (header-only)
│   │   ├── types.h            # Vec3, math utilities
│   │   ├── scene.h            # Scene container (legacy)
│   │   ├── materials.h        # Material types
│   │   └── renderer.h/cpp     # Vulkan renderer
│   │
│   ├── parametric/            # Parametric primitives library
│   │   ├── types.h            # Shared types (Vec3, AABB, Ray)
│   │   ├── bezier/            # Bezier patch implementation
│   │   │   ├── patch.h        # Patch data structure
│   │   │   ├── subdivision.h # De Casteljau subdivision
│   │   │   ├── bvh.h          # BVH acceleration
│   │   │   └── instance.h     # Transform instances
│   │   ├── csg/               # CSG tree data structures
│   │   │   └── csg.h          # CSGScene, CSGNode, CSGPrimitive
│   │   ├── materials/         # Material library
│   │   │   └── material.h     # MaterialLibrary
│   │   └── scene/             # Scene loader
│   │       ├── sexp.h         # S-expression parser
│   │       └── scene_loader.h # .scene file loader
│   │
│   └── apps/                  # Applications
│       ├── ray/               # Main ray tracer
│       │   ├── main.cpp       # Entry point
│       │   └── ray_renderer.cpp
│       └── scenecheck/        # Scene file validator
│           └── main.cpp
│
├── shaders/
│   ├── ray.comp              # Main compute shader
│   └── includes/
│       ├── bezier.glsl       # Bezier intersection (Newton-Raphson)
│       ├── csg.glsl          # CSG tree traversal
│       ├── sdf.glsl          # Signed distance functions
│       ├── materials.glsl    # Material scattering
│       ├── geometry.glsl     # Ray-primitive intersections
│       ├── noise.glsl        # Procedural noise
│       └── random.glsl       # RNG (PCG)
│
├── scenes/                    # Example scene files
│   ├── demo.scene            # CSG showcase + teapots
│   └── teapot.scene          # Utah teapot Bezier patch data
│
└── docs/
    ├── architecture.md       # Code structure overview
    ├── scene-format.md       # Scene file syntax reference
    ├── materials.md          # Material system guide
    └── adr/                  # Architecture Decision Records
```

## Documentation

- [Architecture Guide](docs/architecture.md) - Code structure and data flow
- [Scene Format Reference](docs/scene-format.md) - Complete .scene file syntax
- [Materials Guide](docs/materials.md) - Material types and properties
- [ADRs](docs/adr/) - Architecture Decision Records

## CLI Tools

### scenecheck

Validates scene files and reports statistics:

```bash
./scenecheck ../scenes/demo.scene
```

Output:
```
Scene loaded successfully!
Primitives: 45
CSG nodes: 92
Materials: 6
Patch groups: 1 (teapot: 32 patches)
Instances: 2
```

## Technical Details

- **Compute shader based**: No hardware ray tracing required (runs on any Vulkan 1.2 GPU)
- **Newton-Raphson intersection**: Direct ray-Bezier intersection without tessellation
- **De Casteljau subdivision**: CPU-side patch subdivision for BVH acceleration
- **SDF-based CSG**: Smooth boolean operations using signed distance fields
- **Progressive accumulation**: Multi-sample anti-aliasing with temporal accumulation

## Performance

Typical performance on RTX 3070 (1920x1080):
- Simple scenes (5-10 primitives): 60 fps
- Complex CSG (40+ operations): 30-45 fps
- Bezier patches (Utah teapot): 60+ fps with BVH

## Contributing

This is a personal research project. Issues and pull requests welcome.

## License

MIT License - see LICENSE file for details.

## References

- [ADR-007: SDF-based CSG](docs/adr/ADR-007-sdf-based-csg.md)
- [ADR-008: Parametric Primitives Library](docs/adr/ADR-008-parametric-primitives-library.md)
- Kajiya 1982: Ray Tracing Parametric Patches
- Inigo Quilez: SDF functions and CSG operations

## Acknowledgments

- **Utah Teapot**: Original model by Martin Newell (1975)
- **S-Expression Parser**: Custom implementation for scene format
