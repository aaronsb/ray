# Architecture Guide

This document provides an overview of the Ray's Bouncy Castle codebase structure, key components, and data flow.

## Overview

Ray's Bouncy Castle is a GPU path tracer built on:
- **Vulkan compute shaders** for ray tracing
- **Qt6** for windowing and UI
- **S-expression scene format** for declarative scene definition
- **Parametric primitives library** for geometry representation

The architecture follows a modular design with clear separation between:
- **Core rendering** (`src/core/`) - Vulkan renderer, camera, scene container
- **Parametric library** (`src/parametric/`) - CSG, Bezier patches, materials, scene loader
- **Applications** (`src/apps/`) - `ray` tracer, `scenecheck` validator

## Directory Layout

```
src/
├── core/                       # Core rendering library (header-only)
│   ├── types.h                # Vec3, math utilities
│   ├── camera.h               # OrbitCamera with mouse controls
│   ├── materials.h            # Material type enum (legacy)
│   ├── geometry.h             # Geometry structs (legacy)
│   ├── lights.h               # SpotLight with gobo patterns
│   ├── scene.h                # Scene container (legacy API)
│   └── renderer.h/cpp         # Vulkan renderer and window
│
├── parametric/                # Parametric primitives library
│   ├── types.h                # Vec3, AABB, Ray (shared)
│   │
│   ├── bezier/                # Bezier patch implementation
│   │   ├── patch.h            # Patch data structure (4x4 control points)
│   │   ├── subdivision.h      # De Casteljau subdivision
│   │   ├── bvh.h              # BVH acceleration structure
│   │   ├── patch_group.h      # Collection of patches with BVH
│   │   └── instance.h         # Transform instance (position, rotation, scale)
│   │
│   ├── csg/                   # Constructive Solid Geometry
│   │   └── csg.h              # CSGScene, CSGNode, CSGPrimitive
│   │
│   ├── materials/             # Material library
│   │   ├── material.h         # Material struct, MaterialLibrary
│   │   └── presets/           # Preset materials (WIP)
│   │
│   └── scene/                 # Scene file loader
│       ├── sexp.h             # S-expression parser
│       └── scene_loader.h     # .scene file → SceneData
│
└── apps/                      # Applications
    ├── ray/                   # Main ray tracer
    │   ├── main.cpp           # Entry point, CLI parsing
    │   └── ray_renderer.cpp   # RayWindow (Vulkan + scene integration)
    │
    └── scenecheck/            # Scene file validator
        └── main.cpp           # CLI tool to parse and validate scenes

shaders/
├── ray.comp                   # Main compute shader
└── includes/
    ├── bezier.glsl            # Bezier patch intersection (Newton-Raphson)
    ├── csg.glsl               # CSG tree traversal + boolean ops
    ├── sdf.glsl               # Signed distance functions for primitives
    ├── materials.glsl         # Material scattering (BRDF/BTDF)
    ├── geometry.glsl          # Ray-primitive intersection utilities
    ├── noise.glsl             # Procedural noise (Perlin, FBM)
    └── random.glsl            # PCG random number generator

scenes/
├── demo.scene                 # CSG showcase with teapot instances
└── teapot.scene               # Utah teapot Bezier patch data
```

## Key Components

### 1. Renderer (src/core/renderer.h/cpp)

The `Renderer` class manages Vulkan resources:
- **Compute pipeline** with `ray.comp` shader
- **Storage buffers** for geometry, materials, BVH nodes
- **Push constants** for per-frame parameters
- **Image storage** for accumulation

Key responsibilities:
- Create Vulkan resources (buffers, descriptors, pipeline)
- Upload scene data to GPU
- Dispatch compute shader each frame
- Handle window resize, screenshot capture

### 2. Scene Loader (src/parametric/scene/scene_loader.h)

Parses `.scene` files and builds scene data structures:

