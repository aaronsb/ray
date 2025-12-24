// ============================================================================
// sdf.glsl - Signed Distance Functions for CSG Ray Tracing
// ============================================================================
//
// Convenience header that includes all SDF components.
// Or include individual files for finer control.
//
// FILES:
//   sdf/primitives.glsl  - SDF shapes (sphere, box, cylinder, etc.)
//   sdf/csg.glsl         - Boolean operations (union, intersect, subtract)
//   sdf/transforms.glsl  - Transform operations (translate, rotate)
//   sdf/intersect.glsl   - Ray-SDF intersection (Newton + sphere trace)
//
// QUICK START:
//   #include "sdf.glsl"
//
//   float myScene(vec3 p) {
//       float sphere = sdSphere(opTranslate(p, vec3(0,1,0)), 1.0);
//       float box = sdBox(p, vec3(1.5, 0.5, 1.5));
//       return opSubtract(box, sphere);  // Box with sphere cut out
//   }
//
//   DEFINE_HIT_SDF(myScene)
//
//   // In main():
//   float t; vec3 n;
//   if (hitSDF_myScene(rayOrigin, rayDir, 0.001, 1000.0, t, n)) { ... }
//
// ============================================================================

#include "sdf/primitives.glsl"
#include "sdf/csg.glsl"
#include "sdf/transforms.glsl"
#include "sdf/intersect.glsl"
