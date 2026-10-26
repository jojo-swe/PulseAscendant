#pragma once

#include <array>
#include <cmath>

namespace evo::retro {

// The small double-precision vector helpers the CPU scene code shares (scene, FLYBY, TUNNEL).
using Vec3 = std::array<double, 3>;

constexpr Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

constexpr double dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

inline Vec3 normalize(const Vec3& v) {
    const double length = std::sqrt(dot(v, v));
    return {v[0] / length, v[1] / length, v[2] / length};
}

} // namespace evo::retro