```cpp
SceneData data;
SceneLoader::loadFile("demo.scene", data);

// Access parsed data:
data.csg;              // CSGScene (primitives + CSG tree)
data.materials;        // MaterialLibrary
data.patchGroups;      // Map<name, vector<Patch>>
data.patchInstances;   // Instance transforms
```

**Input**: S-expression scene file
**Output**: `SceneData` struct with:
- `CSGScene` - primitives and CSG tree nodes
- `MaterialLibrary` - named materials
- `patchGroups` - Bezier patch data by name
- `patchInstances` - instances with transforms

### 3. CSG System (src/parametric/csg/csg.h)

Constructive Solid Geometry using tree representation:

```cpp
CSGScene csg;

// Add primitives
uint32_t sphere = csg.addSphere(0, 0, 0, 1.0f);
uint32_t box = csg.addBox(0, 0, 0, 0.6f, 1.2f, 0.6f);

// Create CSG tree
uint32_t sphereNode = csg.addPrimitiveNode(sphere, materialId);
uint32_t boxNode = csg.addPrimitiveNode(box, materialId);
uint32_t subtract = csg.addSubtract(sphereNode, boxNode, materialId);

// Mark as root (visible shape)
csg.addRoot(subtract);
```

**GPU Representation**:
- `CSGPrimitive[]` - geometric parameters (center, radius, etc.)
- `CSGNode[]` - tree nodes (type, left, right, materialId)
- `uint32_t[] roots` - indices of root nodes to render

Shader traverses the CSG tree using SDF evaluation and boolean operations.

### 4. Bezier Patches (src/parametric/bezier/)

Bicubic Bezier patch ray tracing pipeline:

**CPU Side**:
1. **Load patches** - 4x4 control point grids
2. **De Casteljau subdivision** - recursively subdivide until "flat enough"
3. **BVH construction** - build acceleration structure over sub-patches
4. **Upload to GPU** - sub-patches, BVH nodes, instances

**GPU Side** (shaders/includes/bezier.glsl):
1. **BVH traversal** - find candidate patches
2. **Newton-Raphson** - solve for (u,v) intersection
3. **Normal computation** - partial derivatives ∂S/∂u × ∂S/∂v

```cpp
BezierPatchGroup group;
group.build(patches, maxDepth=4, flatnessThreshold=0.05f);

// Create instance with transform
BezierInstance inst;
inst.posX = 0; inst.posY = 0; inst.posZ = -5;
inst.scale = 0.5f;
inst.rotY = 45.0f * DEG2RAD;
inst.materialId = glassId;
```

### 5. Material System (src/parametric/materials/material.h)

Materials define surface appearance:

```cpp
enum class MaterialType {
    Diffuse,    // Lambertian
    Metal,      // Conductor with roughness
    Glass,      // Dielectric with IOR
    Emissive,   // Light-emitting
};

struct Material {
    float r, g, b;      // albedo color
    uint32_t type;      // MaterialType
    float roughness;    // 0=mirror, 1=rough
    float metallic;     // 0=dielectric, 1=metal
    float ior;          // index of refraction
    float emissive;     // emission multiplier
};
```

**MaterialLibrary** provides named material lookup:

```cpp
MaterialLibrary lib;
lib.add("red", {0.9f, 0.2f, 0.2f, MaterialType::Metal, 0.05f, ...});
lib.add("glass", {0.95f, 0.95f, 0.95f, MaterialType::Glass, 0.0f, ...});

uint32_t id = lib.find("glass");
```

## Data Flow

### Scene Loading → GPU Upload → Rendering

