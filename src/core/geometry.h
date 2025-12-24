#pragma once

#include "types.h"

// Import all primitives from parametric library
#include "../parametric/sphere/sphere.h"
#include "../parametric/box/box.h"
#include "../parametric/cylinder/cylinder.h"
#include "../parametric/cone/cone.h"
#include "../parametric/torus/torus.h"
#include "../parametric/mesh/mesh.h"

// Re-export from parametric namespace
using parametric::Sphere;
using parametric::Box;
using parametric::Cylinder;
using parametric::Cone;
using parametric::Torus;
using parametric::Mesh;
using parametric::Vertex;
using parametric::Triangle;
using parametric::GPUVertex;
using parametric::GPUTriangle;
using parametric::MeshInstance;

// Geometry type enum for dispatch
enum class GeometryType : uint32_t {
    Sphere = 0,
    Box = 1,
    Cylinder = 2,
    Cone = 3,
    Torus = 4,
    Mesh = 5
};
