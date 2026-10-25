#include "modern/medium.hpp"

#include "modern/scene.hpp" // linearDeterminant

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace evo::modern {

namespace {

void requireFinite(float v, const char* what) {
    if (!std::isfinite(v))
        throw std::invalid_argument(std::string("medium: ") + what + " is not finite");
}

void requireNonNegative(float v, const char* what) {
    requireFinite(v, what);
    if (v < 0.f)
        throw std::invalid_argument(std::string("medium: ") + what + " is negative");
}

void requireAlbedo(Vec3 albedo, const char* what) {
    for (float c : {albedo.x, albedo.y, albedo.z}) {
        requireFinite(c, what);
        if (c < 0.f || c > 1.f)
            throw std::invalid_argument(std::string("medium: ") + what + " outside [0, 1]");
    }
}

void requirePhase(float g, const char* what) {
    requireFinite(g, what);
    if (!(std::abs(g) < 1.f))
        throw std::invalid_argument(std::string("medium: ") + what + " (HG g) must be in (-1, 1)");
}

void store(float out[3], Vec3 v) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

// (1 - exp(-x)) / x, accurate near 0.
double oneMinusExpOver(double x) {
    if (std::abs(x) < 1e-4)
        return 1.0 - x / 2.0 + x * x / 6.0;
    return -std::expm1(-x) / x;
}

} // namespace

std::uint32_t mediumFlags(const MediumFrame& medium) {
    return (medium.fog ? kMediumFog : 0u) | (medium.grid ? kMediumGrid : 0u);
}

rt::MediumUniforms packMedium(const MediumFrame& medium) {
    rt::MediumUniforms u{};
    if (medium.fog) {
        const FogDesc& f = *medium.fog;
        requireNonNegative(f.density, "the fog's density");
        requireFinite(f.baseHeight, "the fog's base height");
        requireNonNegative(f.falloff, "the fog's falloff");
        requireAlbedo(f.albedo, "the fog's albedo");
        requirePhase(f.phaseG, "the fog's phase");
        store(u.fogAlbedo, f.albedo);
        u.fogPhaseG = f.phaseG;
        u.fogDensity = f.density;
        u.fogBaseHeight = f.baseHeight;
        u.fogFalloff = f.falloff;
    }
    if (medium.grid) {
        const GridMediumDesc& g = *medium.grid;
        for (std::uint32_t n : g.size)
            if (n == 0 || n > kMaxMediumGrid)
                throw std::invalid_argument("medium: a grid of " + std::to_string(n) +
                                            " voxels on an axis (1 to " + std::to_string(kMaxMediumGrid) +
                                            ")");
        for (const auto& row : g.worldToMedium.m)
            for (float v : row)
                requireFinite(v, "the grid's worldToMedium");
        const float det = linearDeterminant(g.worldToMedium);
        if (!(std::abs(det) > 0.f) || !std::isfinite(1.f / det))
            throw std::invalid_argument("medium: the grid's worldToMedium is not invertible");
        requireNonNegative(g.densityScale, "the grid's density scale");
        requireNonNegative(g.emissionScale, "the grid's emission scale");
        requireAlbedo(g.albedo, "the grid's albedo");
        requirePhase(g.phaseG, "the grid's phase");
        for (float p : g.params)
            requireFinite(p, "a grid parameter");
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                u.worldToMedium[r * 4 + c] = g.worldToMedium.m[r][c];
        const std::array<std::uint32_t, 3> cells = majorantSize(g.size);
        for (int a = 0; a < 3; ++a) {
            u.gridSize[a] = g.size[a];
            u.majorantSize[a] = cells[a];
        }
        u.densityScale = g.densityScale;
        store(u.gridAlbedo, g.albedo);
        u.gridPhaseG = g.phaseG;
        u.emissionScale = g.emissionScale;
    }
    return u;
}

std::array<std::uint32_t, 3> majorantSize(std::array<std::uint32_t, 3> size) {
    return {(size[0] + kMajorantCell - 1) / kMajorantCell, (size[1] + kMajorantCell - 1) / kMajorantCell,
            (size[2] + kMajorantCell - 1) / kMajorantCell};
}

rt::Mat34 mediumBox(Vec3 lo, Vec3 hi) {
    const Vec3 extent = hi - lo;
    if (!(extent.x > 0.f && extent.y > 0.f && extent.z > 0.f))
        throw std::invalid_argument("mediumBox: hi must exceed lo on every axis");
    return compose(scale({1.f / extent.x, 1.f / extent.y, 1.f / extent.z}), translate(-lo));
}

double henyeyGreenstein(double g, double cosTheta) {
    const double denom = 1.0 + g * g - 2.0 * g * cosTheta;
    return (1.0 - g * g) / (4.0 * std::numbers::pi * denom * std::sqrt(denom));
}

double sampleHenyeyGreenstein(double g, double u) {
    if (std::abs(g) < 1e-3)
        return std::clamp(1.0 - 2.0 * u, -1.0, 1.0);
    const double s = (1.0 - g * g) / (1.0 - g + 2.0 * g * u);
    return std::clamp((1.0 + g * g - s * s) / (2.0 * g), -1.0, 1.0);
}

double fogOpticalDepth(const FogDesc& fog, Vec3 origin, Vec3 direction, double t0, double t1) {
    if (!(t1 > t0) || !(fog.density > 0.f))
        return 0;
    const double k = fog.falloff;
    // sigma at t0 times the integral of exp(-a s) over the length, a = k * direction.y.
    const double start = double(fog.density) * std::exp(-k * (origin.y + direction.y * t0 - fog.baseHeight));
    const double a = k * direction.y;
    const double length = t1 - t0;
    if (std::isinf(length))
        return a > 0 ? start / a : std::numeric_limits<double>::infinity();
    return start * length * oneMinusExpOver(a * length);
}

} // namespace evo::modern
