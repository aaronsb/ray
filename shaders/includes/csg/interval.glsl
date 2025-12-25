// ============================================================================
// csg/interval.glsl - Interval Type for CSG Ray Tracing
// ============================================================================
//
// An interval represents where a ray passes through a solid:
//   [tEnter, tExit] with associated normals at each boundary
//
// Empty interval: tEnter > tExit
//
// ============================================================================

struct Interval {
    float tEnter;    // Ray enters solid
    float tExit;     // Ray exits solid
    vec3 nEnter;     // Normal at entry (pointing outward)
    vec3 nExit;      // Normal at exit (pointing outward)
};

// Empty interval constant
const Interval EMPTY_INTERVAL = Interval(1e30, -1e30, vec3(0), vec3(0));

// Check if interval is empty (no intersection)
bool isEmpty(Interval i) {
    return i.tEnter > i.tExit;
}

// Check if interval is valid within ray bounds
bool isValid(Interval i, float tMin, float tMax) {
    return i.tEnter <= i.tExit && i.tExit >= tMin && i.tEnter <= tMax;
}

// Get the first valid hit from an interval
bool firstHit(Interval i, float tMin, float tMax, out float t, out vec3 n) {
    if (isEmpty(i)) return false;

    // Check entry point
    if (i.tEnter >= tMin && i.tEnter <= tMax) {
        t = i.tEnter;
        n = i.nEnter;
        return true;
    }

    // Check exit point (ray started inside)
    if (i.tExit >= tMin && i.tExit <= tMax) {
        t = i.tExit;
        n = i.nExit;
        return true;
    }

    return false;
}
