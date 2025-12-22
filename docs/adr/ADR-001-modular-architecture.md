# ADR-001: Modular Architecture Refactoring

Status: Proposed
Date: 2025-12-21
Deciders: @aaron, @claude

## Context

The path tracer has grown organically from a simple demo into a feature-rich renderer with:
- Multiple material types (diffuse, metal, glass, dichroic, procedural textures)
- Multiple geometry types (spheres, boxes)
- Multiple light types (sun/sky, spotlights with gobos)
- Adaptive sampling and convergence detection
- Interactive camera controls

Currently, most code lives in:
- `scene.h` - structs, enums, camera, and `createTestScene()`
- `renderer.cpp` - Vulkan setup, buffers, rendering
- `raytrace.comp` - all shader logic (1100+ lines)

This works but makes it hard to:
- Add new geometry types (meshes, triangles)
- Add new material types without touching multiple files
- Save/load scenes
- Test components in isolation

## Decision

Refactor into a modular architecture with clear separation of concerns:

### Core Modules

| Module | Files | Responsibility |
|--------|-------|----------------|
| **Geometry** | `geometry.h/.cpp` | Base class + Sphere, Box, future Mesh/BVH |
| **Materials** | `materials.h/.cpp` | Material types, procedural textures |
| **Lights** | `lights.h/.cpp` | Sun, SpotLight, future area lights |
| **Scene** | `scene.h/.cpp` | Object collection, serialization |
| **Camera** | `camera.h/.cpp` | OrbitCamera, future fly camera, DOF |
| **Sky** | `sky.h/.cpp` | Atmosphere model, stars, time of day |
| **Renderer** | `renderer.h/.cpp` | Vulkan orchestration, buffer management |
| **Sampler** | `sampler.h/.cpp` | Adaptive sampling, convergence |

### Shader Organization

Split `raytrace.comp` into includable modules:
```
shaders/
  raytrace.comp          # main entry point
  includes/
    random.glsl          # RNG functions
    noise.glsl           # Perlin, FBM, Worley
    materials.glsl       # material evaluation
    geometry.glsl        # intersection tests
    lights.glsl          # light sampling, gobos
    sky.glsl             # atmosphere, stars
    bvh.glsl             # future: acceleration structure
```

### Geometry Abstraction

```cpp
class Geometry {
public:
    virtual ~Geometry() = default;
    virtual GeometryType type() const = 0;
    virtual AABB bounds() const = 0;
    virtual void upload(VkBuffer& buffer, VkDeviceMemory& memory) = 0;
};

class Sphere : public Geometry { ... };
class Box : public Geometry { ... };
class TriangleMesh : public Geometry { ... };  // future
```

### Scene API

```cpp
Scene scene;
scene.add<Sphere>(center, radius, materialId);
scene.add<Box>(center, halfExtents, materialId);
scene.add<Mesh>(Mesh::loadOBJ("bunny.obj"), materialId);  // future
scene.setMaterial(id, Material{...});
scene.addLight<SpotLight>(...);

// Serialization
scene.save("scene.json");
scene.load("scene.json");
```

## Consequences

### Positive
- Easier to add new geometry types (meshes, CSG)
- Easier to add new materials without monolithic changes
- Scene save/load enables sharing and iteration
- Testable components
- Cleaner mental model

### Negative
- Upfront refactoring effort
- More files to navigate
- Some abstraction overhead (minimal for GPU-bound work)
- Shader includes need glslc support or manual concatenation

### Neutral
- Will need to decide on serialization format (JSON, binary, custom)
- BVH implementation is a separate significant effort

## Alternatives Considered

1. **Keep monolithic** - Works but increasingly painful
2. **Full ECS architecture** - Overkill for this scale
3. **Header-only modules** - Considered, but .cpp files help compile times

## Implementation Plan

1. Create branch `refactor/modular-architecture`
2. Extract Camera (already fairly isolated)
3. Extract Materials and Geometry structs
4. Extract Lights
5. Create Scene container with add/remove API
6. Split shader into includes
7. Add basic JSON serialization
8. (Future) Add Mesh/BVH support

## Open Questions

- Shader include strategy: glslc `-I` flag vs preprocessor concatenation?
- Scene format: JSON for human-readable, or binary for large meshes?
- Should materials be referenced by ID or pointer?
