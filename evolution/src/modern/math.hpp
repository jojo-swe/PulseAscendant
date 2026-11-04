#pragma once

// Small float vector types for the 2026 side's scenes, and helpers that build and apply the row-major 3x4
// transforms (rt::Mat34, the layout of VkTransformMatrixKHR) the acceleration structures take.

#include "rt/geometry.hpp"

#include <cmath>

namespace evo::modern {

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
constexpr Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
constexpr Vec3 operator-(Vec3 a) {
    return {-a.x, -a.y, -a.z};
}
constexpr Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
constexpr Vec3 operator*(float s, Vec3 a) {
    return a * s;
}
constexpr Vec3 operator/(Vec3 a, float s) {
    return {a.x / s, a.y / s, a.z / s};
}

constexpr float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(Vec3 a) {
    return std::sqrt(dot(a, a));
}

inline Vec3 normalize(Vec3 a) {
    return a / length(a);
}

constexpr rt::Mat34 identity() {
    return {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
}

constexpr rt::Mat34 translate(Vec3 t) {
    return {{{1, 0, 0, t.x}, {0, 1, 0, t.y}, {0, 0, 1, t.z}}};
}

constexpr rt::Mat34 scale(Vec3 s) {
    return {{{s.x, 0, 0, 0}, {0, s.y, 0, 0}, {0, 0, s.z, 0}}};
}

// Right-handed rotation by `radians` about `axis` (any length but zero; Rodrigues).
inline rt::Mat34 rotate(Vec3 axis, float radians) {
    const Vec3 a = normalize(axis);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float k = 1.f - c;
    return {{{c + a.x * a.x * k, a.x * a.y * k - a.z * s, a.x * a.z * k + a.y * s, 0},
             {a.y * a.x * k + a.z * s, c + a.y * a.y * k, a.y * a.z * k - a.x * s, 0},
             {a.z * a.x * k - a.y * s, a.z * a.y * k + a.x * s, c + a.z * a.z * k, 0}}};
}

// a after b: transformPoint(compose(a, b), p) == transformPoint(a, transformPoint(b, p)).
constexpr rt::Mat34 compose(const rt::Mat34& a, const rt::Mat34& b) {
    rt::Mat34 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            float v = j == 3 ? a.m[i][3] : 0.f;
            for (int k = 0; k < 3; ++k)
                v += a.m[i][k] * b.m[k][j];
            r.m[i][j] = v;
        }
    }
    return r;
}

constexpr Vec3 transformPoint(const rt::Mat34& m, Vec3 p) {
    return {m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3],
            m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3],
            m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3]};
}

// The unit normal of a transformed surface: the inverse transpose of the linear part applied to n,
// normalised.
inline Vec3 transformNormal(const rt::Mat34& m, Vec3 n) {
    const Vec3 r0{m.m[0][0], m.m[0][1], m.m[0][2]};
    const Vec3 r1{m.m[1][0], m.m[1][1], m.m[1][2]};
    const Vec3 r2{m.m[2][0], m.m[2][1], m.m[2][2]};
    // The inverse's columns are (r1 x r2, r2 x r0, r0 x r1) / det, so these are the inverse transpose's rows.
    const Vec3 c0 = cross(r1, r2);
    const Vec3 c1 = cross(r2, r0);
    const Vec3 c2 = cross(r0, r1);
    const Vec3 v{dot(c0, n), dot(c1, n), dot(c2, n)};
    const float det = dot(r0, c0);
    return normalize(det < 0.f ? -v : v);
}

} // namespace evo::modern
