#pragma once

// Triangle mesh primitive with vertex/index buffers
// Supports loading from OBJ files

#include "../types.h"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>

namespace parametric {

// Vertex with position, normal, and UV
struct Vertex {
    Vec3 position;
    Vec3 normal;
    float u, v;
};

// GPU vertex - 32 bytes, 16-byte aligned
struct alignas(16) GPUVertex {
    float px, py, pz;  // position
    float nx, ny, nz;  // normal
    float u, v;        // texture coords
};

// Triangle as 3 vertex indices
struct Triangle {
    uint32_t v0, v1, v2;
    uint32_t materialId;
};

// GPU triangle - 16 bytes
struct alignas(16) GPUTriangle {
    uint32_t v0, v1, v2;
    uint32_t materialId;
};

// Mesh instance for placing meshes in scene
struct alignas(16) MeshInstance {
    Vec3 position;
    float scale;
    Vec3 rotation;     // Euler angles (radians)
    uint32_t meshId;   // Which mesh definition
    uint32_t materialId;
    uint32_t triangleOffset;  // Offset into global triangle buffer
    uint32_t triangleCount;
    uint32_t vertexOffset;    // Offset into global vertex buffer
};

// Mesh definition (CPU side)
class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;

    // Compute bounds
    void computeBounds(Vec3& outMin, Vec3& outMax) const {
        if (vertices.empty()) {
            outMin = outMax = Vec3{0, 0, 0};
            return;
        }
        outMin = outMax = vertices[0].position;
        for (const auto& v : vertices) {
            outMin.x = std::min(outMin.x, v.position.x);
            outMin.y = std::min(outMin.y, v.position.y);
            outMin.z = std::min(outMin.z, v.position.z);
            outMax.x = std::max(outMax.x, v.position.x);
            outMax.y = std::max(outMax.y, v.position.y);
            outMax.z = std::max(outMax.z, v.position.z);
        }
    }

