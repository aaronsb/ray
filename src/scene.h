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

    // Convenience: add sphere by parameters
    void addSphere(Vec3 center, float radius, uint32_t materialId) {
        m_spheres.push_back({center, radius, materialId, {}});
    }

    // Convenience: add box by parameters
    void addBox(Vec3 center, Vec3 halfExtents, uint32_t materialId) {
        m_boxes.push_back({center, halfExtents, materialId, {}});
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
        m_spotLights.clear();
        m_materials.clear();
    }

    // Accessors for renderer
    const std::vector<Sphere>& spheres() const { return m_spheres; }
    const std::vector<Box>& boxes() const { return m_boxes; }
    const std::vector<SpotLight>& spotLights() const { return m_spotLights; }
    const std::vector<Material>& materials() const { return m_materials; }

    // Mutable access for runtime modifications (e.g., gobo cycling)
    std::vector<SpotLight>& spotLightsMut() { return m_spotLights; }

    // Counts
    uint32_t sphereCount() const { return static_cast<uint32_t>(m_spheres.size()); }
    uint32_t boxCount() const { return static_cast<uint32_t>(m_boxes.size()); }
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
    std::vector<SpotLight> m_spotLights;
    std::vector<Material> m_materials;
};

// Default test scene factory
inline Scene createTestScene() {
    Scene scene;
    constexpr float deg2rad = 3.14159265f / 180.0f;

    // === MATERIALS ===
    // 0: White diffuse
    scene.addMaterial(MaterialType::Diffuse, {0.73f, 0.73f, 0.73f});
    // 1: Red diffuse
    scene.addMaterial(MaterialType::Diffuse, {0.65f, 0.05f, 0.05f});
    // 2: Green diffuse
    scene.addMaterial(MaterialType::Diffuse, {0.12f, 0.45f, 0.15f});
    // 3: Emissive (bright sun-like)
    scene.addMaterial(MaterialType::Emissive, {1, 1, 1}, {5000, 4700, 4000});
    // 4: Metal (slightly rough)
    scene.addMaterial(MaterialType::Metal, {0.8f, 0.8f, 0.9f}, {0,0,0}, 0.05f);
    // 5: Glass (clear dielectric)
    scene.addMaterial(MaterialType::Dielectric, {1, 1, 1}, {0,0,0}, 1.5f);
    // 6: White diffuse (reference)
    scene.addMaterial(MaterialType::Diffuse, {0.9f, 0.9f, 0.9f});
    // 7: Frosted glass
    scene.addMaterial(MaterialType::RoughDielectric, {1, 1, 1}, {0,0,0}, 1.5f, 0.15f);
    // 8: Perfect mirror
    scene.addMaterial(MaterialType::Metal, {0.95f, 0.95f, 0.95f}, {0,0,0}, 0.0f);
    // 9: Dichroic (cyan/green)
    scene.addMaterial(MaterialType::Dichroic, {0.9f, 0.9f, 0.95f}, {0,0,0}, 0.35f, 0.2f);
    // 10: Marble
    scene.addMaterial(MaterialType::Marble, {0.0f, 0.25f, 0.2f}, {0.95f, 0.98f, 0.95f}, 8.0f);
    // 11: Wood
    scene.addMaterial(MaterialType::Wood, {0.9f, 0.75f, 0.55f}, {0.5f, 0.3f, 0.15f}, 12.0f);
    // 12: Swirl
    scene.addMaterial(MaterialType::Swirl, {0.98f, 0.92f, 0.8f}, {0.7f, 0.3f, 0.1f}, 2.5f);
    // 13: Checker floor
    scene.addMaterial(MaterialType::Checker, {0.9f, 0.9f, 0.9f}, {0.2f, 0.2f, 0.2f}, 1.0f);
    // 14: Dichroic red/orange
    scene.addMaterial(MaterialType::Dichroic, {0.95f, 0.9f, 0.85f}, {0,0,0}, 0.85f, 0.15f);
    // 15: Dichroic violet/blue
    scene.addMaterial(MaterialType::Dichroic, {0.85f, 0.9f, 0.95f}, {0,0,0}, 0.1f, 0.1f);
    // 16: Dichroic broad (soap bubble)
    scene.addMaterial(MaterialType::Dichroic, {0.95f, 0.95f, 0.95f}, {0,0,0}, 0.5f, 0.6f);
    // 17-23: ROYGBIV dichroic spectrum
    scene.addMaterial(MaterialType::Dichroic, {0.95f, 0.95f, 0.95f}, {0,0,0}, 1.0f, 0.1f);    // Red
    scene.addMaterial(MaterialType::Dichroic, {0.95f, 0.95f, 0.95f}, {0,0,0}, 0.833f, 0.1f);  // Orange
    scene.addMaterial(MaterialType::Dichroic, {0.95f, 0.95f, 0.95f}, {0,0,0}, 0.667f, 0.1f);  // Yellow
    scene.addMaterial(MaterialType::Dichroic, {0.95f, 0.95f, 0.95f}, {0,0,0}, 0.5f, 0.1f);    // Green
    scene.addMaterial(MaterialType::Dichroic, {0.95f, 0.95f, 0.95f}, {0,0,0}, 0.333f, 0.1f);  // Blue
    scene.addMaterial(MaterialType::Dichroic, {0.95f, 0.95f, 0.95f}, {0,0,0}, 0.167f, 0.1f);  // Indigo
    scene.addMaterial(MaterialType::Dichroic, {0.95f, 0.95f, 0.95f}, {0,0,0}, 0.0f, 0.1f);    // Violet

    // === GEOMETRY ===
    // Ground plane (large sphere with checker)
    scene.addSphere({0, -142.0f, 0}, 142.0f, 13);

    // Front row
    scene.addSphere({-2.5f, 1.0f, 2}, 1.0f, 4);   // Metal
    scene.addSphere({0, 1.0f, 2}, 1.0f, 5);       // Glass
    scene.addSphere({2.5f, 1.0f, 2}, 1.0f, 6);    // White diffuse

    // Second row
    scene.addSphere({-2.5f, 1.0f, -1}, 1.0f, 7);  // Frosted glass
    scene.addSphere({0, 1.0f, -1}, 1.0f, 8);      // Mirror
    scene.addSphere({2.5f, 1.0f, -1}, 1.0f, 9);   // Dichroic

    // Third row - procedural
    scene.addSphere({-2.5f, 1.0f, -4}, 1.0f, 10); // Marble
    scene.addSphere({0, 1.0f, -4}, 1.0f, 11);     // Wood
    scene.addSphere({2.5f, 1.0f, -4}, 1.0f, 12);  // Swirl

    // Boxes
    scene.addBox({5.0f, 0.75f, 1.0f}, {0.75f, 0.75f, 0.75f}, 5);   // Glass cube
    scene.addBox({-5.0f, 0.6f, 0.0f}, {0.6f, 0.6f, 0.6f}, 8);      // Mirror cube
    scene.addBox({5.5f, 1.0f, -2.5f}, {0.5f, 1.0f, 0.5f}, 7);      // Frosted tall
    scene.addBox({-4.5f, 0.4f, 2.5f}, {0.4f, 0.4f, 0.4f}, 5);      // Small glass
    scene.addBox({-5.5f, 0.3f, -3.0f}, {0.8f, 0.3f, 0.8f}, 11);    // Wood pedestal
    scene.addBox({6.0f, 0.5f, -4.0f}, {0.5f, 0.5f, 1.0f}, 10);     // Marble block
    scene.addBox({-6.5f, 0.5f, 1.5f}, {0.5f, 0.5f, 0.5f}, 14);     // Red dichroic
    scene.addBox({6.5f, 0.5f, 1.5f}, {0.5f, 0.5f, 0.5f}, 15);      // Blue dichroic

    // Soap bubble sphere
    scene.addSphere({0.0f, 0.6f, 4.0f}, 0.6f, 16);

    // ROYGBIV rainbow
    const float roygbivRadius = 0.5f;
    const float roygbivSpacing = 1.2f;
    const float roygbivZ = 5.5f;
    const float roygbivStartX = -3.6f;
    for (int i = 0; i < 7; i++) {
        float x = roygbivStartX + i * roygbivSpacing;
        scene.addSphere({x, roygbivRadius, roygbivZ}, roygbivRadius, 17 + i);
    }

    // === SPOTLIGHTS ===
    scene.addSpotLight(
        {0.0f, 6.0f, 9.0f},
        {0.0f, -0.4f, -1.0f},
        {800.0f, 750.0f, 700.0f},
        20.0f * deg2rad, 35.0f * deg2rad,
        GoboType::Grid, 3.0f, 0.0f
    );

    scene.addSpotLight(
        {-8.0f, 6.0f, 4.0f},
        {0.6f, -0.6f, -0.3f},
        {100.0f, 200.0f, 800.0f},
        10.0f * deg2rad, 20.0f * deg2rad,
        GoboType::Stripes, 6.0f, 0.785f
    );

    scene.addSpotLight(
        {8.0f, 5.0f, -2.0f},
        {-0.5f, -0.7f, 0.2f},
        {600.0f, 200.0f, 50.0f},
        12.0f * deg2rad, 22.0f * deg2rad,
        GoboType::Circles, 3.0f, 0.0f
    );

    return scene;
}
