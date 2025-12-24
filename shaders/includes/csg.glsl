// ============================================================================
// csg.glsl - Interval-Based Constructive Solid Geometry
// ============================================================================
//
// BEST FOR:
//   - Hard-edged boolean operations on solids
//   - Sphere-minus-box, cylinder-intersect-cube, etc.
//   - Precise geometric cuts with sharp edges
//
// HOW IT WORKS:
//   1. Each primitive returns an Interval (entry/exit t values with normals)
//   2. CSG operations combine intervals mathematically
//   3. Result is exact - no iteration or convergence issues
//
// PRIMITIVES (csg/primitives/):
//   raySphere, rayBox, rayCylinder, rayCone, rayTorus,
//   rayPlane, rayDisc, rayTriangle, rayPrism
//
// OPERATIONS (csg/operations.glsl):
//   opUnion(a, b)      - A or B
//   opIntersect(a, b)  - A and B overlap
//   opSubtract(a, b)   - A minus B
//
// EXAMPLE:
//   #include "csg.glsl"
//
//   Interval sphereMinusBox(vec3 ro, vec3 rd) {
//       Interval sphere = raySphere(ro, rd, vec3(0), 1.0);
//       Interval box = rayBox(ro, rd, vec3(0), vec3(0.6));
//       return opSubtract(sphere, box);
//   }
//
//   // In main:
//   float t; vec3 n;
//   if (firstHit(sphereMinusBox(ro, rd), 0.001, 1000.0, t, n)) {
//       // Hit at t with normal n
//   }
//
// ============================================================================

#include "csg/interval.glsl"
#include "csg/primitives.glsl"
#include "csg/operations.glsl"
#include "csg/primitives/polygon.glsl"  // Requires operations.glsl
