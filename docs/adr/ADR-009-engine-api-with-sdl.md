# ADR-009: Engine API with Scene Description Language

Status: Accepted
Date: 2025-12-24
Deciders: @aaron, @claude

## Context

The ray tracer has evolved from a Bezier patch demo to a capable renderer with:
- Bezier patch ray tracing (Utah teapot)
- Interval-based CSG (sphere, box, cylinder, cone, torus, plane, disc, prism)
- Multiple materials (diffuse, metal, glass)
- Temporal accumulation, shadows, reflections, refractions

Currently scenes are hardcoded in C++/GLSL. We need:
1. Programmatic scene construction (for physics, games, tools)
2. Human-readable scene files (for artists, reproducibility)
3. Eventually: live-reload workflow (edit file → see changes instantly)

## Decision

Build an **API-first architecture** with SDL as a client:

### Phase 1: Engine API (C++)

```cpp
class Scene {
    SphereId addSphere(vec3 center, float radius, MaterialId mat);
    BoxId addBox(vec3 center, vec3 halfExtents, MaterialId mat);
    CSGId addCSG(CSGOp op, PrimitiveId a, PrimitiveId b, MaterialId mat);
    // ...

    MaterialId addMaterial(MaterialType type, vec3 color, float roughness);
    void setCamera(vec3 pos, vec3 target, float fov);

    void update();  // Upload to GPU
};

class Renderer {
    void render(const Scene& scene);
    void saveScreenshot(const std::string& path);
};
```

### Phase 2: Static SDL Loader

Simple text format parsed at startup:

```
# example.scene
material glass { type: glass, color: [1, 1, 1], ior: 1.5 }
material red_metal { type: metal, color: [0.9, 0.2, 0.2], roughness: 0.1 }

sphere { center: [0, 1, 0], radius: 1.5, material: glass }
box { center: [2, 0.5, 0], size: [1, 1, 1], material: red_metal }

csg subtract {
    sphere { center: [-2, 1, 0], radius: 1.5 }
    box { center: [-2, 1, 0], size: [0.7, 2, 0.7] }
    material: red_metal
}

camera { position: [0, 3, 10], target: [0, 1, 0], fov: 60 }
```

Parser translates to API calls:
```cpp
Scene scene;
auto glass = scene.addMaterial(Glass, {1,1,1}, 1.5);
scene.addSphere({0,1,0}, 1.5, glass);
// ...
```

### Phase 3: Live SDL (Future)

- File watcher (inotify/QFileSystemWatcher)
- Re-parse on save
- If valid: update scene, preserve camera, reset accumulation
- If error: keep old scene, show error overlay

## Consequences

### Positive
- API enables physics, games, procedural generation
- SDL enables artists, reproducibility, version control
- Live SDL (future) enables rapid iteration workflow
- Each layer is independently useful

### Negative
- More code to maintain (API + parser)
- Need to keep SDL syntax and API in sync
- SDL design choices may limit API flexibility

### Neutral
- Current hardcoded scene becomes the template for SDL design
- Shader code remains separate (SDL describes what, not how)

## Implementation Plan

1. Add rich CSG test scene to current ray.comp (design template)
2. Extract Scene API from current hardcoded setup
3. Design SDL syntax based on template scene
4. Implement SDL parser
5. (Future) Add file watcher for live reload
