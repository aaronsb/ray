#pragma once

// Scene loader: S-expression -> CSGScene + MaterialLibrary + Patches
// Uses the same public API you'd use programmatically

#include "sexp.h"
#include "../csg/csg.h"
#include "../materials/material.h"
#include "../bezier/patch_group.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <map>

namespace parametric {

// Instance of a patch group with transform
struct PatchInstance {
    std::string patchGroupName;
    float x = 0, y = 0, z = 0;
    float scale = 1.0f;
    float rotX = 0, rotY = 0, rotZ = 0;
    std::string materialName;
};

// Complete scene data
struct SceneData {
    CSGScene csg;
    MaterialLibrary materials;
    std::map<std::string, std::vector<Patch>> patchGroups;
    std::vector<PatchInstance> patchInstances;

    // Build instances for GPU upload
    std::vector<BezierInstance> buildInstances() const {
        std::vector<BezierInstance> result;
        for (const auto& inst : patchInstances) {
            BezierInstance bi;
            bi.posX = inst.x;
            bi.posY = inst.y;
            bi.posZ = inst.z;
            bi.scale = inst.scale;
            bi.rotX = inst.rotX;
            bi.rotY = inst.rotY;
            bi.rotZ = inst.rotZ;
            bi.materialId = materials.find(inst.materialName);
            result.push_back(bi);
        }
        return result;
    }

    // Build combined patch list from all groups (for now, single group support)
    std::vector<Patch> allPatches() const {
        std::vector<Patch> result;
        for (const auto& [name, patches] : patchGroups) {
            result.insert(result.end(), patches.begin(), patches.end());
        }
        return result;
    }
};

class SceneLoader {
public:
    // Load scene from file
    static bool loadFile(const std::string& path, SceneData& data) {
        std::ifstream file(path);
        if (!file) return false;

        std::stringstream buffer;
        buffer << file.rdbuf();

        // Get directory for relative includes
        std::string basePath = path;
        size_t lastSlash = basePath.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            basePath = basePath.substr(0, lastSlash + 1);
        } else {
            basePath = "";
        }

        return loadString(buffer.str(), data, basePath);
    }

    // Legacy API: load into separate CSGScene and MaterialLibrary
    static bool loadFile(const std::string& path, CSGScene& scene, MaterialLibrary& materials) {
        SceneData data;
        if (!loadFile(path, data)) return false;
        scene = std::move(data.csg);
        materials = std::move(data.materials);
        return true;
    }

    // Load scene from string
    static bool loadString(const std::string& source, SceneData& data, const std::string& basePath = "") {
        try {
            SceneLoader loader(data, basePath);
            auto expressions = parseSExp(source);
            for (const auto& expr : expressions) {
                loader.processTopLevel(expr);
            }
            return true;
        } catch (const std::exception& e) {
            fprintf(stderr, "Scene load error: %s\n", e.what());
            return false;
        }
    }

    // Legacy API
    static bool loadString(const std::string& source, CSGScene& scene, MaterialLibrary& materials) {
        SceneData data;
        if (!loadString(source, data, "")) return false;
        scene = std::move(data.csg);
        materials = std::move(data.materials);
        return true;
    }

private:
    SceneLoader(SceneData& data, const std::string& basePath)
        : data_(data), basePath_(basePath) {}

    void processTopLevel(const SExp& expr) {
        if (!expr.isList() || expr.size() == 0) return;

        const std::string& cmd = expr.head();

        if (cmd == "material") {
            processMaterial(expr);
        } else if (cmd == "shape") {
            processShape(expr);
        } else if (cmd == "include") {
            processInclude(expr);
        } else if (cmd == "newell-patch" || cmd == "patches") {
            processPatches(expr);
        } else if (cmd == "instance") {
            processInstance(expr);
        }
    }

    // (include "file.scene")
    void processInclude(const SExp& expr) {
        if (expr.size() < 2) {
            throw std::runtime_error("include requires a filename");
        }

        std::string filename = expr[1].asSymbol();
        std::string fullPath = basePath_ + filename;

        // Prevent infinite loops
        if (includedFiles_.count(fullPath)) {
            return; // Already included
        }
        includedFiles_.insert(fullPath);

        std::ifstream file(fullPath);
        if (!file) {
            throw std::runtime_error("Cannot open include file: " + fullPath);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        // Get directory for nested includes
        std::string nestedBase = fullPath;
        size_t lastSlash = nestedBase.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            nestedBase = nestedBase.substr(0, lastSlash + 1);
        } else {
            nestedBase = basePath_;
        }

        // Parse and process included file
        auto expressions = parseSExp(buffer.str());
        std::string savedBase = basePath_;
        basePath_ = nestedBase;
        for (const auto& e : expressions) {
            processTopLevel(e);
        }
        basePath_ = savedBase;
    }

