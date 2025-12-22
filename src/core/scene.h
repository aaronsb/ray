#pragma once

#include <vector>
#include <cstdint>
#include <QString>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "types.h"
#include "camera.h"
#include "materials.h"
#include "geometry.h"
#include "lights.h"

// Push constants for per-frame data
struct alignas(16) PushConstants {
    uint32_t frameIndex;
    uint32_t sampleCount;
    uint32_t maxBounces;
    uint32_t sphereCount;
    uint32_t boxCount;
    uint32_t cylinderCount;
    uint32_t coneCount;
    uint32_t torusCount;
    uint32_t spotLightCount;
    uint32_t width;
    uint32_t height;
    uint32_t useNEE;
    uint32_t accumulate;  // 0 = no accumulation (moving), 1 = accumulate (stationary)
    float sunElevation;   // radians, 0 = horizon, PI/2 = zenith
    float sunAzimuth;     // radians, angle around Y axis
};

// Scene container with add/remove API
class Scene {
public:
    // Material management - returns material ID
    uint32_t addMaterial(const Material& mat) {
        uint32_t id = static_cast<uint32_t>(m_materials.size());
        m_materials.push_back(mat);
        return id;
    }

    uint32_t addMaterial(MaterialType type, Vec3 albedo, Vec3 emission = {0,0,0},
                         float param = 0, float param2 = 0) {
        return addMaterial(Material{albedo, emission, static_cast<uint32_t>(type), param, param2, 0});
    }

    // Geometry - add primitives
    void add(const Sphere& sphere) { m_spheres.push_back(sphere); }
    void add(const Box& box) { m_boxes.push_back(box); }
    void add(const Cylinder& cyl) { m_cylinders.push_back(cyl); }
    void add(const Cone& cone) { m_cones.push_back(cone); }
    void add(const Torus& torus) { m_tori.push_back(torus); }

    // Convenience: add sphere by parameters
    void addSphere(Vec3 center, float radius, uint32_t materialId) {
        m_spheres.push_back({center, radius, materialId, {}});
    }

    // Convenience: add box by parameters
    void addBox(Vec3 center, Vec3 halfExtents, uint32_t materialId) {
        m_boxes.push_back({center, halfExtents, materialId, {}});
    }

    // Convenience: add cylinder by parameters
    // axis should be normalized; caps=true by default
    void addCylinder(Vec3 base, Vec3 axis, float radius, float height, uint32_t materialId, bool caps = true) {
        m_cylinders.push_back({base, axis.normalized(), radius, height, materialId, caps ? 1u : 0u});
    }

    // Convenience: add cone by parameters
    // axis points from base to tip; cap=true by default
    void addCone(Vec3 base, Vec3 axis, float radius, float height, uint32_t materialId, bool cap = true) {
        m_cones.push_back({base, axis.normalized(), radius, height, materialId, cap ? 1u : 0u});
    }

    // Convenience: add torus by parameters
    // axis points through the hole (perpendicular to ring plane)
    void addTorus(Vec3 center, Vec3 axis, float majorRadius, float minorRadius, uint32_t materialId) {
        m_tori.push_back({center, axis.normalized(), majorRadius, minorRadius, materialId, 0});
    }

    // Lights
    void add(const SpotLight& light) { m_spotLights.push_back(light); }

    void addSpotLight(Vec3 position, Vec3 direction, Vec3 color,
                      float innerAngle, float outerAngle,
                      GoboType gobo = GoboType::None,
                      float goboScale = 1.0f, float goboRotation = 0.0f) {
        m_spotLights.push_back({
            position, direction.normalized(), color,
            innerAngle, outerAngle,
            static_cast<uint32_t>(gobo), goboScale, goboRotation, {}
        });
    }

    // Clear all scene data
    void clear() {
        m_spheres.clear();
        m_boxes.clear();
        m_cylinders.clear();
        m_cones.clear();
        m_tori.clear();
        m_spotLights.clear();
        m_materials.clear();
    }

    // Accessors for renderer
    const std::vector<Sphere>& spheres() const { return m_spheres; }
    const std::vector<Box>& boxes() const { return m_boxes; }
    const std::vector<Cylinder>& cylinders() const { return m_cylinders; }
    const std::vector<Cone>& cones() const { return m_cones; }
    const std::vector<Torus>& tori() const { return m_tori; }
    const std::vector<SpotLight>& spotLights() const { return m_spotLights; }
    const std::vector<Material>& materials() const { return m_materials; }

    // Mutable access for runtime modifications (e.g., gobo cycling)
    std::vector<SpotLight>& spotLightsMut() { return m_spotLights; }

    // Counts
    uint32_t sphereCount() const { return static_cast<uint32_t>(m_spheres.size()); }
    uint32_t boxCount() const { return static_cast<uint32_t>(m_boxes.size()); }
    uint32_t cylinderCount() const { return static_cast<uint32_t>(m_cylinders.size()); }
    uint32_t coneCount() const { return static_cast<uint32_t>(m_cones.size()); }
    uint32_t torusCount() const { return static_cast<uint32_t>(m_tori.size()); }
    uint32_t spotLightCount() const { return static_cast<uint32_t>(m_spotLights.size()); }
    uint32_t materialCount() const { return static_cast<uint32_t>(m_materials.size()); }

