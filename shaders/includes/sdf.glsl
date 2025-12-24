// ============================================================================
// sdf.glsl - Signed Distance Functions for Soft/Volumetric Rendering
// ============================================================================
//
// BEST FOR:
//   - Clouds, fog, atmospheric effects
//   - Soft organic shapes with smooth blends
//   - Procedural terrain, metaballs
//   - Any shape where slight imprecision is acceptable
//
// NOT RECOMMENDED FOR:
//   - Hard-edged CSG on solids (use csg/interval.glsl instead)
//   - Sharp boolean cuts (sphere-minus-box, etc.)
//   - Geometry requiring pixel-perfect edges
//
// WHY: SDF boolean operations (min/max) don't preserve true distances
// at boundaries, causing sphere tracing to struggle at sharp edges.
//
// FILES:
//   sdf/primitives.glsl  - SDF shapes (sphere, box, cylinder, etc.)
//   sdf/csg.glsl         - Soft boolean operations (smooth blends)
//   sdf/transforms.glsl  - Transform operations (translate, rotate)
//   sdf/intersect.glsl   - Ray-SDF intersection (sphere tracing)
//
// EXAMPLE (soft blended shapes):
//   #include "sdf.glsl"
//
//   float myScene(vec3 p) {
//       float a = sdSphere(opTranslate(p, vec3(-0.5, 0, 0)), 1.0);
//       float b = sdSphere(opTranslate(p, vec3(0.5, 0, 0)), 1.0);
//       return opSmoothUnion(a, b, 0.3);  // Smooth blend
//   }
//
//   DEFINE_HIT_SDF(myScene)
//
// ============================================================================

#include "sdf/primitives.glsl"
#include "sdf/csg.glsl"
#include "sdf/transforms.glsl"
#include "sdf/intersect.glsl"
