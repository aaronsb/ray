# Architecture Decision Records

This directory contains Architecture Decision Records (ADRs) documenting significant technical decisions for the path tracer project.

## Index

| ADR | Title | Status | Summary |
|-----|-------|--------|---------|
| [ADR-001](ADR-001-vulkan-rt-architecture.md) | Vulkan RT Architecture | Accepted | Use Vulkan + ray_query + QVulkanWindow for hardware-accelerated path tracing |
| [ADR-002](ADR-002-path-tracer-optimizations.md) | Path Tracer Optimizations | Active | Discovery log tracking optimizations (NEE, MIS, adaptive sampling, etc.) |
| [ADR-003](ADR-003-modular-architecture.md) | Modular Architecture | Accepted | Refactor monolith into separate modules (geometry, materials, lights, scene, etc.) |
| [ADR-004](ADR-004-geometry-primitives.md) | Geometry Primitives | Proposed | Define primitive types: analytic (sphere, box, cylinder), voxel chunks, triangle meshes |
| [ADR-005](ADR-005-multi-target-build.md) | Multi-Target Build | Accepted | Static library + multiple demo executables sharing core renderer |
| [ADR-006](ADR-006-shape-representation.md) | Shape Representation | Proposed | Strategies for complex shapes: analytic, SDF, mesh, NURBS, subdivision |
| [ADR-008](ADR-008-parametric-primitives-library.md) | Parametric Primitives Library | Accepted | Bezier patch implementation (93fps teapot), library architecture for all primitives |

## Status Legend

- **Proposed** - Under discussion, not yet implemented
- **Accepted** - Approved, implementation planned or in progress
- **Active** - Living document, updated as work progresses
- **Implemented** - Decision executed in codebase
- **Deprecated** - No longer relevant
- **Superseded** - Replaced by another ADR
