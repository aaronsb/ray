# ADR-001: Vulkan Ray Tracing Architecture for AMD RDNA3

Status: Accepted
Date: 2025-12-21
Updated: 2025-12-21
Deciders: @aaron, @claude

## Context

We want to implement a "close to the metal" educational ray tracer on AMD hardware (RX 7900 XT/XTX, RDNA3) that:
- Achieves real-time performance (30+ fps target)
- Demonstrates core ray tracing concepts (reflection, refraction, shadows, lighting)
- Displays in a simple Qt window
- Leverages AMD's hardware Ray Accelerators

### Hardware Capabilities
- **GPU**: AMD RX 7900 XT/XTX (Navi 31, RDNA3)
- **Ray Accelerators**: 2nd generation, ~50% faster than RDNA2
- **Vulkan**: 1.4.335 with full RT extension support
  - VK_KHR_acceleration_structure ✓
  - VK_KHR_ray_tracing_pipeline ✓
  - VK_KHR_ray_query ✓
  - maxRayRecursionDepth = 31

### Options Evaluated

**GPU API Options**:
1. **Vulkan RT (VK_KHR_ray_tracing_pipeline)** - Full hardware RT, high boilerplate
2. **Vulkan Ray Query (VK_KHR_ray_query)** - Hardware RT in compute shaders, medium boilerplate
3. **HIP/ROCm** - AMD's CUDA equivalent, no hardware RT access
4. **OpenCL** - Compute-only, no hardware RT access

**Qt Integration Options**:
1. **QVulkanWindow** - Native Vulkan integration, minimal overhead
2. **QOpenGLWidget + interop** - Adds unnecessary complexity
3. **Raw window handle** - Maximum control, more platform-specific code

## Decision

### 1. Use Vulkan with VK_KHR_ray_query (Compute Shader + Hardware RT)

**Rationale**:
- Accesses hardware Ray Accelerators (2-10x faster than compute-only approaches)
- Simpler than full ray tracing pipeline (no shader binding tables)
- Compute shaders give full control over algorithm
- 30fps achievable at 1080p with proper optimization
- Skills transfer to professional graphics programming

**Trade-offs accepted**:
- More boilerplate than HIP/OpenCL
- Must manage acceleration structures (BLAS/TLAS)

### 2. Use QVulkanWindow for display