    // (newell-patch name
    //   (vertices (x y z) (x y z) ...)
    //   (patch i0 i1 ... i15)
    //   ...)
    // Named after Martin Newell, creator of the Utah teapot.
    // Format: shared vertices + 16-index patches (4x4 bicubic Bezier)
    void processPatches(const SExp& expr) {
        if (expr.size() < 2) {
            throw std::runtime_error("patches requires a name");
        }

        std::string name = expr[1].asSymbol();
        std::vector<Vec3> vertices;
        std::vector<std::array<int, 16>> patchIndices;

        for (size_t i = 2; i < expr.size(); i++) {
            const auto& item = expr[i];
            if (!item.isList() || item.size() == 0) continue;

            const std::string& key = item.head();

            if (key == "vertices") {
                // (vertices (x y z) (x y z) ...)
                for (size_t j = 1; j < item.size(); j++) {
                    const auto& v = item[j];
                    if (v.isList() && v.size() >= 3) {
                        vertices.push_back(Vec3(
                            static_cast<float>(v[0].asNumber()),
                            static_cast<float>(v[1].asNumber()),
                            static_cast<float>(v[2].asNumber())
                        ));
                    }
                }
            } else if (key == "patch") {
                // (patch i0 i1 i2 ... i15)
                if (item.size() >= 17) {
                    std::array<int, 16> indices;
                    for (int j = 0; j < 16; j++) {
                        indices[j] = static_cast<int>(item[j + 1].asNumber());
                    }
                    patchIndices.push_back(indices);
                }
            }
        }

        // Build patches from vertices and indices
        std::vector<Patch> patches;
        for (const auto& indices : patchIndices) {
            Patch p;
            for (int j = 0; j < 16; j++) {
                if (indices[j] >= 0 && indices[j] < static_cast<int>(vertices.size())) {
                    p.cp[j] = vertices[indices[j]];
                }
            }
            patches.push_back(p);
        }

        data_.patchGroups[name] = std::move(patches);
    }

    // (instance name (at x y z) (scale s) (rotate rx ry rz) material)
    void processInstance(const SExp& expr) {
        if (expr.size() < 3) {
            throw std::runtime_error("instance requires name and material");
        }

        PatchInstance inst;
        inst.patchGroupName = expr[1].asSymbol();
        inst.materialName = expr[expr.size() - 1].asSymbol(); // Last element is material

        for (size_t i = 2; i < expr.size() - 1; i++) {
            const auto& prop = expr[i];
            if (!prop.isList() || prop.size() == 0) continue;

            const std::string& key = prop.head();

            if (key == "at" && prop.size() >= 4) {
                inst.x = static_cast<float>(prop[1].asNumber());
                inst.y = static_cast<float>(prop[2].asNumber());
                inst.z = static_cast<float>(prop[3].asNumber());
            } else if (key == "scale" && prop.size() >= 2) {
                inst.scale = static_cast<float>(prop[1].asNumber());
            } else if (key == "rotate" && prop.size() >= 4) {
                // Degrees to radians
                const float DEG2RAD = 3.14159265f / 180.0f;
                inst.rotX = static_cast<float>(prop[1].asNumber()) * DEG2RAD;
                inst.rotY = static_cast<float>(prop[2].asNumber()) * DEG2RAD;
                inst.rotZ = static_cast<float>(prop[3].asNumber()) * DEG2RAD;
            }
        }

        data_.patchInstances.push_back(inst);
    }

    // (material name (type ...) (albedo r g b) ...)
    void processMaterial(const SExp& expr) {
        if (expr.size() < 2) {
            throw std::runtime_error("material requires a name");
        }

        std::string name = expr[1].asSymbol();
        Material mat = {0.8f, 0.8f, 0.8f, 0, 0.5f, 0.0f, 1.5f, 0.0f}; // defaults

        for (size_t i = 2; i < expr.size(); i++) {
            const auto& prop = expr[i];
            if (!prop.isList() || prop.size() == 0) continue;

            const std::string& key = prop.head();

            if (key == "type") {
                std::string typeName = prop[1].asSymbol();
                if (typeName == "diffuse") mat.type = static_cast<uint32_t>(MaterialType::Diffuse);
                else if (typeName == "metal") mat.type = static_cast<uint32_t>(MaterialType::Metal);
                else if (typeName == "glass") mat.type = static_cast<uint32_t>(MaterialType::Glass);
                else if (typeName == "emissive") mat.type = static_cast<uint32_t>(MaterialType::Emissive);
            } else if (key == "albedo" || key == "rgb") {
                mat.r = static_cast<float>(prop[1].asNumber());
                mat.g = static_cast<float>(prop[2].asNumber());
                mat.b = static_cast<float>(prop[3].asNumber());
            } else if (key == "roughness") {
                mat.roughness = static_cast<float>(prop[1].asNumber());
            } else if (key == "metallic") {
                mat.metallic = static_cast<float>(prop[1].asNumber());
            } else if (key == "ior") {
                mat.ior = static_cast<float>(prop[1].asNumber());
            } else if (key == "emissive") {
                mat.emissive = static_cast<float>(prop[1].asNumber());
            }
        }

        data_.materials.add(name, mat);
    }

