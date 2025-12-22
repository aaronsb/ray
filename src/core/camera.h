#pragma once

#include "types.h"

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
