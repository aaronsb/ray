# Scene Format Reference

Ray's Bouncy Castle uses S-expressions for scene definition. This format is clean, composable, and supports includes for modularity.

## Overview

Scene files use Lisp-style S-expressions:

```lisp
; Comments start with semicolon
(command arg1 arg2 (nested expression))
```

**Key features**:
- Declarative (order-independent for most declarations)
- Composable (CSG operations nest naturally)
- Modular (includes support)
- Human-readable and parsable

## Basic Syntax

### Comments

```lisp
; This is a comment
; Comments extend to end of line
```

### S-Expressions

```lisp
(keyword arg1 arg2 ...)           ; Simple list
(keyword (property value) ...)    ; Property list
```

Numbers can be integers or floats:
```lisp
1   1.0   -3.5   0.001   1e-3
```

Strings are unquoted symbols:
```lisp
red   my-material   teapot
```

## Top-Level Forms

### Material Definition

Define a named material:

```lisp
(material <name> <properties>...)
```

**Properties**:
- `(type <material-type>)` - Material type (required)
- `(rgb <r> <g> <b>)` or `(albedo <r> <g> <b>)` - Color (0-1 range)
- `(roughness <value>)` - Surface roughness (0=mirror, 1=rough)
- `(metallic <value>)` - Metalness (0=dielectric, 1=metal)
- `(ior <value>)` - Index of refraction (glass: 1.5, diamond: 2.4)
- `(emissive <value>)` - Emission multiplier

**Material Types**:
- `diffuse` - Lambertian diffuse (matte)
- `metal` - Conductor with roughness
- `glass` - Dielectric with IOR
- `emissive` - Light-emitting surface

**Examples**:

```lisp
; Matte red
(material red (type diffuse) (rgb 0.9 0.2 0.2))

; Shiny gold
(material gold (type metal) (rgb 0.83 0.69 0.22) (roughness 0.02))

; Clear glass
(material glass (type glass) (rgb 0.95 0.95 0.95) (ior 1.5))

; White light
(material light (type emissive) (rgb 1.0 1.0 1.0) (emissive 10.0))
```

### Shape Definition

Define a geometric shape with a material:

```lisp
(shape <geometry> <material-name>)
```

The geometry can be:
- A primitive (sphere, box, cylinder, cone, torus)
- A CSG operation (union, subtract, intersect)

**Examples**:

```lisp
; Simple sphere
(shape (sphere (at 0 1 0) (r 1.0)) red)

; Box with CSG
(shape (subtract
         (box (at 0 1 0) (half 1 1 1))
         (sphere (at 0 1 0) (r 1.2)))
       glass)
```

### Include Files

Include another scene file:

```lisp
(include "<filename>")
```

Relative paths are resolved from the including file's directory.

**Example**:

```lisp
; Include teapot patch data
(include "teapot.scene")

; Now can instantiate teapot
(instance teapot (at 0 0 0) (scale 1) (rotate 0 0 0) red)
```

### Bezier Patch Groups

Define a named group of Bezier patches (Newell format):

```lisp
(newell-patch <name>
  (vertices
    (<x> <y> <z>)
    (<x> <y> <z>)
    ...)
  (patch <i0> <i1> ... <i15>)
  (patch <i0> <i1> ... <i15>)
  ...)
```

Each patch references 16 vertices (4x4 control point grid) by index.

**Example**:

```lisp
(newell-patch simple-patch
  (vertices
    (0 0 0) (1 0 0) (2 0 0) (3 0 0)
    (0 1 0) (1 1 1) (2 1 1) (3 1 0)
    (0 2 0) (1 2 1) (2 2 1) (3 2 0)
    (0 3 0) (1 3 0) (2 3 0) (3 3 0))
  (patch 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15))
```

For the Utah teapot, see `scenes/teapot.scene` (306 vertices, 32 patches).

### Bezier Instances

Instantiate a patch group with a transform:

```lisp
(instance <patch-group-name>
  (at <x> <y> <z>)
  (scale <s>)
  (rotate <rx> <ry> <rz>)
  <material-name>)
```

**Transform Properties**:
- `(at <x> <y> <z>)` - Position
- `(scale <s>)` - Uniform scale factor
- `(rotate <rx> <ry> <rz>)` - Rotation in degrees (X, Y, Z axes)

**Example**:

```lisp
; Two teapots with different materials
(instance teapot (at -3 0 0) (scale 0.5) (rotate 0 90 0) glass)
(instance teapot (at  3 0 0) (scale 0.5) (rotate 0 -90 0) gold)
```

## Primitives

### Sphere

```lisp
(sphere (at <x> <y> <z>) (r <radius>))
```

**Example**:
```lisp
(sphere (at 0 1 0) (r 1.5))
```

### Box

Axis-aligned box defined by half-extents:

