// ============================================================================
// csg/primitives.glsl - Ray-Primitive Intervals for CSG
// ============================================================================
//
// Each function returns an Interval: the range of t values where
// the ray passes through the solid primitive.
//
// Primitives:
//   raySphere    - Sphere
//   rayBox       - Axis-aligned box
//   rayCylinder  - Capped cylinder (Y-axis)
//   rayCone      - Capped cone (Y-axis, apex at top)
//   rayTorus     - Torus (Y-axis)
//   rayPlane     - Infinite half-space
//   rayDisc      - Circular disc
//   rayTriangle  - Triangle (thin surface)
//   rayPrism     - Triangular prism
//
// ============================================================================

#include "primitives/sphere.glsl"
#include "primitives/box.glsl"
#include "primitives/cylinder.glsl"
#include "primitives/cone.glsl"
#include "primitives/torus.glsl"
#include "primitives/plane.glsl"
#include "primitives/disc.glsl"
// polygon.glsl requires operations.glsl, include separately after operations
