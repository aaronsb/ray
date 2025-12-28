# Raydemo - GPU Ray Tracing Playground

## Build

```bash
cmake --build build
```

## Applications

### ray - RayVen (Ray Tracing Engine)

```bash
./build/ray [options]
```

**Options:**
- `-h, --help` - Display help
- `-v, --version` - Display version
- `-s, --scene <file>` - Scene file to load (.scene format)
- `--screenshot <filename>` - Take screenshot on start and exit (filename optional)
- `--frames <count>` - Frames to accumulate before screenshot (default: 30)
- `--no-exit` - Keep window open after screenshot
- `-d, --debug` - Enable Vulkan validation layers

**Example:**
```bash
./build/ray -s scenes/caustics.scene --screenshot /tmp/test.png --frames 30
```

**Controls:**
- Left-click drag: Rotate camera (orbit)
- Right-click drag: Pan camera (dolly/truck)
- Scroll wheel: Zoom
- S key: Save screenshot

## Architecture

- `src/apps/ray/` - Ray tracer application
- `src/parametric/bezier/` - Bezier patch library (subdivision, BVH)
- `shaders/ray.comp` - Main compute shader
- `shaders/includes/csg/` - CSG interval-based primitives

## ADRs

See `docs/adr/` for architecture decisions.
