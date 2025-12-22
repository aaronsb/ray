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
    Dichroic = 6,         // Thin-film interference
    Marble = 7,           // Turbulence-based veins
    Wood = 8,             // Concentric rings with grain
    Swirl = 9,            // FBM noise pattern
    Checker = 10          // Checkerboard pattern
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

// GPU box (axis-aligned) - 48 bytes, 16-byte aligned
struct alignas(16) Box {
    Vec3 center;           // 16 bytes (padded)
    Vec3 halfExtents;      // 16 bytes (padded) - size/2 in each dimension
    uint32_t materialId;
    float _pad[3];         // pad to 16 bytes
};

// Gobo pattern types for spotlights
enum class GoboType : uint32_t {
    None = 0,       // No pattern, solid light
    Stripes = 1,    // Vertical stripes
    Grid = 2,       // Grid/window pattern
    Circles = 3,    // Concentric circles
    Dots = 4,       // Dot array
    Star = 5,       // Star/burst pattern
    Off = 6         // Light completely off
};

// GPU spotlight - 80 bytes, 16-byte aligned
struct alignas(16) SpotLight {
    Vec3 position;         // 16 bytes
    Vec3 direction;        // 16 bytes (normalized)
    Vec3 color;            // 16 bytes (emission color/intensity)
    float innerAngle;      // radians - full intensity cone
    float outerAngle;      // radians - falloff to zero
    uint32_t goboType;     // GoboType enum
    float goboScale;       // pattern scale (larger = more repetitions)
    float goboRotation;    // pattern rotation in radians
    float _pad[3];         // pad to 16 bytes
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
    uint32_t boxCount;
    uint32_t spotLightCount;
    uint32_t width;
    uint32_t height;
    uint32_t useNEE;
    uint32_t accumulate;  // 0 = no accumulation (moving), 1 = accumulate (stationary)
    float sunElevation;   // radians, 0 = horizon, PI/2 = zenith
    float sunAzimuth;     // radians, angle around Y axis
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

// Scene data container
struct SceneData {
    std::vector<Sphere> spheres;
    std::vector<Box> boxes;
    std::vector<SpotLight> spotLights;
    std::vector<Material> materials;
};

// Default test scene: spheres and boxes
inline SceneData createTestScene() {
    SceneData scene;
    auto& materials = scene.materials;
    auto& spheres = scene.spheres;
    auto& boxes = scene.boxes;

    // Material 0: White diffuse (floor, ceiling, back wall)
    materials.push_back({{0.73f, 0.73f, 0.73f}, {0, 0, 0}, (uint32_t)MaterialType::Diffuse, 0, 0});

    // Material 1: Red diffuse (left wall)
    materials.push_back({{0.65f, 0.05f, 0.05f}, {0, 0, 0}, (uint32_t)MaterialType::Diffuse, 0, 0});

    // Material 2: Green diffuse (right wall)
    materials.push_back({{0.12f, 0.45f, 0.15f}, {0, 0, 0}, (uint32_t)MaterialType::Diffuse, 0, 0});

    // Material 3: Emissive (light) - sun is VERY bright (~100k lux vs ~10k sky)
    materials.push_back({{1, 1, 1}, {5000, 4700, 4000}, (uint32_t)MaterialType::Emissive, 0, 0});

    // Material 4: Metal sphere (slightly rough)
    materials.push_back({{0.8f, 0.8f, 0.9f}, {0, 0, 0}, (uint32_t)MaterialType::Metal, 0.05f, 0});

    // Material 5: Glass (clear dielectric)
    materials.push_back({{1, 1, 1}, {0, 0, 0}, (uint32_t)MaterialType::Dielectric, 1.5f, 0});

    // Material 6: White diffuse (for white balance reference)
    materials.push_back({{0.9f, 0.9f, 0.9f}, {0, 0, 0}, (uint32_t)MaterialType::Diffuse, 0, 0});

    // Material 7: Frosted glass (rough dielectric)
    materials.push_back({{1, 1, 1}, {0, 0, 0}, (uint32_t)MaterialType::RoughDielectric, 1.5f, 0.15f});

    // Material 8: Perfect mirror
    materials.push_back({{0.95f, 0.95f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Metal, 0.0f, 0});

    // Material 9: Thin-film interference (tunable spectrum)
    // param = peak wavelength position (0 = violet, 0.5 = green, 1 = red)
    // param2 = bandwidth (0 = sharp/saturated, 1 = broad/pastel)
    // This one peaks in cyan/green region
    materials.push_back({{0.9f, 0.9f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 0.35f, 0.2f});

    // Material 10: Green Marble (deep teal-green base, white veins + gold)
    // param = noise scale, albedo = base, emission = vein color
    materials.push_back({{0.0f, 0.25f, 0.2f}, {0.95f, 0.98f, 0.95f}, (uint32_t)MaterialType::Marble, 8.0f, 0});

    // Material 11: Wood / Baltic Birch (light wood with dark rings)
    // param = ring density, albedo = light wood, emission = dark grain
    materials.push_back({{0.9f, 0.75f, 0.55f}, {0.5f, 0.3f, 0.15f}, (uint32_t)MaterialType::Wood, 12.0f, 0});

    // Material 12: Swirl / Jupiter-like (cream and rust bands)
    // param = pattern scale
    materials.push_back({{0.98f, 0.92f, 0.8f}, {0.7f, 0.3f, 0.1f}, (uint32_t)MaterialType::Swirl, 2.5f, 0});

    // Material 13: Checker floor (white and dark gray)
    // param = checker scale, albedo = color1, emission = color2
    materials.push_back({{0.9f, 0.9f, 0.9f}, {0.2f, 0.2f, 0.2f}, (uint32_t)MaterialType::Checker, 1.0f, 0});

    // Material 14: Dichroic - red/orange peak (like a beetle shell)
    materials.push_back({{0.95f, 0.9f, 0.85f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 0.85f, 0.15f});

    // Material 15: Dichroic - violet/blue peak (like a morpho butterfly)
    materials.push_back({{0.85f, 0.9f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 0.1f, 0.1f});

    // Material 16: Dichroic - broad spectrum (soap bubble rainbow)
    materials.push_back({{0.95f, 0.95f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 0.5f, 0.6f});

    // Materials 17-23: ROYGBIV dichroic spectrum array
    // Sharp/saturated (param2 = 0.1) to show distinct colors
    materials.push_back({{0.95f, 0.95f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 1.0f, 0.1f});    // 17: Red
    materials.push_back({{0.95f, 0.95f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 0.833f, 0.1f}); // 18: Orange
    materials.push_back({{0.95f, 0.95f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 0.667f, 0.1f}); // 19: Yellow
    materials.push_back({{0.95f, 0.95f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 0.5f, 0.1f});   // 20: Green
    materials.push_back({{0.95f, 0.95f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 0.333f, 0.1f}); // 21: Blue
    materials.push_back({{0.95f, 0.95f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 0.167f, 0.1f}); // 22: Indigo
    materials.push_back({{0.95f, 0.95f, 0.95f}, {0, 0, 0}, (uint32_t)MaterialType::Dichroic, 0.0f, 0.1f});   // 23: Violet

    // Ground plane (checker pattern) - large sphere acts as floor
    // (Note: shader uses hitGroundPlane() instead, this is skipped)
    const float wallRadius = 142.0f;
    spheres.push_back({{0, -wallRadius, 0}, wallRadius, 13});

    // Sun is now in the sky shader, not a physical sphere
    // Emissive spheres in the scene will still emit light

    // === SPHERES ===
    // Front row
    // Metal sphere (polished)
    spheres.push_back({{-2.5f, 1.0f, 2}, 1.0f, 4});

    // Glass sphere (clear)
    spheres.push_back({{0, 1.0f, 2}, 1.0f, 5});

    // White diffuse sphere (white balance reference)
    spheres.push_back({{2.5f, 1.0f, 2}, 1.0f, 6});

    // Second row - special materials
    // Frosted glass
    spheres.push_back({{-2.5f, 1.0f, -1}, 1.0f, 7});

    // Perfect mirror
    spheres.push_back({{0, 1.0f, -1}, 1.0f, 8});

    // Dichroic / thin-film
    spheres.push_back({{2.5f, 1.0f, -1}, 1.0f, 9});

    // Third row - noise-based procedural materials
    // Marble
    spheres.push_back({{-2.5f, 1.0f, -4}, 1.0f, 10});

    // Wood
    spheres.push_back({{0, 1.0f, -4}, 1.0f, 11});

    // Swirl
    spheres.push_back({{2.5f, 1.0f, -4}, 1.0f, 12});

    // === BOXES ===
    // Glass cube (clear refractive) - to the right of spheres
    boxes.push_back({{5.0f, 0.75f, 1.0f}, {0.75f, 0.75f, 0.75f}, 5});

    // Mirror cube - to the left
    boxes.push_back({{-5.0f, 0.6f, 0.0f}, {0.6f, 0.6f, 0.6f}, 8});

    // Frosted glass tall box
    boxes.push_back({{5.5f, 1.0f, -2.5f}, {0.5f, 1.0f, 0.5f}, 7});

    // Small glass cube
    boxes.push_back({{-4.5f, 0.4f, 2.5f}, {0.4f, 0.4f, 0.4f}, 5});

    // Wood pedestal
    boxes.push_back({{-5.5f, 0.3f, -3.0f}, {0.8f, 0.3f, 0.8f}, 11});

    // Marble block
    boxes.push_back({{6.0f, 0.5f, -4.0f}, {0.5f, 0.5f, 1.0f}, 10});

    // Dichroic showcase - different spectrum peaks
    // Red/orange dichroic cube
    boxes.push_back({{-6.5f, 0.5f, 1.5f}, {0.5f, 0.5f, 0.5f}, 14});

    // Blue/violet dichroic cube
    boxes.push_back({{6.5f, 0.5f, 1.5f}, {0.5f, 0.5f, 0.5f}, 15});

    // Rainbow/soap bubble sphere (front center-ish)
    spheres.push_back({{0.0f, 0.6f, 4.0f}, 0.6f, 16});

    // === ROYGBIV Dichroic Spectrum Array ===
    // 7 spheres in a line, each tuned to a different spectral color
    const float roygbivRadius = 0.5f;
    const float roygbivSpacing = 1.2f;
    const float roygbivZ = 5.5f;  // Front of scene
    const float roygbivY = roygbivRadius;  // Sitting on ground
    const float roygbivStartX = -3.6f;  // Centered around x=0

    for (int i = 0; i < 7; i++) {
        float x = roygbivStartX + i * roygbivSpacing;
        spheres.push_back({{x, roygbivY, roygbivZ}, roygbivRadius, uint32_t(17 + i)});
    }

    // === SPOTLIGHTS ===
    auto& spotLights = scene.spotLights;
    constexpr float deg2rad = 3.14159265f / 180.0f;

    // Spotlight 1: White spot with grid gobo, aimed at ROYGBIV rainbow spheres
    // Rainbow is at z=5.5, y=0.5, x=-3.6 to +3.6
    spotLights.push_back({
        {0.0f, 6.0f, 9.0f},           // position (behind and above rainbow)
        Vec3{0.0f, -0.4f, -1.0f}.normalized(),  // angled down towards rainbow
        {800.0f, 750.0f, 700.0f},     // warm white, bright
        20.0f * deg2rad,              // inner angle - wide enough for all 7 spheres
        35.0f * deg2rad,              // outer angle
        (uint32_t)GoboType::Grid,     // gobo pattern
        3.0f,                         // gobo scale
        0.0f,                         // gobo rotation
        {}
    });

    // Spotlight 2: Blue spot with stripes, angled from the side
    spotLights.push_back({
        {-8.0f, 6.0f, 4.0f},          // position
        Vec3{0.6f, -0.6f, -0.3f}.normalized(),  // angled direction
        {100.0f, 200.0f, 800.0f},     // blue tint
        10.0f * deg2rad,              // tight inner
        20.0f * deg2rad,              // outer
        (uint32_t)GoboType::Stripes,  // stripes gobo
        6.0f,                         // scale
        0.785f,                       // 45 degree rotation
        {}
    });

    // Spotlight 3: Red/orange spot with circles, from the other side
    spotLights.push_back({
        {8.0f, 5.0f, -2.0f},          // position
        Vec3{-0.5f, -0.7f, 0.2f}.normalized(),
        {600.0f, 200.0f, 50.0f},      // orange/red
        12.0f * deg2rad,
        22.0f * deg2rad,
        (uint32_t)GoboType::Circles,
        3.0f,
        0.0f,
        {}
    });

    return scene;
}