```lisp
(box (at <x> <y> <z>) (half <hx> <hy> <hz>))
```

The box extends from `center - half` to `center + half`.

**Example**:
```lisp
; 2x2x2 box centered at origin
(box (at 0 1 0) (half 1 1 1))

; Thin slab
(box (at 0 0 0) (half 10 0.1 10))
```

### Cylinder

Vertical cylinder (Y-axis aligned):

```lisp
(cylinder (at <x> <y> <z>) (r <radius>) (h <height>))
```

The base is at `(x, y, z)` and extends `height` units upward.

**Example**:
```lisp
; Column 2 units tall
(cylinder (at 0 0 0) (r 0.5) (h 2.0))
```

### Cone

Vertical cone (Y-axis aligned):

```lisp
(cone (at <x> <y> <z>) (r <radius>) (h <height>))
```

The base is at `(x, y, z)` with radius `r`, tapering to a point at `y + h`.

**Example**:
```lisp
; Cone pointing upward
(cone (at 0 0 0) (r 1.0) (h 2.0))
```

### Torus

Torus (donut) aligned with Y-axis:

```lisp
(torus (at <x> <y> <z>) (major <R>) (minor <r>))
```

- `major`: Distance from center to tube center
- `minor`: Tube radius

**Example**:
```lisp
; Donut on ground
(torus (at 0 0.3 0) (major 1.0) (minor 0.3))
```

## CSG Operations

Constructive Solid Geometry combines primitives using boolean operations.

### Union

Combine shapes (additive):

```lisp
(union <shape1> <shape2> ...)
```

**Example**:
```lisp
; Snowman (three spheres)
(shape (union
         (union
           (sphere (at 0 0.6 0) (r 0.6))
           (sphere (at 0 1.5 0) (r 0.45)))
         (sphere (at 0 2.2 0) (r 0.3)))
       white)
```

### Subtract (Difference)

Subtract second shape from first:

```lisp
(subtract <base> <subtrahend>)
```

For multiple subtractions, nest operations:

```lisp
(subtract (subtract <base> <sub1>) <sub2>)
```

**Examples**:

```lisp
; Sphere with box-shaped hole
(shape (subtract
         (sphere (at 0 1 0) (r 1.2))
         (box (at 0 1 0) (half 0.5 1.5 0.5)))
       red)

; Sphere with three perpendicular holes
(shape (subtract
         (subtract
           (subtract
             (sphere (at 0 1 0) (r 1.2))
             (box (at 0 1 0) (half 1.5 0.4 0.4)))
           (box (at 0 1 0) (half 0.4 1.5 0.4)))
         (box (at 0 1 0) (half 0.4 0.4 1.5)))
       gold)
```

### Intersect

Keep only the overlap of shapes:

```lisp
(intersect <shape1> <shape2> ...)
```

**Examples**:

```lisp
; Rounded box (sphere ∩ box)
(shape (intersect
         (sphere (at 0 1 0) (r 1.3))
         (box (at 0 1 0) (half 0.9 0.9 0.9)))
       silver)

; Lens shape (two spheres intersecting)
(shape (intersect
         (sphere (at -0.5 1 0) (r 1.2))
         (sphere (at  0.5 1 0) (r 1.2)))
       glass)
```

### Complex CSG

Combine operations:

```lisp
; (sphere ∪ cylinder) - box
(shape (subtract
         (union
           (sphere (at 0 1 0) (r 1.0))
           (cylinder (at 0 0 0) (r 0.4) (h 2.0)))
         (box (at 0.5 1 0) (half 0.8 1.5 0.3)))
       blue)
```

## Complete Examples

### Minimal Scene

```lisp
; One red sphere
(material red (type diffuse) (rgb 0.9 0.2 0.2))
(shape (sphere (at 0 1 0) (r 1.0)) red)
```

### Material Showcase

```lisp
; Define materials
(material diffuse_white (type diffuse) (rgb 0.9 0.9 0.9))
(material shiny_gold (type metal) (rgb 0.83 0.69 0.22) (roughness 0.02))
(material brushed_steel (type metal) (rgb 0.78 0.78 0.78) (roughness 0.3))
(material clear_glass (type glass) (rgb 0.95 0.95 0.95) (ior 1.5))
(material blue_light (type emissive) (rgb 0.3 0.5 1.0) (emissive 5.0))

; Arrange primitives
(shape (sphere (at -4 1 0) (r 1.0)) diffuse_white)
(shape (sphere (at -2 1 0) (r 1.0)) shiny_gold)
(shape (sphere (at  0 1 0) (r 1.0)) brushed_steel)
(shape (sphere (at  2 1 0) (r 1.0)) clear_glass)
(shape (sphere (at  4 1 0) (r 1.0)) blue_light)
```

### CSG Gallery