    // (shape geometry material-name)
    void processShape(const SExp& expr) {
        if (expr.size() < 3) {
            throw std::runtime_error("shape requires geometry and material");
        }

        const SExp& geom = expr[1];
        std::string matName = expr[2].asSymbol();
        uint32_t matId = data_.materials.find(matName);

        uint32_t nodeIdx = processGeometry(geom, matId);
        data_.csg.addRoot(nodeIdx);
    }

    // Process geometry expression, returns node index
    uint32_t processGeometry(const SExp& expr, uint32_t matId) {
        if (!expr.isList() || expr.size() == 0) {
            throw std::runtime_error("Invalid geometry expression");
        }

        const std::string& type = expr.head();

        // Primitives
        if (type == "sphere") {
            auto [x, y, z] = getCenter(expr);
            float r = getFloat(expr, "r", "radius");
            uint32_t prim = data_.csg.addSphere(x, y, z, r);
            return data_.csg.addPrimitiveNode(prim, matId);
        }
        if (type == "box") {
            auto [x, y, z] = getCenter(expr);
            auto [hx, hy, hz] = getVec3(expr, "half", "size");
            uint32_t prim = data_.csg.addBox(x, y, z, hx, hy, hz);
            return data_.csg.addPrimitiveNode(prim, matId);
        }
        if (type == "cylinder") {
            auto [x, y, z] = getCenter(expr);
            float r = getFloat(expr, "r", "radius");
            float h = getFloat(expr, "h", "height");
            uint32_t prim = data_.csg.addCylinder(x, y, z, r, h);
            return data_.csg.addPrimitiveNode(prim, matId);
        }
        if (type == "cone") {
            auto [x, y, z] = getCenter(expr);
            float r = getFloat(expr, "r", "radius");
            float h = getFloat(expr, "h", "height");
            uint32_t prim = data_.csg.addCone(x, y, z, r, h);
            return data_.csg.addPrimitiveNode(prim, matId);
        }
        if (type == "torus") {
            auto [x, y, z] = getCenter(expr);
            float major = getFloat(expr, "major", "R");
            float minor = getFloat(expr, "minor", "r");
            uint32_t prim = data_.csg.addTorus(x, y, z, major, minor);
            return data_.csg.addPrimitiveNode(prim, matId);
        }

        // CSG operations
        if (type == "union") {
            return processCSGOp(expr, matId, [this](uint32_t l, uint32_t r, uint32_t m) {
                return data_.csg.addUnion(l, r, m);
            });
        }
        if (type == "subtract" || type == "difference") {
            return processCSGOp(expr, matId, [this](uint32_t l, uint32_t r, uint32_t m) {
                return data_.csg.addSubtract(l, r, m);
            });
        }
        if (type == "intersect" || type == "intersection") {
            return processCSGOp(expr, matId, [this](uint32_t l, uint32_t r, uint32_t m) {
                return data_.csg.addIntersect(l, r, m);
            });
        }

        throw std::runtime_error("Unknown geometry type: " + type);
    }

    template<typename OpFunc>
    uint32_t processCSGOp(const SExp& expr, uint32_t matId, OpFunc opFunc) {
        if (expr.size() < 3) {
            throw std::runtime_error("CSG operation requires at least 2 children");
        }

        uint32_t result = processGeometry(expr[1], matId);
        for (size_t i = 2; i < expr.size(); i++) {
            uint32_t right = processGeometry(expr[i], matId);
            result = opFunc(result, right, matId);
        }
        return result;
    }

    std::tuple<float, float, float> getCenter(const SExp& expr) {
        for (size_t i = 1; i < expr.size(); i++) {
            if (expr[i].isList() && expr[i].size() >= 4) {
                const std::string& key = expr[i].head();
                if (key == "at" || key == "center") {
                    return {
                        static_cast<float>(expr[i][1].asNumber()),
                        static_cast<float>(expr[i][2].asNumber()),
                        static_cast<float>(expr[i][3].asNumber())
                    };
                }
            }
        }
        return {0, 0, 0};
    }

    float getFloat(const SExp& expr, const std::string& key, const std::string& altKey = "") {
        for (size_t i = 1; i < expr.size(); i++) {
            if (expr[i].isList() && expr[i].size() >= 2) {
                const std::string& k = expr[i].head();
                if (k == key || k == altKey) {
                    return static_cast<float>(expr[i][1].asNumber());
                }
            }
        }
        throw std::runtime_error("Missing required property: " + key);
    }

    std::tuple<float, float, float> getVec3(const SExp& expr, const std::string& key, const std::string& altKey = "") {
        for (size_t i = 1; i < expr.size(); i++) {
            if (expr[i].isList() && expr[i].size() >= 4) {
                const std::string& k = expr[i].head();
                if (k == key || k == altKey) {
                    return {
                        static_cast<float>(expr[i][1].asNumber()),
                        static_cast<float>(expr[i][2].asNumber()),
                        static_cast<float>(expr[i][3].asNumber())
                    };
                }
            }
        }
        throw std::runtime_error("Missing required property: " + key);
    }

    SceneData& data_;
    std::string basePath_;
    std::unordered_set<std::string> includedFiles_;
};

} // namespace parametric