**Rationale**:
- Direct swapchain access, near-zero Qt overhead
- Built-in frame synchronization
- Platform abstraction (X11/Wayland/Windows without #ifdefs)
- ~150 lines boilerplate vs ~250 for raw handle approach

### 3. Incremental implementation following "Ray Tracing in One Weekend" patterns

**Phase 1: Prove the Path** (MVP)
- Spheres only (analytic intersection, no mesh loading)
- Diffuse (Lambertian) + Emissive materials
- Path tracing with loop-based bounces (3-5 max)
- 1 sample per pixel (accept noise)
- Fixed camera
- Simple gamma correction
- Target: visible image with color bleeding

**Phase 2: Visual Quality**
- Antialiasing (4-16 samples/pixel)
- Reflective (metal) materials
- Refractive (glass) materials with Fresnel
- Better random numbers (PCG)
- Moveable camera

**Phase 3: Performance & Complexity**
- BVH acceleration structure for triangles
- Mesh loading (OBJ)
- Environment maps
- Tone mapping (Reinhard or ACES)

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        Qt Application                        │
│  ┌─────────────────────────────────────────────────────────┐│
│  │                    QVulkanWindow                         ││
│  │  ┌─────────────────────────────────────────────────────┐││
│  │  │              RayTracingRenderer                      │││
│  │  │                                                      │││
│  │  │  startNextFrame() {                                  │││
│  │  │    1. Update uniforms (camera, frame number)         │││
│  │  │    2. Dispatch compute shader (ray tracing)          │││
│  │  │    3. Blit storage image → swapchain                 │││
│  │  │    4. Present                                        │││
│  │  │  }                                                   │││
│  │  └─────────────────────────────────────────────────────┘││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                     Vulkan Resources                         │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────────┐  │
│  │ Acceleration │  │   Storage    │  │  Compute Pipeline │  │
│  │  Structure   │  │    Image     │  │   (ray tracing)   │  │
│  │  (TLAS/BLAS) │  │  (RGBA32F)   │  │                   │  │
│  └──────────────┘  └──────────────┘  └───────────────────┘  │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────────┐  │
│  │   Spheres    │  │  Materials   │  │  Camera Uniforms  │  │
│  │   Buffer     │  │   Buffer     │  │                   │  │
│  └──────────────┘  └──────────────┘  └───────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Compute Shader Structure (GLSL)

```glsl
#version 460
#extension GL_EXT_ray_query : enable

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba32f, set = 0, binding = 0) uniform image2D outputImage;
layout(set = 0, binding = 1) uniform accelerationStructureEXT topLevelAS;

struct Ray { vec3 origin; vec3 direction; };
struct HitInfo { vec3 point; vec3 normal; uint materialId; };

vec3 tracePath(Ray ray, uint maxBounces) {
    vec3 throughput = vec3(1.0);
    vec3 radiance = vec3(0.0);

    for (uint bounce = 0; bounce < maxBounces; bounce++) {
        rayQueryEXT rayQuery;
        rayQueryInitializeEXT(rayQuery, topLevelAS,
            gl_RayFlagsOpaqueEXT, 0xFF,
            ray.origin, 0.001, ray.direction, 10000.0);

        while (rayQueryProceedEXT(rayQuery)) {}

        if (rayQueryGetIntersectionTypeEXT(rayQuery, true) ==
            gl_RayQueryCommittedIntersectionNoneEXT) {
            // Miss - return background
            radiance += throughput * vec3(0.5, 0.7, 1.0);
            break;
        }

        // Hit - evaluate material, scatter ray
        HitInfo hit = getHitInfo(rayQuery);
        Material mat = materials[hit.materialId];

        radiance += throughput * mat.emission;
        throughput *= mat.albedo;

        ray = scatter(ray, hit, mat);
    }

    return radiance;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    Ray ray = generateCameraRay(pixel);
    vec3 color = tracePath(ray, 5);
    imageStore(outputImage, pixel, vec4(color, 1.0));
}
```

## Consequences

### Positive
- Leverages RDNA3 Ray Accelerators for maximum performance
- Clean incremental development path
- Qt handles platform differences (X11/Wayland)
- Educational value: learning both ray tracing AND modern GPU APIs
- 30+ fps achievable at 1080p with simple scenes

### Negative
- Vulkan boilerplate is substantial (~500-800 lines for setup)
- Must learn acceleration structure management
- Debugging ray tracing shaders is harder than CPU code
- Initial setup before seeing first image is significant

### Neutral
- C++ required (Qt + Vulkan SDK)
- RADV (Mesa) driver required (already present on system)
- glslc or glslangValidator for shader compilation

## Performance Expectations

At 1080p (1920x1080):

| Scene | Samples/pixel | Expected FPS |
|-------|---------------|--------------|
| 10 spheres, diffuse | 1 | 200+ |
| 10 spheres, diffuse | 16 | 60+ |
| Cornell box, diffuse+emissive | 1 | 150+ |
| Cornell box, diffuse+emissive | 16 | 30-60 |
| 1000 triangles w/ BVH | 1 | 100+ |
| 1000 triangles w/ BVH | 16 | 30+ |

## File Structure

```
raydemo/
├── CMakeLists.txt
├── src/
│   ├── main.cpp              # Qt app entry, QVulkanWindow setup
│   ├── renderer.h/cpp        # RayTracingRenderer (QVulkanWindowRenderer)
│   ├── vulkan_helpers.h/cpp  # Boilerplate abstractions
│   └── scene.h/cpp           # Scene description, sphere/material data
├── shaders/
│   ├── raytrace.comp         # Main ray tracing compute shader
│   └── common.glsl           # Shared math, random, material functions
├── docs/
│   └── adr/
│       └── ADR-001-vulkan-rt-architecture.md
└── README.md
```

## Alternatives Considered

### HIP/ROCm with custom BVH
- **Rejected**: Cannot access Ray Accelerators, 3-10x slower
- Would be educational for algorithm understanding but misses the hardware

### Full VK_KHR_ray_tracing_pipeline
- **Deferred to Phase 4**: More complex (shader binding tables, hit/miss shaders)
- ray_query gives hardware acceleration with simpler mental model
- Can migrate later if needed for specific features

### OpenGL compute shaders
- **Rejected**: Would require Vulkan-OpenGL interop for RT
- Adds complexity with no benefit