```
┌─────────────────┐
│  demo.scene     │  S-expression file
└────────┬────────┘
         │ SceneLoader::loadFile()
         v
┌─────────────────┐
│   SceneData     │  CPU-side scene representation
│  - CSGScene     │  - Primitives + CSG tree
│  - Materials    │  - Named materials
│  - Patches      │  - Bezier control points
│  - Instances    │  - Transforms
└────────┬────────┘
         │ RayWindow::loadScene()
         │ + BezierPatchGroup::build()
         v
┌─────────────────┐
│ GPU Buffers     │  Vulkan storage buffers
│  - csgPrims     │  CSGPrimitive[]
│  - csgNodes     │  CSGNode[]
│  - materials    │  Material[]
│  - bezierPatches│  BezierPatch[] (subdivided)
│  - bvhNodes     │  BVHNode[]
│  - instances    │  BezierInstance[]
└────────┬────────┘
         │ vkCmdDispatch()
         v
┌─────────────────┐
│   ray.comp      │  Compute shader
│  - Trace rays   │  - Ray-CSG intersection
│  - BVH traverse │  - Ray-Bezier intersection
│  - Material     │  - BRDF/BTDF scattering
│  - Accumulate   │  - Progressive sampling
└────────┬────────┘
         │
         v
┌─────────────────┐
│  Frame buffer   │  Display
└─────────────────┘
```

### Detailed Trace

1. **Parse scene file** (`.scene` → `SceneData`)
   - S-expression parser tokenizes input
   - `SceneLoader` builds CSG tree, material library, patch data

2. **Preprocess geometry** (CPU)
   - Bezier patches: De Casteljau subdivision → BVH
   - CSG tree: already in GPU-ready format

3. **Upload to GPU** (`SceneData` → Vulkan buffers)
   - Allocate storage buffers
   - Copy data: `vkCmdUpdateBuffer()` or staging buffers
   - Bind to descriptor sets

4. **Per-frame dispatch**
   - Update push constants (frameIndex, camera, etc.)
   - Dispatch compute shader: `(width/8, height/8, 1)` workgroups
   - Each invocation traces 1+ rays per pixel

5. **Shader execution** (`ray.comp`)
   ```glsl
   // Primary ray from camera
   Ray ray = generateCameraRay(pixelCoord, camera);

   // Intersect scene
   HitRecord hit;
   if (intersectScene(ray, hit)) {
       // CSG intersection via SDF
       intersectCSG(ray, hit);

       // Bezier patch intersection via BVH + Newton-Raphson
       intersectBezier(ray, hit);
   }

   // Shade hit point
   Material mat = materials[hit.materialId];
   vec3 color = evaluateMaterial(mat, ray, hit);

   // Accumulate
   imageStore(accumulationBuffer, pixelCoord, vec4(color, 1));
   ```

6. **Display**
   - Vulkan presents swapchain image
   - Accumulation buffer averaged over frames

## Shader Architecture

### ray.comp

Main compute shader layout:

```glsl
#version 450
#extension GL_EXT_control_flow_attributes : enable

layout(local_size_x = 8, local_size_y = 8) in;

// Push constants (per-frame)
layout(push_constant) uniform PushConstants {
    uint frameIndex;
    uint sampleCount;
    // ... camera, scene stats
};

// Storage buffers (scene data)
layout(set = 0, binding = 0) buffer CSGPrimitives { ... };
layout(set = 0, binding = 1) buffer CSGNodes { ... };
layout(set = 0, binding = 2) buffer Materials { ... };
layout(set = 0, binding = 3) buffer BezierPatches { ... };
layout(set = 0, binding = 4) buffer BVHNodes { ... };

// Image output
layout(set = 0, binding = 10, rgba32f) uniform image2D accumulationImage;

void main() {
    // Pixel coordinate
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);

    // Generate camera ray
    Ray ray = cameraRay(pixel, camera);

    // Path trace
    vec3 color = tracePath(ray);

    // Accumulate
    vec4 prev = imageLoad(accumulationImage, pixel);
    vec4 next = mix(prev, vec4(color, 1), 1.0 / frameIndex);
    imageStore(accumulationImage, pixel, next);
}
```

### Includes

Modular GLSL includes:

- **sdf.glsl**: SDF primitives (sphere, box, cylinder, cone, torus)
- **csg.glsl**: CSG tree traversal, boolean operations
- **bezier.glsl**: Newton-Raphson patch intersection
- **materials.glsl**: BRDF/BTDF evaluation
- **geometry.glsl**: Ray-AABB, ray-plane utilities
- **random.glsl**: PCG random number generator
- **noise.glsl**: Perlin noise, FBM

