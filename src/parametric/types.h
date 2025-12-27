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

// Axis-Aligned Bounding Box
struct AABB {
    Vec3 min, max;

    AABB() : min(1e30f, 1e30f, 1e30f), max(-1e30f, -1e30f, -1e30f) {}
    AABB(Vec3 mn, Vec3 mx) : min(mn), max(mx) {}

    float diagonal() const { return (max - min).length(); }

    void expand(const Vec3& p) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }

    void expand(const AABB& other) {
        min.x = std::min(min.x, other.min.x);
        min.y = std::min(min.y, other.min.y);
        min.z = std::min(min.z, other.min.z);
        max.x = std::max(max.x, other.max.x);
        max.y = std::max(max.y, other.max.y);
        max.z = std::max(max.z, other.max.z);
    }

    Vec3 center() const {
        return Vec3((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f);
    }
};

} // namespace parametric
