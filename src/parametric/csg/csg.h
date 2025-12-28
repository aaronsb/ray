#pragma once

// CSG (Constructive Solid Geometry) data structures for GPU upload
// Supports primitives (sphere, box, cylinder, cone, torus) and
// boolean operations (union, intersect, subtract)

#include "../types.h"
#include <vector>
#include <cstdint>
#include <cmath>

namespace parametric {

// Primitive types (matches shader)
enum class CSGPrimType : uint32_t {
    Sphere = 0,
    Box = 1,
    Cylinder = 2,
    Cone = 3,
    Torus = 4,
};

// Node types (matches shader)
enum class CSGNodeType : uint32_t {
    Primitive = 0,   // Leaf node - references a primitive
    Union = 1,
    Intersect = 2,
    Subtract = 3,
};

// GPU CSG Primitive - 32 bytes
// Stores geometric parameters for a single primitive
struct alignas(16) CSGPrimitive {
    float x, y, z;          // center
    uint32_t type;          // CSGPrimType
    float param0;           // sphere: radius, box: halfX, cyl/cone: radius, torus: majorR
    float param1;           // box: halfY, cyl/cone: height, torus: minorR
    float param2;           // box: halfZ
    float _pad;
};

// GPU CSG Transform - 16 bytes
// Separate buffer for runtime manipulation (animation, interactivity)
struct alignas(16) CSGTransform {
    float rotX, rotY, rotZ;  // Euler angles (radians), applied in XYZ order
    float scale;             // Uniform scale (1.0 = no scale)
};

// GPU CSG Node - 16 bytes
// Tree node: either a primitive reference or a boolean operation
struct alignas(16) CSGNode {
    uint32_t type;          // CSGNodeType
    uint32_t left;          // For Primitive: index into primitives array
                            // For ops: left child node index
    uint32_t right;         // For ops: right child node index (unused for Primitive)
    uint32_t materialId;    // Material ID (used when this node is a root)
};

// CSG Scene builder
// Builds primitive and node arrays for GPU upload
class CSGScene {
public:
    // Add a primitive, returns its index
    uint32_t addSphere(float x, float y, float z, float radius);
    uint32_t addBox(float x, float y, float z, float halfX, float halfY, float halfZ);
    uint32_t addCylinder(float x, float y, float z, float radius, float height);
    uint32_t addCone(float x, float y, float z, float radius, float height);
    uint32_t addTorus(float x, float y, float z, float majorR, float minorR);

    // Add a node, returns its index
    // For primitive nodes, 'left' is the primitive index, 'right' is unused
    uint32_t addPrimitiveNode(uint32_t primIndex, uint32_t materialId);
    uint32_t addUnion(uint32_t leftNode, uint32_t rightNode, uint32_t materialId);
    uint32_t addIntersect(uint32_t leftNode, uint32_t rightNode, uint32_t materialId);
    uint32_t addSubtract(uint32_t leftNode, uint32_t rightNode, uint32_t materialId);

    // Mark a node as a root (visible shape to render)
    void addRoot(uint32_t nodeIndex);

    // Convenience: add primitive and immediately make it a root shape
    uint32_t addSphereShape(float x, float y, float z, float radius, uint32_t materialId);
    uint32_t addBoxShape(float x, float y, float z, float hx, float hy, float hz, uint32_t materialId);
    uint32_t addCylinderShape(float x, float y, float z, float radius, float height, uint32_t materialId);
    uint32_t addConeShape(float x, float y, float z, float radius, float height, uint32_t materialId);
    uint32_t addTorusShape(float x, float y, float z, float majorR, float minorR, uint32_t materialId);

    // Data accessors for GPU upload
    const std::vector<CSGPrimitive>& primitives() const { return m_primitives; }
    const std::vector<CSGTransform>& transforms() const { return m_transforms; }
    const std::vector<CSGNode>& nodes() const { return m_nodes; }
    const std::vector<uint32_t>& roots() const { return m_roots; }

    uint32_t primitiveCount() const { return static_cast<uint32_t>(m_primitives.size()); }
    uint32_t nodeCount() const { return static_cast<uint32_t>(m_nodes.size()); }
    uint32_t rootCount() const { return static_cast<uint32_t>(m_roots.size()); }

    // Transform manipulation (for animation/interactivity)
    void setTransform(uint32_t primIndex, float rotX, float rotY, float rotZ, float scale);
    CSGTransform& transform(uint32_t primIndex) { return m_transforms[primIndex]; }

    // Compute AABB for a primitive
    AABB computePrimitiveAABB(uint32_t primIndex) const;

    // Compute surface area for a primitive (for light sampling PDF)
    float computePrimitiveSurfaceArea(uint32_t primIndex) const;

    // Compute AABB for a CSG node (recursive through tree)
    AABB computeNodeAABB(uint32_t nodeIndex) const;

    // Compute AABBs for all roots
    std::vector<AABB> computeRootAABBs() const;

