#include "modern/scenes/singularity.hpp"

#include "modern/mesh_builder.hpp"
#include "modern/scenes/shots.hpp"

#include <algorithm>
#include <cmath>

namespace evo::modern {

namespace {

using namespace shots;

float smooth01(float x) {
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

// The background galaxy, high behind the hole as S0 sees it.
constexpr Vec3 kGalaxyCentre{-900.f, 650.f, -4200.f};
constexpr float kGalaxyRadius = 1800.f, kGalaxyTilt = 1.15f, kGalaxyYaw = 0.5f, kGalaxyFlux = 2.2e5f;
// The ring star's drift across the sky (radians per second) before the accent and after the held bar.
constexpr float kStarDrift = 0.02f;

// The camera's stations. S2 holds from the accent for one bar at kHold.
struct Station {
    float azimuth, elevation, distance;
};
constexpr Station kHold{-30.f, -4.5f, 44.f};

Key at(const Station& s) {
    return orbit({}, s.azimuth, s.elevation, s.distance);
}

Station mix(const Station& a, const Station& b, float t) {
    return {a.azimuth + (b.azimuth - a.azimuth) * t, a.elevation + (b.elevation - a.elevation) * t,
            a.distance + (b.distance - a.distance) * t};
}

} // namespace

std::uint32_t singularityStarCount(SceneDetail detail) {
    const double scale = std::clamp(static_cast<double>(detail.scale), 0.0, 1.0);
    return std::max(kMinSingularityStars, static_cast<std::uint32_t>(std::llround(kSingularityStars * scale)));
}

std::array<std::uint32_t, 3> discGridSize(SceneDetail detail) {
    const double f = std::cbrt(std::clamp(static_cast<double>(detail.scale), 0.0, 1.0));
    std::array<std::uint32_t, 3> size{};
    for (int a = 0; a < 3; ++a)
        size[a] = std::max(8u, static_cast<std::uint32_t>(std::llround(kDiscGrid[a] * f)));
    return size;
}

LensingFrame singularityLens() {
    LensingFrame lens;
    lens.center = {};
    lens.massRadius = 1.f;
    lens.influenceRadius = 30.f;
    lens.steps = kGeodesicSteps;
    lens.stepScale = 0.2f;
    lens.disc = true;
    return lens;
}

Camera singularityCamera(const show::TimelineFrame& f) {
    const Bars b = barsOf(f);
    const auto t = static_cast<float>(f.localSeconds);
    const auto accent = static_cast<float>(f.accentSeconds);
    const float s1 = b.at(-2), s3 = b.at(2);
    if (t < s1) {
        const float s = smooth01(t / std::max(s1, 1e-3f));
        return makeCamera(at(mix({0.f, 1.2f, 96.f}, {-4.f, 1.6f, 74.f}, s)), 13.7f, 0);
    }
    if (t < b.d) {
        const float s = smooth01((t - s1) / std::max(b.d - s1, 1e-3f));
        return makeCamera(at(mix({-6.f, 3.f, 64.f}, {-24.f, 11.f, 48.f}, s)), 31.8f, 1);
    }
    if (t < s3) {
        // Across the disc's plane by the accent, then still for a bar while the star passes behind the hole.
        const float s = smooth01((t - b.d) / std::max(accent - b.d, 1e-3f));
        return makeCamera(at(mix({-24.f, 11.f, 48.f}, kHold, s)), 31.8f, 2);
    }
    // The fall: inwards on a spiral, the hole sliding out of the frame's centre to the left third.
    const auto end = static_cast<float>(f.chapterDuration);
    const float s = smooth01((t - s3) / std::max(end - s3, 1e-3f));
    const Station station = mix({-32.f, -4.f, 42.f}, {-95.f, 5.f, 9.f}, s);
    Key k = at(station);
    const CameraBasis basis = cameraBasis({k.eye, k.target, k.up, 31.8f});
    k.target = k.target + basis.right * (0.9f * station.distance * s * s);
    return makeCamera(k, 31.8f, 3);
}

Vec3 ringStarDirection(const show::TimelineFrame& f) {
    const Bars b = barsOf(f);
    const auto t = static_cast<float>(f.localSeconds);
    const auto accent = static_cast<float>(f.accentSeconds);
    const Key hold = at(kHold);
    const Vec3 behind = normalize(hold.target - hold.eye);
    // Drifting along the camera's up direction: up to the accent, then still for one bar, then on.
    const float offset = kStarDrift * (std::min(t - accent, 0.f) + std::max(t - accent - b.bar, 0.f));
    const Vec3 up = normalize(hold.up - behind * dot(hold.up, behind));
    return normalize(behind * std::cos(offset) + up * std::sin(offset));
}

float singularityExposureEV(const show::TimelineFrame& f) {
    const Bars b = barsOf(f);
    const auto t = static_cast<float>(f.localSeconds);
    const auto end = static_cast<float>(f.chapterDuration);
    // The last two bars burn to white (RETURN's first frame is white).
    return 0.2f + 6.f * smooth01((t - (end - 2.f * b.bar)) / std::max(2.f * b.bar, 0.1f));
}

Singularity::Singularity(SceneDetail detail) : stars_(singularityStarCount(detail)), grid_(discGridSize(detail)) {
    static_.meshes = {quad(1.f, 1.f)};
    static_.materials.resize(1); // no surfaces: the hole, its disc and the sky are the lens's and the splats'
    static_.capacity.instances = 1;
    static_.capacity.lights = 1;
    static_.capacity.materials = 1;
    static_.capacity.splats = 2 * stars_;
    static_.capacity.mediumGrid = grid_;
    static_.splatShader = kSingularitySplatShader;
    static_.mediumShader = kSingularityMediumShader;
}

SceneFrame Singularity::frame(const show::TimelineFrame& f) const {
    SceneFrame frame;
    frame.camera = singularityCamera(f);
    frame.exposureEV = singularityExposureEV(f);
    frame.lensing = singularityLens();

    const float bass = std::clamp(f.bass, 0.f, 1.f);
    GridMediumDesc disc;
    disc.size = grid_;
    disc.worldToMedium =
        mediumBox({-kDiscHalfWidth, -kDiscHalfHeight, -kDiscHalfWidth}, {kDiscHalfWidth, kDiscHalfHeight, kDiscHalfWidth});
    disc.densityScale = 1.f;
    disc.albedo = {0.f, 0.f, 0.f};
    disc.phaseG = 0.f;
    disc.emissionScale = 1.1f * (1.f + 0.45f * bass); // the disc's brightness beats with the bass
    disc.params = {kDiscHalfWidth, kDiscHalfHeight, 4.f * static_cast<float>(f.localSeconds), 0.55f + 0.4f * bass,
                   kDiscInner, kDiscOuter, 0.62f, 3.f};
    frame.medium = MediumFrame{std::nullopt, disc};

    SplatFrame splats;
    splats.count = 2 * stars_;
    splats.fixedPointScale = 16384.f;
    splats.windowFootprint = 3;
    splats.params = {kGalaxyCentre.x, kGalaxyCentre.y, kGalaxyCentre.z, kGalaxyRadius,
                     kGalaxyTilt,     kGalaxyYaw,      0.3f,            kGalaxyFlux,
                     static_cast<float>(stars_), 0.f, 0.f, 0.f,
                     0.f, 0.f, 0.f, 1.f};
    frame.splats = splats;

    EnvironmentDesc& env = frame.environment;
    env = environmentDefaults(EnvironmentKind::Space);
    env.starBrightness = 1.1f;
    // The bright star that becomes the Einstein ring. Bright enough for the ring to clip to white, and no more:
    // the bloom spreads a share of every pixel's light, and a brighter ring floods the accent's frame.
    env.discDirection = ringStarDirection(f);
    env.discAngularRadius = 0.12f * kDegrees;
    env.discRadiance = {3000.f, 3200.f, 3600.f};
    env.select = 0.f;
    // Deep space is never quite black; over the last two bars the sky burns to white.
    const Bars b = barsOf(f);
    const auto end = static_cast<float>(f.chapterDuration);
    const float white = smooth01((static_cast<float>(f.localSeconds) - (end - 2.f * b.bar)) / std::max(2.f * b.bar, 0.1f));
    env.zenith = env.horizon = env.ground =
        Vec3{0.0035f, 0.0045f, 0.008f} * (1.f - white) + Vec3{0.9f, 0.93f, 1.f} * (0.4f * white * white);
    return frame;
}

} // namespace evo::modern
