#pragma once
#include <cmath>

namespace fulmetry {

// Units: millimetres, seconds, kilograms, radians unless a name says otherwise.
struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    double length() const { return std::sqrt(dot(*this)); }
    bool operator==(const Vec3&) const = default;
};

struct Quat {
    double w = 1, x = 0, y = 0, z = 0;
    bool operator==(const Quat&) const = default;
};

struct Transform {
    Vec3 position;
    Quat rotation;
    bool operator==(const Transform&) const = default;
};

struct Aabb {
    Vec3 min, max;
    Vec3 center() const { return (min + max) * 0.5; }
    Vec3 size() const { return max - min; }
    bool operator==(const Aabb&) const = default;
};

struct CameraState {
    Vec3 eye{300, -300, 250};
    Vec3 target{0, 0, 0};
    double fovDegrees = 35;
    bool operator==(const CameraState&) const = default;
};

}  // namespace fulmetry
