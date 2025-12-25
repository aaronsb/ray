// ============================================================================
// csg/operations.glsl - CSG Boolean Operations on Intervals
// ============================================================================
//
// Combine ray-primitive intervals to implement CSG:
//   - Union:      A ∪ B (either solid)
//   - Intersect:  A ∩ B (both solids overlap)
//   - Subtract:   A - B (A with B carved out)
//
// Each operation returns an Interval representing where the ray
// passes through the resulting solid.
//
// ============================================================================

// Epsilon for interval overlap tolerance - needs to be large enough to catch
// grazing angle gaps where intervals should geometrically overlap but don't
const float INTERVAL_EPS = 0.01;

// --- Union (A ∪ B) ---
// Ray is inside result when inside A OR inside B
// For simple two-interval union, return the interval with earliest valid entry
Interval opUnion(Interval a, Interval b) {
    if (isEmpty(a)) return b;
    if (isEmpty(b)) return a;

    // Both non-empty: check for overlap or gap (with epsilon tolerance)
    // If they overlap or nearly touch, merge them
    if (a.tEnter <= b.tExit + INTERVAL_EPS && b.tEnter <= a.tExit + INTERVAL_EPS) {
        // Overlapping intervals - merge
        float tEnter = min(a.tEnter, b.tEnter);
        float tExit = max(a.tExit, b.tExit);
        vec3 nEnter = (a.tEnter < b.tEnter) ? a.nEnter : b.nEnter;
        vec3 nExit = (a.tExit > b.tExit) ? a.nExit : b.nExit;
        return Interval(tEnter, tExit, nEnter, nExit);
    }

    // Disjoint: return the one that starts first
    // (For multi-interval support, we'd need a list)
    return (a.tEnter < b.tEnter) ? a : b;
}

// --- Intersection (A ∩ B) ---
// Ray is inside result when inside A AND inside B
Interval opIntersect(Interval a, Interval b) {
    if (isEmpty(a) || isEmpty(b)) return EMPTY_INTERVAL;

    float tEnter = max(a.tEnter, b.tEnter);
    float tExit = min(a.tExit, b.tExit);

    if (tEnter > tExit) return EMPTY_INTERVAL;

    // Normal comes from whichever primitive we're entering/exiting
    vec3 nEnter = (a.tEnter > b.tEnter) ? a.nEnter : b.nEnter;
    vec3 nExit = (a.tExit < b.tExit) ? a.nExit : b.nExit;

    return Interval(tEnter, tExit, nEnter, nExit);
}

// --- Subtraction (A - B) ---
// Ray is inside result when inside A AND NOT inside B
// This can produce 0, 1, or 2 intervals. We return the first valid one.
Interval opSubtract(Interval a, Interval b) {
    if (isEmpty(a)) return EMPTY_INTERVAL;
    if (isEmpty(b)) return a;  // Nothing to subtract

    // Check for no overlap (B doesn't cut A) - with epsilon tolerance
    if (b.tExit < a.tEnter - INTERVAL_EPS || b.tEnter > a.tExit + INTERVAL_EPS) {
        return a;  // B is completely outside A
    }

    // B overlaps A - compute what remains
    // Case 1: B covers the entry of A (carves the front)
    if (b.tEnter <= a.tEnter && b.tExit < a.tExit) {
        // Result: [b.tExit, a.tExit]
        // Entry normal is the INVERTED exit normal of B (we're exiting B, entering result)
        return Interval(b.tExit, a.tExit, -b.nExit, a.nExit);
    }

    // Case 2: B covers the exit of A (carves the back)
    if (b.tEnter > a.tEnter && b.tExit >= a.tExit) {
        // Result: [a.tEnter, b.tEnter]
        // Exit normal is the INVERTED entry normal of B
        return Interval(a.tEnter, b.tEnter, a.nEnter, -b.nEnter);
    }

    // Case 3: B is completely inside A (splits A into two pieces)
    if (b.tEnter > a.tEnter && b.tExit < a.tExit) {
        // Two intervals: [a.tEnter, b.tEnter] and [b.tExit, a.tExit]
        // Return the first one (front piece)
        return Interval(a.tEnter, b.tEnter, a.nEnter, -b.nEnter);
    }

    // Case 4: B completely covers A
    if (b.tEnter <= a.tEnter && b.tExit >= a.tExit) {
        return EMPTY_INTERVAL;
    }

    // Fallback (shouldn't reach here)
    return EMPTY_INTERVAL;
}

// --- Multi-hit Subtraction ---
// For cases where B splits A, we need to return both pieces.
// This version outputs up to 2 intervals.
void opSubtract2(Interval a, Interval b, out Interval result1, out Interval result2) {
    result1 = EMPTY_INTERVAL;
    result2 = EMPTY_INTERVAL;

    if (isEmpty(a)) return;
    if (isEmpty(b)) {
        result1 = a;
        return;
    }

    // No overlap
    if (b.tExit < a.tEnter || b.tEnter > a.tExit) {
        result1 = a;
        return;
    }

    // B splits A into potentially two pieces
    // Front piece: [a.tEnter, b.tEnter]
    if (b.tEnter > a.tEnter) {
        result1 = Interval(a.tEnter, b.tEnter, a.nEnter, -b.nEnter);
    }

    // Back piece: [b.tExit, a.tExit]
    if (b.tExit < a.tExit) {
        Interval back = Interval(b.tExit, a.tExit, -b.nExit, a.nExit);
        if (isEmpty(result1)) {
            result1 = back;
        } else {
            result2 = back;
        }
    }
}