    // === SERIALIZATION ===

    bool save(const QString& path) const {
        QJsonObject root;
        root["version"] = 1;

        // Materials
        QJsonArray materialsArray;
        for (const auto& mat : m_materials) {
            QJsonObject m;
            m["type"] = static_cast<int>(mat.type);
            m["albedo"] = vec3ToJson(mat.albedo);
            m["emission"] = vec3ToJson(mat.emission);
            m["param"] = mat.param;
            m["param2"] = mat.param2;
            materialsArray.append(m);
        }
        root["materials"] = materialsArray;

        // Spheres
        QJsonArray spheresArray;
        for (const auto& s : m_spheres) {
            QJsonObject obj;
            obj["center"] = vec3ToJson(s.center);
            obj["radius"] = s.radius;
            obj["materialId"] = static_cast<int>(s.materialId);
            spheresArray.append(obj);
        }
        root["spheres"] = spheresArray;

        // Boxes
        QJsonArray boxesArray;
        for (const auto& b : m_boxes) {
            QJsonObject obj;
            obj["center"] = vec3ToJson(b.center);
            obj["halfExtents"] = vec3ToJson(b.halfExtents);
            obj["materialId"] = static_cast<int>(b.materialId);
            boxesArray.append(obj);
        }
        root["boxes"] = boxesArray;

        // Spotlights
        QJsonArray lightsArray;
        for (const auto& l : m_spotLights) {
            QJsonObject obj;
            obj["position"] = vec3ToJson(l.position);
            obj["direction"] = vec3ToJson(l.direction);
            obj["color"] = vec3ToJson(l.color);
            obj["innerAngle"] = l.innerAngle;
            obj["outerAngle"] = l.outerAngle;
            obj["goboType"] = static_cast<int>(l.goboType);
            obj["goboScale"] = l.goboScale;
            obj["goboRotation"] = l.goboRotation;
            lightsArray.append(obj);
        }
        root["spotLights"] = lightsArray;

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return true;
    }

    bool load(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            return false;
        }

        QJsonObject root = doc.object();

        // Clear existing data
        clear();

        // Materials
        for (const auto& val : root["materials"].toArray()) {
            QJsonObject m = val.toObject();
            Material mat;
            mat.type = static_cast<uint32_t>(m["type"].toInt());
            mat.albedo = jsonToVec3(m["albedo"].toArray());
            mat.emission = jsonToVec3(m["emission"].toArray());
            mat.param = static_cast<float>(m["param"].toDouble());
            mat.param2 = static_cast<float>(m["param2"].toDouble());
            mat._pad = 0;
            m_materials.push_back(mat);
        }

        // Spheres
        for (const auto& val : root["spheres"].toArray()) {
            QJsonObject s = val.toObject();
            Sphere sphere;
            sphere.center = jsonToVec3(s["center"].toArray());
            sphere.radius = static_cast<float>(s["radius"].toDouble());
            sphere.materialId = static_cast<uint32_t>(s["materialId"].toInt());
            sphere._pad[0] = sphere._pad[1] = 0;
            m_spheres.push_back(sphere);
        }

        // Boxes
        for (const auto& val : root["boxes"].toArray()) {
            QJsonObject b = val.toObject();
            Box box;
            box.center = jsonToVec3(b["center"].toArray());
            box.halfExtents = jsonToVec3(b["halfExtents"].toArray());
            box.materialId = static_cast<uint32_t>(b["materialId"].toInt());
            box._pad[0] = box._pad[1] = box._pad[2] = 0;
            m_boxes.push_back(box);
        }

        // Spotlights
        for (const auto& val : root["spotLights"].toArray()) {
            QJsonObject l = val.toObject();
            SpotLight light;
            light.position = jsonToVec3(l["position"].toArray());
            light.direction = jsonToVec3(l["direction"].toArray());
            light.color = jsonToVec3(l["color"].toArray());
            light.innerAngle = static_cast<float>(l["innerAngle"].toDouble());
            light.outerAngle = static_cast<float>(l["outerAngle"].toDouble());
            light.goboType = static_cast<uint32_t>(l["goboType"].toInt());
            light.goboScale = static_cast<float>(l["goboScale"].toDouble());
            light.goboRotation = static_cast<float>(l["goboRotation"].toDouble());
            light._pad[0] = light._pad[1] = light._pad[2] = 0;
            m_spotLights.push_back(light);
        }

        return true;
    }

private:
    // JSON helpers
    static QJsonArray vec3ToJson(const Vec3& v) {
        return QJsonArray{v.x, v.y, v.z};
    }

    static Vec3 jsonToVec3(const QJsonArray& arr) {
        return Vec3{
            static_cast<float>(arr[0].toDouble()),
            static_cast<float>(arr[1].toDouble()),
            static_cast<float>(arr[2].toDouble())
        };
    }

private:
    std::vector<Sphere> m_spheres;
    std::vector<Box> m_boxes;
    std::vector<Cylinder> m_cylinders;
    std::vector<Cone> m_cones;
    std::vector<Torus> m_tori;
    std::vector<SpotLight> m_spotLights;
    std::vector<Material> m_materials;
};
