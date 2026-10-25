#pragma once

// Analytic spheres (step 5, Task 8): the path tracer's second geometry kind. A sphere is a SphereRecord
// (rt/gpu_types.hpp) in a storage buffer and an AABB in a bottom-level acceleration structure of
// VK_GEOMETRY_TYPE_AABBS_KHR; the intersection shader (shaders/rt/sphere.rint, the math in
// shaders/rt/sphere.glsl) solves the ray against it and the sphere variants of the closest-hits
// (rt_<class>_sphere.rchit.spv) shade it. Hit groups 4..7 are the spheres' (hitGroupFor(class, Spheres)).
//
// A sphere instance places one range of spheres: one AABB BLAS per range, so primitive i of the BLAS is
// sphere first + i of its buffer, and every sphere of one instance shares that instance's hit group (its
// material class). Static spheres (a scene's, built once here) and the particle field (rebuilt every frame,
// rt/particles.hpp) differ only in which buffer and BLAS an instance names.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "rt/accel.hpp"
#include "rt/gpu_types.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace evo::rt {

// Spheres first .. first + count - 1 of a sphere buffer: what one AABB BLAS holds and one sphere instance
// places.
struct SphereRange {
    std::uint32_t first = 0, count = 0;
};
constexpr bool operator==(const SphereRange& a, const SphereRange& b) {
    return a.first == b.first && a.count == b.count;
}

// The hit kinds sphere.rint reports (gl_HitKindEXT): the ray enters the sphere from outside (its front, the
// outward side) or leaves it from inside (a ray refracted into a glass sphere, a camera inside one). Kinds
// 128-255 are reserved for the API (the triangle facing kinds), so these are not those.
inline constexpr std::uint32_t kSphereHitOutside = 0u, kSphereHitInside = 1u;

// --- CPU references of shaders/rt/sphere.glsl (tests, and scenes that pick spheres) ----------------------

using Vec3d = std::array<double, 3>;

struct SphereHit {
    double t;
    bool inside; // the ray leaves the sphere (its origin is inside): kSphereHitInside
};

// The first hit of the ray origin + t direction (direction of any non-zero length) with the sphere's surface
// for t in [tMin, tMax], in double precision: the entry point when it is in range, else the exit point.
// nullopt for a miss, a radius <= 0 or a zero direction.
std::optional<SphereHit> intersectSphere(const Vec3d& origin, const Vec3d& direction, const Vec3d& center,
                                         double radius, double tMin, double tMax);

// The spherical uv of a unit outward normal in the sphere's own frame, as modern::uvSphere maps its mesh: u
// = the angle about +y from +x towards -z over 2 pi, in [0, 1); v = 0 at the top (+y) and 1 at the bottom.
std::array<double, 2> sphereUv(const Vec3d& normal);

// The AABB of a sphere (VkAabbPositionsKHR: min xyz, max xyz). A sphere of radius <= 0 or with a non-finite
// centre or radius gets an inactive AABB (min > max: the BLAS skips it, no ray reaches it).
VkAabbPositionsKHR sphereAabb(const SphereRecord& sphere);

// --- Static spheres ---------------------------------------------------------------------------------------

// A scene's static spheres: one device-local storage buffer of all of them (binding
// EVO_BINDING_STATIC_SPHERES) and one compacted AABB BLAS per range, built once at setup (immediate submits
// that wait; PREFER_FAST_TRACE). No ranges means one range over every sphere.
//
// Lifetime as for a BlasSet: it must outlive every frame whose TLAS points at its BLASes and whose shaders
// read its buffer (declare it before the ring and the Tlas).
class SphereBlas {
public:
    // Throws std::invalid_argument for no spheres, or a range that is empty or reaches past the spheres.
    SphereBlas(gfx::Context& ctx, std::span<const SphereRecord> spheres,
               std::span<const SphereRange> ranges = {});

    // Range `range`'s BLAS (std::out_of_range past the ranges).
    VkDeviceAddress address(std::uint32_t range = 0) const { return blases_.address(range); }
    std::span<const SphereRange> ranges() const { return ranges_; }
    // The index of the range (first, count), if it is one of them.
    std::optional<std::uint32_t> findRange(SphereRange range) const;
    const gfx::Buffer& records() const { return records_; }
    // The device memory held, as allocated (VMA): the sphere buffer and the compacted BLASes.
    VkDeviceSize bytes() const { return records_.bytes() + blases_.bytes(); }

private:
    std::vector<SphereRange> ranges_;
    gfx::Buffer records_;
    BlasSet blases_;
};

// The ranges a scene's spheres are built in: `ranges`, or one over all `sphereCount` when it is empty.
std::vector<SphereRange> sphereRangesOf(std::span<const SphereRange> ranges, std::uint32_t sphereCount);

} // namespace evo::rt