    // Compute smooth normals from face normals
    void computeNormals() {
        // Zero all normals
        for (auto& v : vertices) {
            v.normal = Vec3{0, 0, 0};
        }

        // Accumulate face normals to vertices
        for (const auto& tri : triangles) {
            const Vec3& p0 = vertices[tri.v0].position;
            const Vec3& p1 = vertices[tri.v1].position;
            const Vec3& p2 = vertices[tri.v2].position;

            Vec3 e1{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
            Vec3 e2{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
            Vec3 n = cross(e1, e2);

            vertices[tri.v0].normal.x += n.x;
            vertices[tri.v0].normal.y += n.y;
            vertices[tri.v0].normal.z += n.z;
            vertices[tri.v1].normal.x += n.x;
            vertices[tri.v1].normal.y += n.y;
            vertices[tri.v1].normal.z += n.z;
            vertices[tri.v2].normal.x += n.x;
            vertices[tri.v2].normal.y += n.y;
            vertices[tri.v2].normal.z += n.z;
        }

        // Normalize
        for (auto& v : vertices) {
            v.normal = v.normal.normalized();
        }
    }

    // Convert to GPU format
    std::vector<GPUVertex> toGPUVertices() const {
        std::vector<GPUVertex> gpu;
        gpu.reserve(vertices.size());
        for (const auto& v : vertices) {
            gpu.push_back({
                v.position.x, v.position.y, v.position.z,
                v.normal.x, v.normal.y, v.normal.z,
                v.u, v.v
            });
        }
        return gpu;
    }

    std::vector<GPUTriangle> toGPUTriangles() const {
        std::vector<GPUTriangle> gpu;
        gpu.reserve(triangles.size());
        for (const auto& t : triangles) {
            gpu.push_back({t.v0, t.v1, t.v2, t.materialId});
        }
        return gpu;
    }

    // Load from OBJ file (basic loader - positions and faces only)
    static Mesh loadOBJ(const std::string& path, uint32_t materialId = 0) {
        Mesh mesh;
        std::ifstream file(path);
        if (!file.is_open()) {
            return mesh;
        }

        std::vector<Vec3> positions;
        std::vector<Vec3> normals;
        std::vector<std::pair<float, float>> uvs;

        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string prefix;
            iss >> prefix;

            if (prefix == "v") {
                float x, y, z;
                iss >> x >> y >> z;
                positions.push_back({x, y, z});
            }
            else if (prefix == "vn") {
                float x, y, z;
                iss >> x >> y >> z;
                normals.push_back({x, y, z});
            }
            else if (prefix == "vt") {
                float u, v;
                iss >> u >> v;
                uvs.push_back({u, v});
            }
            else if (prefix == "f") {
                // Parse face - handles v, v/vt, v/vt/vn, v//vn formats
                std::vector<uint32_t> faceVerts;
                std::string vertStr;

                while (iss >> vertStr) {
                    uint32_t vi = 0, ti = 0, ni = 0;

                    // Parse vertex index
                    size_t slash1 = vertStr.find('/');
                    if (slash1 == std::string::npos) {
                        vi = std::stoi(vertStr);
                    } else {
                        vi = std::stoi(vertStr.substr(0, slash1));
                        size_t slash2 = vertStr.find('/', slash1 + 1);
                        if (slash2 != std::string::npos) {
                            if (slash2 > slash1 + 1) {
                                ti = std::stoi(vertStr.substr(slash1 + 1, slash2 - slash1 - 1));
                            }
                            if (slash2 + 1 < vertStr.size()) {
                                ni = std::stoi(vertStr.substr(slash2 + 1));
                            }
                        } else if (slash1 + 1 < vertStr.size()) {
                            ti = std::stoi(vertStr.substr(slash1 + 1));
                        }
                    }

                    // OBJ indices are 1-based
                    Vertex v;
                    v.position = positions[vi - 1];
                    v.normal = (ni > 0 && ni <= normals.size()) ? normals[ni - 1] : Vec3{0, 0, 0};
                    v.u = (ti > 0 && ti <= uvs.size()) ? uvs[ti - 1].first : 0;
                    v.v = (ti > 0 && ti <= uvs.size()) ? uvs[ti - 1].second : 0;

                    faceVerts.push_back(static_cast<uint32_t>(mesh.vertices.size()));
                    mesh.vertices.push_back(v);
                }

                // Triangulate face (fan triangulation)
                for (size_t i = 2; i < faceVerts.size(); i++) {
                    mesh.triangles.push_back({
                        faceVerts[0], faceVerts[i - 1], faceVerts[i], materialId
                    });
                }
            }
        }

        // Compute normals if not provided
        bool hasNormals = false;
        for (const auto& v : mesh.vertices) {
            if (v.normal.x != 0 || v.normal.y != 0 || v.normal.z != 0) {
                hasNormals = true;
                break;
            }
        }
        if (!hasNormals) {
            mesh.computeNormals();
        }

        return mesh;
    }

    // Create simple box mesh
    static Mesh createBox(float halfX, float halfY, float halfZ, uint32_t materialId = 0) {
        Mesh mesh;

        // 8 vertices
        Vec3 corners[8] = {
            {-halfX, -halfY, -halfZ}, {+halfX, -halfY, -halfZ},
            {+halfX, +halfY, -halfZ}, {-halfX, +halfY, -halfZ},
            {-halfX, -halfY, +halfZ}, {+halfX, -halfY, +halfZ},
            {+halfX, +halfY, +halfZ}, {-halfX, +halfY, +halfZ}
        };

        // 6 faces, 2 triangles each
        int faces[6][4] = {
            {0, 1, 2, 3}, // back
            {5, 4, 7, 6}, // front
            {4, 0, 3, 7}, // left
            {1, 5, 6, 2}, // right
            {3, 2, 6, 7}, // top
            {4, 5, 1, 0}  // bottom
        };
        Vec3 normals[6] = {
            {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}
        };

        for (int f = 0; f < 6; f++) {
            uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            for (int i = 0; i < 4; i++) {
                Vertex v;
                v.position = corners[faces[f][i]];
                v.normal = normals[f];
                v.u = (i == 1 || i == 2) ? 1.0f : 0.0f;
                v.v = (i == 2 || i == 3) ? 1.0f : 0.0f;
                mesh.vertices.push_back(v);
            }
            mesh.triangles.push_back({base, base + 1, base + 2, materialId});
            mesh.triangles.push_back({base, base + 2, base + 3, materialId});
        }

        return mesh;
    }
};

} // namespace parametric
