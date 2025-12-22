#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <cmath>

// GPU-aligned data structures (std140/std430 compatible)
// All structures are 16-byte aligned for optimal GPU access

struct alignas(16) Vec3 {
    float x, y, z;
    float _pad;

    Vec3() : x(0), y(0), z(0), _pad(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_), _pad(0) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float length() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3 normalized() const {
        float len = length();
        return len > 0 ? Vec3{x/len, y/len, z/len} : Vec3{};
    }
};

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Material types matching shader
enum class MaterialType : uint32_t {
    Diffuse = 0,
    Metal = 1,
    Dielectric = 2,
    Emissive = 3,
    RoughDielectric = 4,  // Frosted glass
    AnisotropicMetal = 5, // Brushed steel
    Dichroic = 6          // Color-shifting
};

// GPU material - 48 bytes, 16-byte aligned
struct alignas(16) Material {
    Vec3 albedo;           // 16 bytes (padded)
    Vec3 emission;         // 16 bytes (padded) - also used as secondary color for dichroic
    uint32_t type;         // MaterialType
    float param;           // roughness (metal) or IOR (dielectric/rough dielectric)
    float param2;          // roughness (rough dielectric), anisotropy (brushed metal)
    float _pad;            // pad to 16-byte boundary
};

// GPU sphere - 32 bytes, 16-byte aligned
struct alignas(16) Sphere {
    Vec3 center;           // 16 bytes (padded)
    float radius;
    uint32_t materialId;
    float _pad[2];         // pad to 16 bytes
};

// Camera uniform data - matches shader layout
struct alignas(16) CameraData {
    Vec3 origin;
    Vec3 lowerLeftCorner;
    Vec3 horizontal;
    Vec3 vertical;
    Vec3 u, v, w;          // camera basis vectors
    float lensRadius;
    float _pad[3];
};

// Push constants for per-frame data
struct alignas(16) PushConstants {
    uint32_t frameIndex;
    uint32_t sampleCount;
    uint32_t maxBounces;
    uint32_t sphereCount;
    uint32_t width;
    uint32_t height;
    uint32_t useNEE;
    uint32_t accumulate;  // 0 = no accumulation (moving), 1 = accumulate (stationary)
};

// Orbit camera controller
class OrbitCamera {
public:
    float distance = 10.0f;
    float azimuth = 0.0f;       // radians, around Y axis
    float elevation = 0.3f;     // radians, up/down
    Vec3 target{0, 0, 0};
    float fovY = 45.0f;         // degrees

    void rotate(float dAzimuth, float dElevation) {
        azimuth += dAzimuth;
        elevation += dElevation;

        // Clamp elevation to avoid gimbal lock
        constexpr float maxElev = 1.5f; // ~86 degrees
        if (elevation > maxElev) elevation = maxElev;
        if (elevation < -maxElev) elevation = -maxElev;
    }

    void zoom(float delta) {
        distance *= (1.0f - delta * 0.1f);
        if (distance < 0.5f) distance = 0.5f;
        if (distance > 100.0f) distance = 100.0f;
    }

    void pan(float dx, float dy) {
        // Pan in camera's local XY plane
        float cosElev = std::cos(elevation);
        Vec3 right{
            std::cos(azimuth),
            0,
            -std::sin(azimuth)
        };
        Vec3 up{
            -std::sin(elevation) * std::sin(azimuth),
            std::cos(elevation),
            -std::sin(elevation) * std::cos(azimuth)
        };

        float panSpeed = distance * 0.002f;
        target = target + right * (-dx * panSpeed) + up * (dy * panSpeed);
    }

    Vec3 getPosition() const {
        float cosElev = std::cos(elevation);
        return {
            target.x + distance * cosElev * std::sin(azimuth),
            target.y + distance * std::sin(elevation),
            target.z + distance * cosElev * std::cos(azimuth)
        };
    }

    CameraData getCameraData(float aspectRatio) const {
        CameraData cam{};

        Vec3 pos = getPosition();
        cam.origin = pos;

        // Camera basis
        Vec3 lookDir = (target - pos).normalized();
        Vec3 worldUp{0, 1, 0};
        cam.w = (pos - target).normalized(); // points away from target
        cam.u = cross(worldUp, cam.w).normalized();
        cam.v = cross(cam.w, cam.u);

        float theta = fovY * 3.14159265f / 180.0f;
        float h = std::tan(theta / 2.0f);
        float viewportHeight = 2.0f * h;
        float viewportWidth = aspectRatio * viewportHeight;

        cam.horizontal = cam.u * (viewportWidth * distance);
        cam.vertical = cam.v * (viewportHeight * distance);
        cam.lowerLeftCorner = cam.origin - cam.horizontal * 0.5f - cam.vertical * 0.5f - cam.w * distance;

        cam.lensRadius = 0.0f; // No DOF for now

        return cam;
    }
};

