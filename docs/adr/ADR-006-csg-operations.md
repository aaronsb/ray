# ADR-006: Constructive Solid Geometry (CSG) Operations

Status: Accepted
Date: 2025-12-21
Deciders: @aaron, @claude

## Context

We have implemented analytic primitives (sphere, box, cylinder, cone, torus) and want to combine them using boolean operations to create complex shapes. CSG enables:
- **Union**: Combine two shapes (A ∪ B)
- **Intersection**: Keep only overlapping regions (A ∩ B)
- **Difference**: Subtract one shape from another (A - B)

Examples: a sphere with a cylindrical hole, a rounded cube (box ∩ sphere), interlocking rings.

## Decision

Implement CSG using interval-based ray intersection.

### Core Concept: Intervals

Instead of finding just the closest hit, CSG requires knowing all segments where a ray is "inside" a primitive:

```
Ray: ----[====]-------[====]------>
          ^  ^         ^  ^
         t0 t1        t2 t3
```

Each `[t_enter, t_exit]` pair is an interval. Boolean operations combine intervals:

| Operation | Result |
|-----------|--------|
| A ∪ B (Union) | Merge overlapping intervals |
| A ∩ B (Intersection) | Keep only overlapping portions |
| A - B (Difference) | Remove B's intervals from A |

### Data Structures

```glsl
// Interval: ray segment inside a solid
struct Interval {
    float tMin;      // Entry point
    float tMax;      // Exit point
    vec3 normalMin;  // Normal at entry
    vec3 normalMax;  // Normal at exit
    uint materialId;
};

// CSG operations
const uint CSG_UNION = 0;
const uint CSG_INTERSECTION = 1;
const uint CSG_DIFFERENCE = 2;

// CSG node - references two children (primitives or other CSG nodes)
struct CSGNode {
    vec3 boundsMin;      // Precomputed AABB min (for early rejection)
    float _pad1;
    vec3 boundsMax;      // Precomputed AABB max
    float _pad2;
    uint operation;      // CSG_UNION, CSG_INTERSECTION, CSG_DIFFERENCE
    uint leftType;       // 0=sphere, 1=box, 2=cylinder, 3=cone, 4=torus, 5=csg
    uint leftIndex;      // Index into appropriate buffer
    uint rightType;
    uint rightIndex;
    uint materialId;     // Override material, or 0xFFFFFFFF to use child materials
    uint _pad3[2];
};
```

### Interval Functions for Primitives

Each primitive needs a function returning intervals, not just closest hit:

```glsl
// Returns number of intervals found (0, 1, or 2 for most primitives)
int sphereIntervals(Sphere s, Ray r, float tMin, float tMax, out Interval intervals[2]);
int boxIntervals(Box b, Ray r, float tMin, float tMax, out Interval intervals[2]);
int cylinderIntervals(Cylinder c, Ray r, float tMin, float tMax, out Interval intervals[2]);
// etc.
```

Most convex primitives produce at most 1 interval. Torus can produce 2 (ray enters, exits, re-enters, exits).

### Boolean Operations on Intervals

```glsl
// Combine interval lists according to operation
void combineIntervals(
    Interval a[], int countA,
    Interval b[], int countB,
    uint operation,
    out Interval result[], out int countResult
);
```

