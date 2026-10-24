// Ray casting for the painter's-order tests: the ray of a retro camera (retro/scene.hpp) through a point of
// the 320x200 frame, and its distance to a world-space triangle (Moller-Trumbore, two-sided).

#pragma once

#include "retro/scene.hpp"
#include "retro/vec3.hpp"

#include <cmath>
#include <numbers>

namespace evo::test {

using retro::Vec3;

struct Ray {
    Vec3 origin;
    Vec3 direction; // unit length, so hit() returns distances
};

// The ray from the camera's eye through screen point (px, py) in pixels, with addTriangle's projection.
inline Ray rayThrough(const retro::Camera& camera, double px, double py) {
    const Vec3 eye = {camera.eye[0], camera.eye[1], camera.eye[2]};
    const Vec3 forward =
        retro::normalize(retro::sub({camera.target[0], camera.target[1], camera.target[2]}, eye));
    const Vec3 right = retro::normalize(retro::cross({camera.up[0], camera.up[1], camera.up[2]}, forward));
    const Vec3 up = retro::cross(forward, right);
    const double focalY =
        (retro::kScreenHeight / 2.0) / std::tan(camera.fovYDegrees * std::numbers::pi / 360.0);
    const double focalX = focalY * retro::kPixelAspect;
    const double vx = (px - retro::kScreenWidth / 2.0) / focalX;
    const double vy = (retro::kScreenHeight / 2.0 - py) / focalY;
    return {eye, retro::normalize({forward[0] + right[0] * vx + up[0] * vy,
                                   forward[1] + right[1] * vx + up[1] * vy,
                                   forward[2] + right[2] * vx + up[2] * vy})};
}

// The distance along the ray to the triangle (either side facing), or a negative number for a miss.
inline double hit(const Ray& ray, const double world[3][3]) {
    const Vec3 a = {world[0][0], world[0][1], world[0][2]};
    const Vec3 e1 = retro::sub({world[1][0], world[1][1], world[1][2]}, a);
    const Vec3 e2 = retro::sub({world[2][0], world[2][1], world[2][2]}, a);
    const Vec3 p = retro::cross(ray.direction, e2);
    const double det = retro::dot(e1, p);
    if (std::abs(det) < 1e-14)
        return -1;
    const Vec3 s = retro::sub(ray.origin, a);
    const double u = retro::dot(s, p) / det;
    if (u < 0 || u > 1)
        return -1;
    const Vec3 q = retro::cross(s, e1);
    const double v = retro::dot(ray.direction, q) / det;
    if (v < 0 || u + v > 1)
        return -1;
    return retro::dot(e2, q) / det;
}

} // namespace evo::test
