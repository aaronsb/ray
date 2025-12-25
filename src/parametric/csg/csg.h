#pragma once

// CSG (Constructive Solid Geometry) data structures for GPU upload
// Supports primitives (sphere, box, cylinder, cone, torus) and
// boolean operations (union, intersect, subtract)

#include "../types.h"
#include <vector>
#include <cstdint>

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
    const std::vector<CSGNode>& nodes() const { return m_nodes; }
    const std::vector<uint32_t>& roots() const { return m_roots; }

    uint32_t primitiveCount() const { return static_cast<uint32_t>(m_primitives.size()); }
    uint32_t nodeCount() const { return static_cast<uint32_t>(m_nodes.size()); }
    uint32_t rootCount() const { return static_cast<uint32_t>(m_roots.size()); }

    void clear();

private:
    std::vector<CSGPrimitive> m_primitives;
    std::vector<CSGNode> m_nodes;
    std::vector<uint32_t> m_roots;  // Indices of root nodes
};

// Inline implementations

inline uint32_t CSGScene::addSphere(float x, float y, float z, float radius) {
    uint32_t idx = primitiveCount();
    m_primitives.push_back({x, y, z, static_cast<uint32_t>(CSGPrimType::Sphere), radius, 0, 0, 0});
    return idx;
}

inline uint32_t CSGScene::addBox(float x, float y, float z, float halfX, float halfY, float halfZ) {
    uint32_t idx = primitiveCount();
    m_primitives.push_back({x, y, z, static_cast<uint32_t>(CSGPrimType::Box), halfX, halfY, halfZ, 0});
    return idx;
}

inline uint32_t CSGScene::addCylinder(float x, float y, float z, float radius, float height) {
    uint32_t idx = primitiveCount();
    m_primitives.push_back({x, y, z, static_cast<uint32_t>(CSGPrimType::Cylinder), radius, height, 0, 0});
    return idx;
}

inline uint32_t CSGScene::addCone(float x, float y, float z, float radius, float height) {
    uint32_t idx = primitiveCount();
    m_primitives.push_back({x, y, z, static_cast<uint32_t>(CSGPrimType::Cone), radius, height, 0, 0});
    return idx;
}

inline uint32_t CSGScene::addTorus(float x, float y, float z, float majorR, float minorR) {
    uint32_t idx = primitiveCount();
    m_primitives.push_back({x, y, z, static_cast<uint32_t>(CSGPrimType::Torus), majorR, minorR, 0, 0});
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
    m_nodes.clear();
    m_roots.clear();
}

} // namespace parametric