**Union**: Sort all endpoints, track depth, emit intervals where depth > 0
**Intersection**: Emit intervals where both A and B are active
**Difference**: Emit intervals where A is active but B is not (flip B's normals at boundaries)

### Bounding Box Computation

Each CSG node stores a precomputed AABB derived from its children:

```cpp
AABB computeCSGBounds(CSGOperation op, AABB left, AABB right) {
    switch (op) {
        case CSG_UNION:
            // Enclosing box of both children
            return AABB{min(left.min, right.min), max(left.max, right.max)};
        case CSG_INTERSECTION:
            // Intersection of bounds (tighter)
            return AABB{max(left.min, right.min), min(left.max, right.max)};
        case CSG_DIFFERENCE:
            // Subtracting can't grow beyond left child
            return left;
    }
}
```

Bounds are computed recursively when building the CSG tree, so nested CSG nodes automatically get correct bounds.

### CSG Tree Evaluation

```glsl
int evaluateCSG(CSGNode node, Ray r, float tMin, float tMax, out Interval result[4]) {
    // Early rejection: test bounding box first
    if (!hitAABB(node.boundsMin, node.boundsMax, r, tMin, tMax)) {
        return 0;  // No intervals
    }

    Interval leftIntervals[4], rightIntervals[4];
    int leftCount, rightCount;

    // Recursively get intervals from children
    leftCount = getIntervals(node.leftType, node.leftIndex, r, tMin, tMax, leftIntervals);
    rightCount = getIntervals(node.rightType, node.rightIndex, r, tMin, tMax, rightIntervals);

    // Combine according to operation
    return combineIntervals(leftIntervals, leftCount, rightIntervals, rightCount,
                           node.operation, result);
}
```

### Interval Caching

When evaluating CSG trees, the same primitive may appear multiple times. To avoid redundant computation, we cache intervals per-ray:

```glsl
// Fixed-size cache for interval results during CSG evaluation
const int INTERVAL_CACHE_SIZE = 8;

struct CacheEntry {
    uint type;           // Primitive type
    uint index;          // Index in buffer
    int count;           // Number of intervals
    Interval intervals[2];
};

// Thread-local cache (stored in registers/local memory)
CacheEntry intervalCache[INTERVAL_CACHE_SIZE];
int cacheUsed = 0;

// Look up cached intervals, or compute and cache
int getCachedIntervals(uint type, uint index, Ray r, float tMin, float tMax,
                       out Interval result[2]) {
    // Check cache first
    for (int i = 0; i < cacheUsed; i++) {
        if (intervalCache[i].type == type && intervalCache[i].index == index) {
            result = intervalCache[i].intervals;
            return intervalCache[i].count;
        }
    }

    // Cache miss: compute intervals
    int count = computeIntervals(type, index, r, tMin, tMax, result);

    // Store in cache if room
    if (cacheUsed < INTERVAL_CACHE_SIZE) {
        intervalCache[cacheUsed].type = type;
        intervalCache[cacheUsed].index = index;
        intervalCache[cacheUsed].count = count;
        intervalCache[cacheUsed].intervals = result;
        cacheUsed++;
    }

    return count;
}
```

This is especially valuable for:
- Primitives shared across multiple CSG operations
- Deep CSG trees where subtrees might overlap
- Difference operations like `A - B - C - D` where A is tested repeatedly

The cache is per-ray and lives in registers/local memory, so there's no global memory overhead.

### CPU Compilation of CSG Trees

**Design principle**: CPU RAM is abundant, GPU RAM is precious. Do heavy lifting on CPU.

Instead of sending tree structures to the GPU (requiring recursive traversal), we "compile" CSG trees to linear stack-based programs on the CPU:

```cpp
// CPU-side: Original CSG tree (can be arbitrarily complex)
struct CSGTree {
    CSGOperation op;
    std::variant<PrimitiveRef, std::unique_ptr<CSGTree>> left;
    std::variant<PrimitiveRef, std::unique_ptr<CSGTree>> right;
    // Rich metadata, debug info, names, etc. - CPU RAM is cheap
};

// CPU compiles to GPU-friendly linear program
enum class CSGOpCode : uint32_t {
    EVAL_PRIMITIVE,  // Push primitive's intervals onto stack
    COMBINE,         // Pop 2, combine with boolean op, push result
    END              // Evaluation complete, result on stack
};

struct CSGInstruction {
    CSGOpCode opcode;
    uint32_t arg1;   // Primitive type or CSG operation
    uint32_t arg2;   // Primitive index (for EVAL_PRIMITIVE)
    uint32_t _pad;
};

// Compiler output
struct CompiledCSG {
    AABB bounds;                         // Precomputed bounding box
    std::vector<CSGInstruction> program; // Linear instruction sequence
    uint32_t materialId;
};
```

**Compilation process** (on CPU):
1. Walk CSG tree in post-order
2. Emit `EVAL_PRIMITIVE` for leaves
3. Emit `COMBINE` after processing both children
4. Compute bounding boxes bottom-up
5. **Optimize**: detect shared primitives, reorder for cache locality

**GPU execution** (simple and fast):
```glsl
struct IntervalStack {
    Interval intervals[MAX_STACK][2];  // Each entry holds up to 2 intervals
    int counts[MAX_STACK];
    int top;
};

int evaluateCSGProgram(CSGInstruction program[], int programLen,
                       Ray r, float tMin, float tMax, out Interval result[2]) {
    IntervalStack stack;
    stack.top = 0;

    for (int pc = 0; pc < programLen; pc++) {
        CSGInstruction inst = program[pc];

        if (inst.opcode == EVAL_PRIMITIVE) {
            // Compute intervals for primitive, push to stack
            Interval iv[2];
            int count = computeIntervals(inst.arg1, inst.arg2, r, tMin, tMax, iv);
            stack.intervals[stack.top] = iv;
            stack.counts[stack.top] = count;
            stack.top++;
        }
        else if (inst.opcode == COMBINE) {
            // Pop two, combine, push result
            stack.top--;
            Interval b[2] = stack.intervals[stack.top];
            int countB = stack.counts[stack.top];
            stack.top--;
            Interval a[2] = stack.intervals[stack.top];
            int countA = stack.counts[stack.top];

            Interval combined[2];
            int countCombined = combineIntervals(a, countA, b, countB, inst.arg1, combined);
            stack.intervals[stack.top] = combined;
            stack.counts[stack.top] = countCombined;
            stack.top++;
        }
        else { // END
            break;
        }
    }

    result = stack.intervals[0];
    return stack.counts[0];
}
```

**CPU-side optimizations** (free, runs once at load time):
- **Shared primitive detection**: Mark primitives used multiple times, GPU can cache
- **Subtree deduplication**: Identical subtrees compiled once, referenced by offset
- **Operation reordering**: Put cheaper tests first for early termination
- **Dead code elimination**: Remove CSG ops that can't affect result (e.g., union with empty)
- **Bounds tightening**: Intersect bounds at each step for tighter culling

**Memory comparison**:
| Approach | GPU Memory | GPU Complexity |
|----------|------------|----------------|
| Tree pointers | O(nodes × 32 bytes) | Recursive, divergent |
| Compiled program | O(nodes × 16 bytes) | Linear, predictable |

### Transforms and Instancing

Compiled CSG programs work in local space. To support translation, rotation, scaling, and instancing, we transform the ray rather than the geometry:

```cpp
// GPU-side: CSG instance (small - just references a program + transform)
struct CSGInstance {
    mat4 worldToLocal;     // Inverse transform: transform ray into local space
    vec3 boundsMin;        // World-space AABB (precomputed on CPU)
    float _pad1;
    vec3 boundsMax;
    float _pad2;
    uint programOffset;    // Offset into CSG instruction buffer
    uint programLength;
    uint materialOverride; // 0xFFFFFFFF = use original materials
    uint _pad3;
};
```

**Evaluation with transforms**:
```glsl
int evaluateCSGInstance(CSGInstance inst, Ray worldRay, float tMin, float tMax,
                        out HitRecord rec) {
    // Early rejection with world-space bounds
    if (!hitAABB(inst.boundsMin, inst.boundsMax, worldRay, tMin, tMax)) {
        return 0;
    }

    // Transform ray to local space
    Ray localRay;
    localRay.origin = (inst.worldToLocal * vec4(worldRay.origin, 1.0)).xyz;
    localRay.direction = (inst.worldToLocal * vec4(worldRay.direction, 0.0)).xyz;

    // Scale tMin/tMax if non-uniform scale (direction length changes)
    float dirScale = length(localRay.direction);
    localRay.direction /= dirScale;
    float localTMin = tMin * dirScale;
    float localTMax = tMax * dirScale;

    // Evaluate CSG program in local space
    Interval intervals[2];
    int count = evaluateCSGProgram(
        csgProgram[inst.programOffset], inst.programLength,
        localRay, localTMin, localTMax, intervals
    );

    if (count == 0) return 0;

    // Transform hit back to world space
    float localT = intervals[0].tMin;
    rec.t = localT / dirScale;  // Correct t for world ray
    rec.point = worldRay.origin + rec.t * worldRay.direction;

    // Transform normal (use transpose of inverse for correct normal transformation)
    mat3 normalMatrix = transpose(mat3(inst.worldToLocal));
    rec.normal = normalize(normalMatrix * intervals[0].normalMin);

    rec.materialId = (inst.materialOverride != 0xFFFFFFFF)
                   ? inst.materialOverride
                   : intervals[0].materialId;

    return 1;
}
```

**Benefits**:
- **One program, many instances**: 100 identical CSG objects = 1 program + 100 small instance records
- **Arbitrary transforms**: translate, rotate, scale (including non-uniform)
- **World-space culling**: Bounds test happens before expensive ray transform
- **CPU-computed bounds**: Transform local AABB to world space once at load time

**Memory example**:
```
Complex CSG object: 50 instructions × 16 bytes = 800 bytes (compiled once)
100 instances: 100 × 96 bytes = 9,600 bytes (transforms + refs)
Total: 10.4 KB

vs. 100 separate copies: 80 KB
```

**CPU-side instance creation**:
```cpp
class CSGProgram {
    std::vector<CSGInstruction> instructions;
    AABB localBounds;

public:
    CSGInstance createInstance(const Transform& transform) {
        CSGInstance inst;
        inst.worldToLocal = transform.inverse();
        inst.bounds = localBounds.transformed(transform);  // World-space AABB
        inst.programOffset = /* offset in GPU buffer */;
        inst.programLength = instructions.size();
        return inst;
    }
};
```

### Collision Detection

The interval-based CSG evaluation provides collision primitives essentially for free:

**Point containment** (is point inside CSG?):
```glsl
bool isInsideCSG(CSGInstance inst, vec3 worldPoint) {
    // Transform point to local space
    vec3 localPoint = (inst.worldToLocal * vec4(worldPoint, 1.0)).xyz;

    // Shoot ray in arbitrary direction
    Ray testRay = Ray(localPoint, vec3(1, 0, 0));
    Interval intervals[2];
    int count = evaluateCSGProgram(inst.program, testRay, -T_MAX, T_MAX, intervals);

    // Point is inside if t=0 falls within any interval
    for (int i = 0; i < count; i++) {
        if (intervals[i].tMin <= 0.0 && intervals[i].tMax >= 0.0) {
            return true;
        }
    }
    return false;
}
```

**Distance to surface**:
```glsl
float distanceToCSG(CSGInstance inst, vec3 point) {
    if (isInsideCSG(inst, point)) {
        // Inside: find penetration depth (negative distance)
        // Cast rays in multiple directions, find minimum exit distance
        float minDist = T_MAX;
        vec3 dirs[6] = {vec3(1,0,0), vec3(-1,0,0), vec3(0,1,0),
                        vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1)};
        for (int i = 0; i < 6; i++) {
            Ray r = Ray(point, dirs[i]);
            Interval intervals[2];
            int count = evaluateCSG(inst, r, 0, T_MAX, intervals);
            if (count > 0) {
                minDist = min(minDist, intervals[0].tMax);  // Exit distance
            }
        }
        return -minDist;  // Negative = inside
    } else {
        // Outside: find distance to entry
        // Cast ray toward CSG center (or use bounds center)
        vec3 toCenter = inst.boundsCenter - point;
        Ray r = Ray(point, normalize(toCenter));
        Interval intervals[2];
        int count = evaluateCSG(inst, r, 0, T_MAX, intervals);
        if (count > 0) {
            return intervals[0].tMin;
        }
        return T_MAX;
    }
}
```

**Sphere vs CSG collision**:
```glsl
bool sphereIntersectsCSG(CSGInstance inst, vec3 center, float radius) {
    // Quick AABB check first
    if (!sphereIntersectsAABB(center, radius, inst.boundsMin, inst.boundsMax)) {
        return false;
    }

    // Precise check: is surface within radius of center?
    float dist = distanceToCSG(inst, center);
    return dist < radius;  // Works for both inside (negative) and outside (positive)
}
```

**Contact point and normal** (for physics):
```glsl
struct Contact {
    vec3 point;
    vec3 normal;
    float penetration;
};

bool getCSGContact(CSGInstance inst, vec3 queryPoint, float radius, out Contact contact) {
    float dist = distanceToCSG(inst, queryPoint);
    if (dist >= radius) return false;  // No contact

    // Cast ray toward surface to find contact point and normal
    Ray r = Ray(queryPoint, /* direction toward nearest surface */);
    HitRecord rec;
    if (evaluateCSGInstance(inst, r, 0, T_MAX, rec)) {
        contact.point = rec.point;
        contact.normal = rec.normal;
        contact.penetration = radius - dist;
        return true;
    }
    return false;
}
```

**Collision bounds hierarchy**:
```cpp
// CPU-side: Build BVH over CSG instances for broad phase
class CSGCollisionWorld {
    std::vector<CSGInstance> instances;
    BVH<CSGInstance> bvh;  // Spatial index using instance AABBs

public:
    // Broad phase: query BVH with sphere/AABB
    // Narrow phase: precise CSG interval tests
    std::vector<Contact> queryCollisions(const Sphere& sphere) {
        auto candidates = bvh.query(sphere.bounds());
        std::vector<Contact> contacts;
        for (auto& inst : candidates) {
            Contact c;
            if (getCSGContact(inst, sphere.center, sphere.radius, c)) {
                contacts.push_back(c);
            }
        }
        return contacts;
    }
};
```

**Benefits over mesh collision**:
| Aspect | CSG Intervals | Triangle Mesh |
|--------|---------------|---------------|
| Accuracy | Mathematically exact | Approximation |
| Memory | Compact programs | Vertices + indices + BVH |
| Curved surfaces | Perfect | Tessellation artifacts |
| Penetration depth | Direct from intervals | Requires EPA/GJK |
| Scaling | Just transform matrix | Rebuild BVH |

The same compiled CSG program works for both rendering and physics - no separate collision mesh needed.

### Shadow Rays

For shadow testing, we don't need full interval computation - just "is there any solid blocking the ray?" This allows early termination:

```glsl
bool csgBlocksShadowRay(CSGInstance inst, Ray r, float maxDist) {
    // Early bounds check
    if (!hitAABB(inst.boundsMin, inst.boundsMax, r, 0, maxDist)) {
        return false;
    }

    // Transform ray to local space
    Ray localRay = transformRay(r, inst.worldToLocal);

    // Evaluate with early-exit flag
    // Returns true as soon as any solid interval intersects [0, maxDist]
    return evaluateCSGShadow(inst.program, localRay, 0, maxDist);
}

// Optimized shadow evaluation - can skip normal computation, early exit
bool evaluateCSGShadow(CSGInstruction program[], Ray r, float tMin, float tMax) {
    // Same stack-based evaluation, but:
    // - Don't compute normals (not needed for shadows)
    // - Return true immediately if result has any interval in range
    // - Can short-circuit DIFFERENCE: if A doesn't hit, result is empty
}
```

### Interval Limits and Edge Cases

**Maximum intervals**: Fixed at 4 per primitive/CSG result.
- Sphere, Box, Cylinder, Cone: max 1 interval (convex)
- Torus: max 2 intervals (ray can enter/exit twice)
- CSG union: at most sum of children's intervals
- CSG intersection/difference: at most min of children's intervals

```glsl
const int MAX_INTERVALS = 4;

// If boolean operation would produce more intervals than MAX_INTERVALS,
// merge adjacent intervals (loses some precision but maintains correctness)
void clampIntervals(inout Interval intervals[], inout int count) {
    while (count > MAX_INTERVALS) {
        // Find smallest gap between adjacent intervals
        int mergeIdx = findSmallestGap(intervals, count);
        // Merge intervals[mergeIdx] and intervals[mergeIdx+1]
        intervals[mergeIdx].tMax = intervals[mergeIdx + 1].tMax;
        intervals[mergeIdx].normalMax = intervals[mergeIdx + 1].normalMax;
        // Shift remaining intervals down
        for (int i = mergeIdx + 1; i < count - 1; i++) {
            intervals[i] = intervals[i + 1];
        }
        count--;
    }
}
```

**Edge cases**:
- **Tangent rays**: Intervals with tMin ≈ tMax are discarded (thickness < EPSILON)
- **Grazing hits**: Use robust intersection tests with small tolerance
- **Degenerate CSG**: Empty intersection → 0 intervals (valid result)
- **Infinite intervals**: For unbounded primitives (planes), clamp to scene bounds

### Scene Integration

Add compiled CSG programs as a buffer:
```cpp
layout(std430, set = 0, binding = 10) readonly buffer CSGBuffer {
    CSGNode csgNodes[];
};
```

In `hitScene()`, after checking all primitives, check CSG nodes and find closest valid interval entry.

### C++ API

```cpp
// Scene methods
uint32_t addCSG(CSGOperation op,
                GeometryType leftType, uint32_t leftIndex,
                GeometryType rightType, uint32_t rightIndex,
                uint32_t materialOverride = UINT32_MAX);

// Convenience builders
uint32_t addCSGUnion(GeometryType t1, uint32_t i1, GeometryType t2, uint32_t i2);
uint32_t addCSGIntersection(GeometryType t1, uint32_t i1, GeometryType t2, uint32_t i2);
uint32_t addCSGDifference(GeometryType t1, uint32_t i1, GeometryType t2, uint32_t i2);
```

## Implementation Plan

1. **Phase 1: Interval infrastructure**
   - Add Interval struct to shader
   - Create `*Intervals()` functions for each primitive
   - Test that intervals correctly bracket solid regions

2. **Phase 2: Boolean operations**
   - Implement `combineIntervals()` for union, intersection, difference
   - Test with simple cases (overlapping, non-overlapping, contained)

3. **Phase 3: CSG nodes**
   - Add CSGNode struct and buffer
   - Implement `evaluateCSG()` with recursive child evaluation
   - Integrate into `hitScene()`

4. **Phase 4: C++ API and testing**
   - Add Scene methods for CSG
   - Create test objects (sphere with hole, rounded cube, etc.)

## Consequences

### Positive
- Enables complex shapes from simple primitives
- No mesh generation required
- Exact mathematical surfaces
- Natural fit with existing analytic primitives

### Negative
- Performance cost: each CSG node requires evaluating both children fully
- Nested CSG trees can be expensive (evaluate entire subtree per ray)
- More complex shader code
- Limited interval count may miss some ray-torus-torus intersections

### Neutral
- May want to add bounding box acceleration for CSG nodes
- Could cache interval results for rays hitting same CSG multiple times
- Maximum tree depth should be limited (stack depth in shader)

## Alternatives Considered

### 1. Mesh-based CSG
Generate triangle mesh from CSG at load time.
- Pro: Standard rendering pipeline
- Con: Loses exact surfaces, requires mesh boolean library, static only

### 2. SDF-based CSG
Use signed distance functions with sphere tracing.
- Pro: Trivial boolean ops (min/max/subtraction of distances)
- Con: Requires sphere tracing, harder to get exact hit points, slower for deep scenes

### 3. Interval arithmetic only for shadows
Only use CSG intervals for shadow rays, use closest-hit approximation for primary rays.
- Pro: Simpler, faster
- Con: Incorrect rendering of CSG surfaces

## Open Questions

1. **Maximum interval count**: 4 should handle most cases, but torus-torus CSG could theoretically produce 8. Use fixed array or dynamic?

2. **CSG-in-CSG depth limit**: How deep should CSG trees be allowed? 4 levels? 8?

3. **Material handling**: Should CSG nodes override child materials, or preserve them based on which surface was hit?

4. ~~**Bounding volumes**: Should CSG nodes store precomputed bounding boxes for early rejection?~~ **Resolved**: Yes, bounds computed recursively from children based on operation type.

5. **NEE (Next Event Estimation)**: How should direct lighting work with CSG? Sample underlying primitives?