// Default test scene: Cornell box-ish with spheres
inline std::pair<std::vector<Sphere>, std::vector<Material>> createTestScene() {
    std::vector<Material> materials;
    std::vector<Sphere> spheres;

    // Material 0: White diffuse (floor, ceiling, back wall)
    materials.push_back({{0.73f, 0.73f, 0.73f}, {0, 0, 0}, (uint32_t)MaterialType::Diffuse, 0, 0});

    // Material 1: Red diffuse (left wall)
    materials.push_back({{0.65f, 0.05f, 0.05f}, {0, 0, 0}, (uint32_t)MaterialType::Diffuse, 0, 0});

    // Material 2: Green diffuse (right wall)
    materials.push_back({{0.12f, 0.45f, 0.15f}, {0, 0, 0}, (uint32_t)MaterialType::Diffuse, 0, 0});

    // Material 3: Emissive (light)
    materials.push_back({{1, 1, 1}, {15, 15, 15}, (uint32_t)MaterialType::Emissive, 0, 0});

    // Material 4: Metal sphere (slightly rough)
    materials.push_back({{0.8f, 0.8f, 0.9f}, {0, 0, 0}, (uint32_t)MaterialType::Metal, 0.05f, 0});

    // Material 5: Glass sphere (clear)
    materials.push_back({{1, 1, 1}, {0, 0, 0}, (uint32_t)MaterialType::Dielectric, 1.5f, 0});

    // Material 6: Blue diffuse
    materials.push_back({{0.2f, 0.3f, 0.8f}, {0, 0, 0}, (uint32_t)MaterialType::Diffuse, 0, 0});

    // Material 7: Frosted glass (rough dielectric)
    materials.push_back({{1, 1, 1}, {0, 0, 0}, (uint32_t)MaterialType::RoughDielectric, 1.5f, 0.15f});

    // Material 8: Brushed steel (anisotropic metal)
    materials.push_back({{0.7f, 0.7f, 0.75f}, {0, 0, 0}, (uint32_t)MaterialType::AnisotropicMetal, 0.3f, 0.1f});

    // Material 9: Dichroic (color-shifting: purple to gold)
    materials.push_back({{0.8f, 0.2f, 0.8f}, {1.0f, 0.8f, 0.2f}, (uint32_t)MaterialType::Dichroic, 3.0f, 0});

    // Expanded room (150 unit walls instead of 100)
    const float wallDist = 150.0f;
    const float wallRadius = 142.0f;

    // Ground plane
    spheres.push_back({{0, -wallRadius, 0}, wallRadius, 0});

    // Back wall
    spheres.push_back({{0, 0, -(wallDist + wallRadius - 8)}, wallRadius, 0});

    // Left wall (red)
    spheres.push_back({{-(wallDist - 8 + wallRadius), 0, 0}, wallRadius, 1});

    // Right wall (green)
    spheres.push_back({{(wallDist - 8 + wallRadius), 0, 0}, wallRadius, 2});

    // Ceiling
    spheres.push_back({{0, wallDist - 8 + wallRadius, 0}, wallRadius, 0});

    // Light (emissive sphere in ceiling)
    spheres.push_back({{0, 7.9f, 0}, 1.5f, 3});

    // Original spheres (front row)
    // Metal sphere (polished)
    spheres.push_back({{-2.5f, 1.0f, 2}, 1.0f, 4});

    // Glass sphere (clear)
    spheres.push_back({{0, 1.0f, 2}, 1.0f, 5});

    // Blue diffuse sphere
    spheres.push_back({{2.5f, 1.0f, 2}, 1.0f, 6});

    // New materials (back row)
    // Frosted glass
    spheres.push_back({{-2.5f, 1.0f, -1}, 1.0f, 7});

    // Brushed steel
    spheres.push_back({{0, 1.0f, -1}, 1.0f, 8});

    // Dichroic
    spheres.push_back({{2.5f, 1.0f, -1}, 1.0f, 9});

    return {spheres, materials};
}