    void clear();

private:
    std::vector<CSGPrimitive> m_primitives;
    std::vector<CSGTransform> m_transforms;  // Parallel to m_primitives
    std::vector<CSGNode> m_nodes;
    std::vector<uint32_t> m_roots;  // Indices of root nodes
};

// Inline implementations

inline uint32_t CSGScene::addSphere(float x, float y, float z, float radius) {
    uint32_t idx = primitiveCount();
    m_primitives.push_back({x, y, z, static_cast<uint32_t>(CSGPrimType::Sphere), radius, 0, 0, 0});
    m_transforms.push_back({0, 0, 0, 1.0f});  // Identity transform
    return idx;
}

inline uint32_t CSGScene::addBox(float x, float y, float z, float halfX, float halfY, float halfZ) {
    uint32_t idx = primitiveCount();
    m_primitives.push_back({x, y, z, static_cast<uint32_t>(CSGPrimType::Box), halfX, halfY, halfZ, 0});
    m_transforms.push_back({0, 0, 0, 1.0f});  // Identity transform
    return idx;
}

inline uint32_t CSGScene::addCylinder(float x, float y, float z, float radius, float height) {
    uint32_t idx = primitiveCount();
    m_primitives.push_back({x, y, z, static_cast<uint32_t>(CSGPrimType::Cylinder), radius, height, 0, 0});
    m_transforms.push_back({0, 0, 0, 1.0f});  // Identity transform
    return idx;
}

inline uint32_t CSGScene::addCone(float x, float y, float z, float radius, float height) {
    uint32_t idx = primitiveCount();
    m_primitives.push_back({x, y, z, static_cast<uint32_t>(CSGPrimType::Cone), radius, height, 0, 0});
    m_transforms.push_back({0, 0, 0, 1.0f});  // Identity transform
    return idx;
}

inline uint32_t CSGScene::addTorus(float x, float y, float z, float majorR, float minorR) {
    uint32_t idx = primitiveCount();
    m_primitives.push_back({x, y, z, static_cast<uint32_t>(CSGPrimType::Torus), majorR, minorR, 0, 0});
    m_transforms.push_back({0, 0, 0, 1.0f});  // Identity transform
    return idx;
}

inline uint32_t CSGScene::addPrimitiveNode(uint32_t primIndex, uint32_t materialId) {
    uint32_t idx = nodeCount();
    m_nodes.push_back({static_cast<uint32_t>(CSGNodeType::Primitive), primIndex, 0, materialId});
    return idx;
}

inline uint32_t CSGScene::addUnion(uint32_t leftNode, uint32_t rightNode, uint32_t materialId) {
    uint32_t idx = nodeCount();
    m_nodes.push_back({static_cast<uint32_t>(CSGNodeType::Union), leftNode, rightNode, materialId});
    return idx;
}

inline uint32_t CSGScene::addIntersect(uint32_t leftNode, uint32_t rightNode, uint32_t materialId) {
    uint32_t idx = nodeCount();
    m_nodes.push_back({static_cast<uint32_t>(CSGNodeType::Intersect), leftNode, rightNode, materialId});
    return idx;
}

inline uint32_t CSGScene::addSubtract(uint32_t leftNode, uint32_t rightNode, uint32_t materialId) {
    uint32_t idx = nodeCount();
    m_nodes.push_back({static_cast<uint32_t>(CSGNodeType::Subtract), leftNode, rightNode, materialId});
    return idx;
}

inline void CSGScene::addRoot(uint32_t nodeIndex) {
    m_roots.push_back(nodeIndex);
}

inline uint32_t CSGScene::addSphereShape(float x, float y, float z, float radius, uint32_t materialId) {
    uint32_t prim = addSphere(x, y, z, radius);
    uint32_t node = addPrimitiveNode(prim, materialId);
    addRoot(node);
    return node;
}

inline uint32_t CSGScene::addBoxShape(float x, float y, float z, float hx, float hy, float hz, uint32_t materialId) {
    uint32_t prim = addBox(x, y, z, hx, hy, hz);
    uint32_t node = addPrimitiveNode(prim, materialId);
    addRoot(node);
    return node;
}

inline uint32_t CSGScene::addCylinderShape(float x, float y, float z, float radius, float height, uint32_t materialId) {
    uint32_t prim = addCylinder(x, y, z, radius, height);
    uint32_t node = addPrimitiveNode(prim, materialId);
    addRoot(node);
    return node;
}

inline uint32_t CSGScene::addConeShape(float x, float y, float z, float radius, float height, uint32_t materialId) {
    uint32_t prim = addCone(x, y, z, radius, height);
    uint32_t node = addPrimitiveNode(prim, materialId);
    addRoot(node);
    return node;
}

inline uint32_t CSGScene::addTorusShape(float x, float y, float z, float majorR, float minorR, uint32_t materialId) {
    uint32_t prim = addTorus(x, y, z, majorR, minorR);
    uint32_t node = addPrimitiveNode(prim, materialId);
    addRoot(node);
    return node;
}

inline void CSGScene::clear() {
    m_primitives.clear();
    m_transforms.clear();
    m_nodes.clear();
    m_roots.clear();
}

inline void CSGScene::setTransform(uint32_t primIndex, float rotX, float rotY, float rotZ, float scale) {
    m_transforms[primIndex] = {rotX, rotY, rotZ, scale};
}

inline AABB CSGScene::computePrimitiveAABB(uint32_t primIndex) const {
    const CSGPrimitive& p = m_primitives[primIndex];
    const CSGTransform& xform = m_transforms[primIndex];
    bool hasRotation = (xform.rotX != 0 || xform.rotY != 0 || xform.rotZ != 0);
    float s = xform.scale;

    switch (static_cast<CSGPrimType>(p.type)) {
        case CSGPrimType::Sphere: {
            float r = p.param0 * s;
            return AABB(Vec3(p.x - r, p.y - r, p.z - r),
                       Vec3(p.x + r, p.y + r, p.z + r));
        }
        case CSGPrimType::Box: {
            float hx = p.param0 * s, hy = p.param1 * s, hz = p.param2 * s;
            if (hasRotation) {
                // Conservative: use diagonal as spherical bound
                float diag = std::sqrt(hx*hx + hy*hy + hz*hz);
                return AABB(Vec3(p.x - diag, p.y - diag, p.z - diag),
                           Vec3(p.x + diag, p.y + diag, p.z + diag));
            }
            return AABB(Vec3(p.x - hx, p.y - hy, p.z - hz),
                       Vec3(p.x + hx, p.y + hy, p.z + hz));
        }
        case CSGPrimType::Cylinder: {
            float r = p.param0 * s, h = p.param1 * s;
            if (hasRotation) {
                float diag = std::sqrt(r*r + h*h);
                return AABB(Vec3(p.x - diag, p.y - diag, p.z - diag),
                           Vec3(p.x + diag, p.y + diag, p.z + diag));
            }
            return AABB(Vec3(p.x - r, p.y, p.z - r),
                       Vec3(p.x + r, p.y + h, p.z + r));
        }
        case CSGPrimType::Cone: {
            float r = p.param0 * s, h = p.param1 * s;
            if (hasRotation) {
                float diag = std::sqrt(r*r + h*h);
                return AABB(Vec3(p.x - diag, p.y - diag, p.z - diag),
                           Vec3(p.x + diag, p.y + diag, p.z + diag));
            }
            return AABB(Vec3(p.x - r, p.y, p.z - r),
                       Vec3(p.x + r, p.y + h, p.z + r));
        }
        case CSGPrimType::Torus: {
            float R = p.param0 * s, r = p.param1 * s;
            float extent = R + r;
            if (hasRotation) {
                // Torus rotated: worst case is extent in all directions
                return AABB(Vec3(p.x - extent, p.y - extent, p.z - extent),
                           Vec3(p.x + extent, p.y + extent, p.z + extent));
            }
            return AABB(Vec3(p.x - extent, p.y - r, p.z - extent),
                       Vec3(p.x + extent, p.y + r, p.z + extent));
        }
    }
    return AABB();
}

inline float CSGScene::computePrimitiveSurfaceArea(uint32_t primIndex) const {
    const CSGPrimitive& p = m_primitives[primIndex];
    constexpr float PI = 3.14159265358979323846f;

    switch (static_cast<CSGPrimType>(p.type)) {
        case CSGPrimType::Sphere: {
            float r = p.param0;
            return 4.0f * PI * r * r;
        }
        case CSGPrimType::Box: {
            float hx = p.param0, hy = p.param1, hz = p.param2;
            // Surface area = 2(wh + hd + dw) where w=2*hx, h=2*hy, d=2*hz
            return 8.0f * (hx*hy + hy*hz + hz*hx);
        }
        case CSGPrimType::Cylinder: {
            float r = p.param0, h = p.param1;
            // Two caps + lateral surface
            return 2.0f * PI * r * r + 2.0f * PI * r * h;
        }
        case CSGPrimType::Cone: {
            float r = p.param0, h = p.param1;
            // Base + lateral surface
            float slant = std::sqrt(r*r + h*h);
            return PI * r * r + PI * r * slant;
        }
        case CSGPrimType::Torus: {
            float R = p.param0, rr = p.param1;
            return 4.0f * PI * PI * R * rr;
        }
    }
    return 1.0f;  // Fallback
}

inline AABB CSGScene::computeNodeAABB(uint32_t nodeIndex) const {
    const CSGNode& node = m_nodes[nodeIndex];

    if (node.type == static_cast<uint32_t>(CSGNodeType::Primitive)) {
        return computePrimitiveAABB(node.left);
    }

    // For all CSG operations, conservatively use union of child AABBs
    AABB left = computeNodeAABB(node.left);
    AABB right = computeNodeAABB(node.right);
    left.expand(right);
    return left;
}

inline std::vector<AABB> CSGScene::computeRootAABBs() const {
    std::vector<AABB> result;
    result.reserve(m_roots.size());
    for (uint32_t rootIdx : m_roots) {
        result.push_back(computeNodeAABB(rootIdx));
    }
    return result;
}

} // namespace parametric
