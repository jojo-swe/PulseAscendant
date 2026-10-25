#include "rt/spheres.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <string>

namespace evo::rt {

namespace {

double dot3(const Vec3d& a, const Vec3d& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

bool finite(const SphereRecord& s) {
    return std::isfinite(s.center[0]) && std::isfinite(s.center[1]) && std::isfinite(s.center[2]) &&
           std::isfinite(s.radius);
}

// Every sphere's AABB in one device-local buffer, and one BLAS input per range over its part of it. Lives
// for the full expression that builds the BlasSet (the build reads the buffer).
class AabbInputs {
public:
    AabbInputs(gfx::Context& ctx, std::span<const SphereRecord> spheres,
               std::span<const SphereRange> ranges) {
        std::vector<VkAabbPositionsKHR> aabbs(spheres.size());
        for (std::size_t i = 0; i < spheres.size(); ++i)
            aabbs[i] = sphereAabb(spheres[i]);
        const auto bytes = std::as_bytes(std::span(aabbs));
        buffer_ =
            gfx::createDeviceBuffer(ctx,
                                    {bytes.size(),
                                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                     false, "sphere aabbs"},
                                    bytes); // setup
        const VkDeviceAddress base = buffer_.address();
        for (const SphereRange& range : ranges) {
            BlasInput input{};
            input.geometry = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            input.geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            input.geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            input.geometry.geometry.aabbs = {
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR};
            input.geometry.geometry.aabbs.data.deviceAddress =
                base + VkDeviceSize{range.first} * sizeof(VkAabbPositionsKHR);
            input.geometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);
            input.primitiveCount = range.count;
            inputs_.push_back(input);
        }
    }
    std::span<const BlasInput> inputs() const { return inputs_; }

private:
    gfx::Buffer buffer_;
    std::vector<BlasInput> inputs_;
};

std::span<const SphereRecord> checkedSpheres(std::span<const SphereRecord> spheres) {
    if (spheres.empty())
        throw std::invalid_argument("SphereBlas: no spheres");
    return spheres;
}

std::vector<SphereRange> checkedRanges(std::span<const SphereRange> ranges, std::size_t sphereCount) {
    std::vector<SphereRange> out = sphereRangesOf(ranges, static_cast<std::uint32_t>(sphereCount));
    for (std::size_t i = 0; i < out.size(); ++i) {
        const SphereRange& r = out[i];
        if (r.count == 0 || std::uint64_t{r.first} + r.count > sphereCount)
            throw std::invalid_argument("SphereBlas: range " + std::to_string(i) + " (" +
                                        std::to_string(r.first) + ", " + std::to_string(r.count) +
                                        ") is empty or reaches past the " + std::to_string(sphereCount) +
                                        " spheres");
    }
    return out;
}

gfx::Buffer recordBuffer(gfx::Context& ctx, std::span<const SphereRecord> spheres) {
    const auto bytes = std::as_bytes(spheres);
    return gfx::createDeviceBuffer(
        ctx, {bytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "static spheres"}, bytes); // setup
}

} // namespace

std::optional<SphereHit> intersectSphere(const Vec3d& origin, const Vec3d& direction, const Vec3d& center,
                                         double radius, double tMin, double tMax) {
    const double a = dot3(direction, direction);
    if (!(radius > 0.0) || !(a > 0.0))
        return std::nullopt;
    const Vec3d f{origin[0] - center[0], origin[1] - center[1], origin[2] - center[2]};
    const double b = dot3(f, direction);
    const double c = dot3(f, f) - radius * radius;
    const double disc = b * b - a * c;
    if (disc < 0.0)
        return std::nullopt;
    const double root = std::sqrt(disc);
    const double t0 = (-b - root) / a, t1 = (-b + root) / a;
    if (t0 >= tMin && t0 <= tMax)
        return SphereHit{t0, false};
    if (t0 < tMin && t1 >= tMin && t1 <= tMax)
        return SphereHit{t1, true};
    return std::nullopt;
}

std::array<double, 2> sphereUv(const Vec3d& n) {
    constexpr double pi = std::numbers::pi;
    double u = std::atan2(-n[2], n[0]) / (2.0 * pi);
    if (u < 0.0)
        u += 1.0;
    if (u >= 1.0)
        u -= 1.0;
    return {u, std::acos(std::clamp(n[1], -1.0, 1.0)) / pi};
}

VkAabbPositionsKHR sphereAabb(const SphereRecord& s) {
    if (!finite(s) || !(s.radius > 0.f))
        return {1.f, 1.f, 1.f, -1.f, -1.f, -1.f}; // min > max: inactive
    return {s.center[0] - s.radius, s.center[1] - s.radius, s.center[2] - s.radius,
            s.center[0] + s.radius, s.center[1] + s.radius, s.center[2] + s.radius};
}

std::vector<SphereRange> sphereRangesOf(std::span<const SphereRange> ranges, std::uint32_t sphereCount) {
    if (ranges.empty())
        return {SphereRange{0, sphereCount}};
    return {ranges.begin(), ranges.end()};
}

SphereBlas::SphereBlas(gfx::Context& ctx, std::span<const SphereRecord> spheres,
                       std::span<const SphereRange> ranges)
    : ranges_(checkedRanges(ranges, checkedSpheres(spheres).size())), records_(recordBuffer(ctx, spheres)),
      blases_(ctx, AabbInputs(ctx, spheres, ranges_).inputs()) {}

std::optional<std::uint32_t> SphereBlas::findRange(SphereRange range) const {
    const auto it = std::find(ranges_.begin(), ranges_.end(), range);
    if (it == ranges_.end())
        return std::nullopt;
    return static_cast<std::uint32_t>(it - ranges_.begin());
}

} // namespace evo::rt