Shaders are compiled with `-I shaders/includes` so they can:

```glsl
#include "sdf.glsl"
#include "csg.glsl"
#include "bezier.glsl"
```

## Build System

CMake structure:

```cmake
# Core library (header-only, interface target)
add_library(raycore INTERFACE)
target_include_directories(raycore INTERFACE src/core src/parametric)
target_link_libraries(raycore INTERFACE Qt6::Core Vulkan::Vulkan)

# Helper function for apps
function(add_demo name dir)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE raycore Qt6::Widgets)
    # Shader dependency + copy to build dir
    add_dependencies(${name} shaders)
endfunction()

# Applications
add_demo(ray src/apps/ray main.cpp ray_renderer.cpp)
add_executable(scenecheck src/apps/scenecheck/main.cpp)
```

**Shader Compilation**:

```cmake
foreach(SHADER ${SHADERS})
    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND glslc -O --target-env=vulkan1.2
                -I${SHADER_SOURCE_DIR}/includes
                ${SHADER_SRC} -o ${SHADER_SPV}
        DEPENDS ${SHADER_SRC} ${SHADER_INCLUDES}
    )
endforeach()
```

## Extension Points

### Adding a New Primitive

1. **Define primitive** in `src/parametric/csg/csg.h`:
   ```cpp
   enum class CSGPrimType { ..., MyPrimitive };
   uint32_t CSGScene::addMyPrimitive(float x, float y, float z, ...);
   ```

2. **Add SDF** in `shaders/includes/sdf.glsl`:
   ```glsl
   float sdMyPrimitive(vec3 p, vec3 params) { ... }
   ```

3. **Update shader dispatch** in `shaders/includes/csg.glsl`:
   ```glsl
   case PRIM_MY_PRIMITIVE:
       d = sdMyPrimitive(p, ...);
       break;
   ```

4. **Scene loader** in `src/parametric/scene/scene_loader.h`:
   ```cpp
   if (type == "my-primitive") {
       uint32_t prim = data_.csg.addMyPrimitive(...);
       return data_.csg.addPrimitiveNode(prim, matId);
   }
   ```

### Adding a New Material Type

1. **Extend enum** in `src/parametric/materials/material.h`:
   ```cpp
   enum class MaterialType { ..., MyMaterial };
   ```

2. **Implement BRDF** in `shaders/includes/materials.glsl`:
   ```glsl
   case MAT_MY_MATERIAL:
       scatteredRay = myMaterialScatter(ray, hit, mat);
       break;
   ```

3. **Scene loader** support in `scene_loader.h`:
   ```cpp
   if (typeName == "my-material")
       mat.type = MaterialType::MyMaterial;
   ```

## Performance Considerations

### CPU Side

- **Bezier subdivision**: Done once at load time, cached in BVH
- **Scene loading**: Fast (S-expression parser is simple)
- **GPU upload**: Typically < 10ms for complex scenes

### GPU Side

- **CSG intersection**: O(tree depth), typically 3-5 nodes
- **BVH traversal**: O(log n) for n patches
- **Newton-Raphson**: 10 iterations, converges quickly with subdivision
- **Path tracing**: 1-8 bounces per ray

**Bottlenecks**:
- Deep CSG trees (prefer balanced trees)
- Large BVH nodes (ensure patches are well-subdivided)
- High bounce counts (exponential path explosion)

**Optimizations**:
- BVH with tight AABBs (reduces false positives)
- Early ray termination (opacity culling)
- Importance sampling for materials
- Next Event Estimation (NEE) for direct lighting

## Related Documentation

- [Scene Format Reference](scene-format.md) - `.scene` file syntax
- [Materials Guide](materials.md) - Material types and properties
- [ADR-007: SDF-based CSG](adr/ADR-007-sdf-based-csg.md)
- [ADR-008: Parametric Primitives Library](adr/ADR-008-parametric-primitives-library.md)
