#pragma once

#include <cmath>
#include <cstdint>

namespace ext {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }

    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
};

struct Rotator {
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

// FTransform is read as three raw fields at build-dependent offsets, never as a
// flat memcpy, because UE4 stores FVector as float[3] and UE5 LWC stores
// FVector3d as double[3].
struct Transform {
    Quat rotation;
    Vec3 translation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

inline Vec3 rotate_vector(const Quat& q, const Vec3& v) {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t{u.y * v.z - u.z * v.y,
                 u.z * v.x - u.x * v.z,
                 u.x * v.y - u.y * v.x};
    t = t * 2.0f;
    const Vec3 ut{u.y * t.z - u.z * t.y,
                  u.z * t.x - u.x * t.z,
                  u.x * t.y - u.y * t.x};
    return {v.x + q.w * t.x + ut.x,
            v.y + q.w * t.y + ut.y,
            v.z + q.w * t.z + ut.z};
}

inline Vec3 transform_position(const Transform& t, const Vec3& p) {
    const Vec3 scaled{p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z};
    return rotate_vector(t.rotation, scaled) + t.translation;
}

inline float normalize_angle(float degrees) {
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees < -180.0f) degrees += 360.0f;
    return degrees;
}

} // namespace ext
