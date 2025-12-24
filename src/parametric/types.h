#pragma once

// Parametric primitives - common types

#include <cstdint>
#include <cmath>

namespace parametric {

// GPU-aligned vector type (std140/std430 compatible)
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

} // namespace parametric
