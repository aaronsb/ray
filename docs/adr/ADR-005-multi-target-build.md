# ADR-005: Multi-Target Build System

Status: Proposed
Date: 2025-12-21
Deciders: @aaron, @claude

## Context

Following ADR-003's modular architecture refactoring, we now have clean separation between:
- Core types and utilities (Vec3, camera, materials, geometry, lights)
- Scene management (Scene class with serialization)
- Vulkan rendering (renderer)
- Demo-specific scene setup (createTestScene)

Currently everything builds into a single `raydemo` executable. As we explore different use cases (voxels, landscapes, physics demos), we want multiple executables sharing the core rendering infrastructure.

## Decision

Restructure into a static library plus multiple demo executables.

### Directory Structure

```
src/
  core/                    # Static library: libraycore
    types.h
    camera.h / camera.cpp
    materials.h
    geometry.h
    lights.h
    scene.h
    renderer.h / renderer.cpp

  demos/
    raydemo/
      main.cpp
      test_scene.cpp       # createTestScene()
    voxeldemo/
      main.cpp
      voxel_scene.cpp      # createVoxelScene()
    landscapedemo/
      main.cpp
      landscape_scene.cpp  # createLandscapeScene()

shaders/                   # Shared shaders (compiled once)
  raytrace.comp
  includes/
    ...
```

### CMake Structure

```cmake
# Core library
add_library(raycore STATIC
    src/core/camera.cpp
    src/core/renderer.cpp
)
target_include_directories(raycore PUBLIC src/core)
target_link_libraries(raycore PUBLIC Qt6::Gui Vulkan::Vulkan)

# Shader compilation (shared)
add_custom_target(shaders ...)

# Demo executables
function(add_demo name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE raycore)
    add_dependencies(${name} shaders)
endfunction()

add_demo(raydemo
    src/demos/raydemo/main.cpp
    src/demos/raydemo/test_scene.cpp
)

add_demo(voxeldemo
    src/demos/voxeldemo/main.cpp
    src/demos/voxeldemo/voxel_scene.cpp
)
```

### Demo Interface

Each demo provides:
1. `main.cpp` - Window setup, event handling (can share a common pattern)
2. `*_scene.cpp` - Scene factory function returning a `Scene` object

```cpp
// demos/voxeldemo/voxel_scene.cpp
#include <scene.h>

Scene createVoxelScene() {
    Scene scene;
    // Voxel-specific geometry and materials
    return scene;
}
```

## Consequences

### Positive
- Share rendering code across demos without duplication
- Faster incremental builds (core compiled once)
- Clear separation between infrastructure and demo content
- Easy to add new demos
- Each demo can be built/run independently

### Negative
- More complex directory structure
- Need to move existing files
- Slightly more CMake complexity

### Neutral
- Shaders remain shared (all demos use same path tracer)
- May eventually want per-demo shader variants

## Alternatives Considered

1. **OBJECT library** - Simpler but less flexible for future needs
2. **Header-only core** - Recompiles everything per target, slower builds
3. **Monorepo with subprojects** - Overkill for current scale

## Implementation Plan

1. Create `src/core/` directory
2. Move core headers and create .cpp files where needed
3. Update CMakeLists.txt with library target
4. Create `src/demos/raydemo/` and move main.cpp
5. Extract createTestScene() to separate file
6. Add `add_demo()` CMake function
7. Verify raydemo still builds and runs
8. (Future) Add voxeldemo, landscapedemo as needed

## Open Questions

- Should demos share a common main.cpp pattern, or each be independent?
- Do we need per-demo shader variants eventually?
- Should Scene have a virtual factory pattern, or just free functions?