```lisp
(material red (type metal) (rgb 0.9 0.2 0.2) (roughness 0.05))
(material green (type metal) (rgb 0.2 0.8 0.3) (roughness 0.05))
(material blue (type metal) (rgb 0.2 0.4 0.9) (roughness 0.05))

; Row 1: Plain primitives
(shape (sphere (at -6 1 0) (r 1.0)) red)
(shape (box (at -2 1 0) (half 0.8 0.8 0.8)) green)
(shape (cylinder (at 2 0 0) (r 0.7) (h 2.0)) blue)
(shape (cone (at 6 0 0) (r 1.0) (h 2.0)) red)

; Row 2: CSG subtract
(shape (subtract
         (sphere (at -6 1 3) (r 1.2))
         (box (at -6 1 3) (half 0.5 1.5 0.5)))
       red)

(shape (subtract
         (cylinder (at 2 0 3) (r 1.0) (h 2.0))
         (cylinder (at 2 0 3) (r 0.6) (h 2.5)))
       blue)

; Row 3: CSG intersect
(shape (intersect
         (sphere (at -6 1 6) (r 1.3))
         (box (at -6 1 6) (half 0.9 0.9 0.9)))
       green)

; Row 4: CSG union
(shape (union
         (sphere (at -6 1 9) (r 0.9))
         (box (at -6 0.5 9) (half 0.6 0.5 0.6)))
       red)
```

### Teapot Scene

```lisp
; Load teapot patch data
(include "teapot.scene")

; Define materials
(material silver (type metal) (rgb 0.95 0.95 0.97) (roughness 0.02))
(material glass (type glass) (rgb 0.95 0.95 0.95) (ior 1.5))
(material gold (type metal) (rgb 0.83 0.69 0.22) (roughness 0.02))

; Three teapots with different materials and orientations
(instance teapot (at -3 0 0) (scale 0.5) (rotate 0 90 0) silver)
(instance teapot (at  0 0 0) (scale 0.5) (rotate 0 0 0) glass)
(instance teapot (at  3 0 0) (scale 0.5) (rotate 0 -90 0) gold)
```

## Coordinate System

- **Y-up**: Y-axis points upward
- **Right-handed**: X-right, Y-up, Z-toward viewer
- **Camera default**: Looking down -Z axis from (0, 5, 10)

**Typical scene layout**:
```
Y
|
|__ X
/
Z (toward viewer)

Ground plane at Y=0
Objects typically centered around Y=1
```

## Tips and Best Practices

### Organization

Structure scene files for readability:

```lisp
; ============================================================================
; Materials
; ============================================================================
(material red ...)
(material blue ...)

; ============================================================================
; Primitives
; ============================================================================
(shape (sphere ...) red)
(shape (box ...) blue)

; ============================================================================
; CSG Operations
; ============================================================================
(shape (subtract ...) ...)
```

### Modularity

Use includes for reusable components:

```
scenes/
  demo.scene          ; Main scene
  materials.scene     ; Material library
  teapot.scene        ; Teapot patch data
  props.scene         ; Reusable props
```

```lisp
; demo.scene
(include "materials.scene")
(include "teapot.scene")
(include "props.scene")

(instance teapot (at 0 0 0) (scale 1) (rotate 0 0 0) gold)
```

### CSG Guidelines

1. **Keep trees balanced**: Nested operations perform better when balanced
   ```lisp
   ; Good: balanced tree
   (union (union A B) (union C D))

   ; Avoid: deep linear chain
   (union A (union B (union C D)))
   ```

2. **Use appropriate operations**:
   - `subtract` for holes, cutouts
   - `intersect` for rounded corners, lenses
   - `union` for combining separate parts

3. **Material inheritance**: Material is specified at the root `(shape ...)` level

### Performance

- **Bezier patches**: Larger scenes benefit from fewer, larger patch groups
- **CSG depth**: Limit nesting to 5-7 levels for best performance
- **Primitive count**: Scenes with 50+ CSG operations may see slowdown

## Validation

Use `scenecheck` to validate scene files:

```bash
./scenecheck scenes/demo.scene
```

Output shows:
- Parse errors (if any)
- Scene statistics (primitives, nodes, materials, patches)
- Warnings (missing materials, invalid references)

## Future Additions

Planned features for scene format:

- **Cameras**: `(camera (pos ...) (target ...) (fov ...))`
- **Lights**: Point, directional, area lights
- **OBJ meshes**: `(mesh "file.obj" material)`
- **Transformations**: Arbitrary rotation axes, non-uniform scale
- **Material libraries**: Import preset materials
- **Texture maps**: Image-based materials

## Related Documentation

- [Materials Guide](materials.md) - Material types and properties
- [Architecture Guide](architecture.md) - Scene loader implementation
- [ADR-007: SDF-based CSG](adr/ADR-007-sdf-based-csg.md) - CSG design decisions
