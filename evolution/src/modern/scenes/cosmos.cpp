#include "modern/scenes/cosmos.hpp"

#include "modern/mesh_builder.hpp"
#include "modern/scenes/logo_targets.hpp"
#include "modern/scenes/shots.hpp"
#include "retro/boot.hpp"
#include "retro/dots_schedule.hpp"
#include "show/era.hpp"
#include "text/font8x8.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>
#include <span>

namespace evo::modern {

namespace {

using namespace shots;

constexpr float kPi = 3.14159265f;
constexpr float kTau = 6.28318531f;

// --- galaxy.glsl's dust on the CPU
// -------------------------------------------------------------------------------

constexpr float kGalaxyPitch = 0.26f, kGalaxyArmStart = 0.07f, kGalaxyLaneOffset = 0.3f;
constexpr float kDustDensityScale = 0.7f; // sigma_t per world unit of a dust value of 1 (GALAXY's radius)
constexpr float kDustHii = 0.45f;          // the HII knots' emission

constexpr float kGalaxyBranchStart = 0.38f;

float galaxyWrap(float a) {
    return a - kTau * std::floor(a / kTau + 0.5f);
}

float galaxySpine(float lr, int arm) {
    if (arm == 1)
        return lr / (1.1f * kGalaxyPitch) + kPi + 0.3f * std::sin(1.7f * lr + 2.6f) + 0.1f * std::sin(4.6f * lr + 0.4f);
    float spine = lr / kGalaxyPitch + 0.22f * std::sin(2.1f * lr + 0.7f) + 0.08f * std::sin(5.3f * lr + 2.f);
    if (arm == 2)
        spine -= 1.25f * std::max(lr - std::log(kGalaxyBranchStart / kGalaxyArmStart), 0.f);
    return spine;
}

float galaxyArmOffset(float radius, float azimuth, int arm) {
    return galaxyWrap(azimuth - galaxySpine(std::log(std::max(radius, 1e-4f) / kGalaxyArmStart), arm));
}

float galaxyArmPhase(float radius, float azimuth) {
    const float a0 = galaxyArmOffset(radius, azimuth, 0), a1 = galaxyArmOffset(radius, azimuth, 1);
    return std::abs(a0) <= std::abs(a1) ? a0 : a1;
}

float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float lattice(int x, int y, int z) {
    return logoUnit(logoHash(static_cast<std::uint32_t>(x) * 0x8DA6B343u ^ static_cast<std::uint32_t>(y) * 0xD8163841u ^
                             static_cast<std::uint32_t>(z) * 0xCB1AB31Fu));
}

float noise(Vec3 p) {
    const float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
    const int x = static_cast<int>(fx), y = static_cast<int>(fy), z = static_cast<int>(fz);
    const auto ease = [](float t) { return t * t * (3.f - 2.f * t); };
    const float tx = ease(p.x - fx), ty = ease(p.y - fy), tz = ease(p.z - fz);
    const auto mix = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00 = mix(lattice(x, y, z), lattice(x + 1, y, z), tx);
    const float x10 = mix(lattice(x, y + 1, z), lattice(x + 1, y + 1, z), tx);
    const float x01 = mix(lattice(x, y, z + 1), lattice(x + 1, y, z + 1), tx);
    const float x11 = mix(lattice(x, y + 1, z + 1), lattice(x + 1, y + 1, z + 1), tx);
    return mix(mix(x00, x10, ty), mix(x01, x11, ty), tz);
}

Vec3 turnAboutY(Vec3 p, float turn) {
    const float c = std::cos(turn), s = std::sin(turn);
    return {c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
}

// --- The particles (cosmos_particles.comp on the CPU)
// ------------------------------------------------------------

constexpr float kFullCount = 2097152.f;
constexpr float kParticleRadius = 0.0052f, kReferenceCell = 0.09f;

float hashUnit(std::uint32_t x) {
    return logoUnit(logoHash(x));
}

float golden(std::uint32_t i) {
    return logoUnit(i * 0x9E3779B9u);
}

float smooth01(float x) {
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

Vec3 bezier(Vec3 a, Vec3 b, Vec3 c, float e) {
    const float u = 1.f - e;
    return u * u * a + 2.f * u * e * b + e * e * c;
}

Vec3 turn(Vec3 p, float yaw, float pitch) {
    const float cy = std::cos(yaw), sy = std::sin(yaw), cx = std::cos(pitch), sx = std::sin(pitch);
    const float x = cy * p.x + sy * p.z, z = -sy * p.x + cy * p.z;
    return {x, cx * p.y - sx * z, sx * p.y + cx * z};
}

Vec3 mixv(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

Vec3 identityColour(float x, std::uint32_t h) {
    const float u = std::clamp(0.5f + 0.5f * x + 0.16f * (logoUnit(h) - 0.5f), 0.f, 1.f);
    const Vec3 ember{1.f, 0.36f, 0.08f}, ivory{0.95f, 0.88f, 0.76f}, ice{0.5f, 0.74f, 1.f};
    return u < 0.5f ? ember + (ivory - ember) * (u * 2.f) : ivory + (ice - ivory) * (u * 2.f - 1.f);
}

constexpr std::uint32_t kStrands = 72; // cosmos_particles.comp's
constexpr float kAscendantCoreRadius = 2.5f;

void setColor(float out[3], Vec3 c) {
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
}


// The 1993 cube's turn (DOTS' spin: retro::dotsShapePoints) as REBOOT's CRT shows it at GALAXY chapter time t,
// frozen from the accent on: its yaw and pitch.
std::array<float, 2> rebootCubeTurn(const show::TimelineFrame& f, float t) {
    const double galaxyStart = f.showSeconds - f.localSeconds;
    const RebootMarks reboot = rebootMarks(f);
    const double at = galaxyStart + std::min(t, reboot.accent);
    const show::TimelineFrame frame = show::sampleTimeline(at);
    const auto retroFrame = static_cast<std::uint32_t>(std::floor(std::max(frame.localSeconds, 0.0) * retro::kDotsRetroRate));
    const std::uint32_t cube = retro::rebootCubeFrame(frame);
    const double seconds = static_cast<double>(retroFrame > cube ? retroFrame - cube : 0) / retro::kDotsRetroRate;
    return {static_cast<float>(0.6 + 0.85 * seconds), static_cast<float>(0.4 + 0.12 * seconds)};
}

// An orbit's place on its sphere (azimuth and elevation in degrees, distance) at a time, for the keyed
// approach.
struct OrbitKey {
    float time, azimuth, elevation, distance;
};

// Catmull-Rom through the keys (tangents from the neighbours over their times; zero at the last key, so the
// approach settles), clamped to the first and last.
Key throughOrbits(std::span<const OrbitKey> keys, float t) {
    const std::size_t n = keys.size();
    const auto value = [&](std::size_t k) { return Vec3{keys[k].azimuth, keys[k].elevation, keys[k].distance}; };
    Vec3 v;
    if (t <= keys.front().time) {
        v = value(0);
    } else if (t >= keys.back().time) {
        v = value(n - 1);
    } else {
        std::size_t i = 0;
        while (i + 2 < n && t >= keys[i + 1].time)
            ++i;
        const auto tangent = [&](std::size_t k) -> Vec3 {
            if (k + 1 >= n)
                return {};
            const std::size_t lo = k == 0 ? 0 : k - 1;
            return (value(k + 1) - value(lo)) / (keys[k + 1].time - keys[lo].time);
        };
        const float h = keys[i + 1].time - keys[i].time, u = (t - keys[i].time) / h;
        const float u2 = u * u, u3 = u2 * u;
        v = (2.f * u3 - 3.f * u2 + 1.f) * value(i) + (u3 - 2.f * u2 + u) * h * tangent(i) +
            (-2.f * u3 + 3.f * u2) * value(i + 1) + (u3 - u2) * h * tangent(i + 1);
    }
    return orbit({}, v.x, v.y, v.z);
}

// The approach through REBOOT's glass and into GALAXY: from the crossing (D of REBOOT, where the director
// switches to this scene at negative chapter time) the camera is already flying in, past the 1993 cube's
// points, and slows to the edge-on hold. The key at REBOOT's accent is where the cube freezes into stars.
constexpr OrbitKey kCubeKey{0.f, 11.6f, 2.1f, 960.f}; // its time is REBOOT's accent (GALAXY's chapter seconds)

std::array<OrbitKey, 4> approachKeys(const show::TimelineFrame& f) {
    const RebootMarks reboot = rebootMarks(f);
    const Bars b = barsOf(f);
    OrbitKey cube = kCubeKey;
    cube.time = reboot.accent;
    return {{{reboot.crossing, 12.f, 2.2f, 1000.f}, cube, {0.f, 8.f, 1.6f, 470.f}, {b.at(-4), 2.f, 1.4f, 290.f}}};
}

// GALAXY: shot 0 the 85 mm edge-on approach (from REBOOT's crossing: see approachKeys); from the bar line four
// bars before D, shot 1 on 35 mm: the swing above the plane to face-on at D, the hold through the accent, and
// from four bars after D the dive into an arm.
Camera galaxyCamera(const show::TimelineFrame& f) {
    const Bars b = barsOf(f);
    const auto t = static_cast<float>(f.localSeconds);
    const float cut = b.at(-4), dive = b.at(4);
    if (t < cut) {
        const std::array<OrbitKey, 4> keys = approachKeys(f);
        return makeCamera(throughOrbits(keys, t), kLens85, 0);
    }
    if (t < dive) {
        // The swing: elevation from a few degrees to straight down by D, turning a little; then a slow push in.
        const float swing = smooth01((t - cut) / std::max(b.d - cut, 1e-3f));
        const float hold = smooth01((t - b.d) / std::max(dive - b.d, 1e-3f));
        const float elevation = 6.f + 83.5f * swing;
        const float azimuth = -10.f + 40.f * swing + 8.f * hold;
        const float distance = 250.f - 60.f * swing - 14.f * hold;
        return makeCamera(orbit({}, azimuth, elevation, distance), kLens35, 1);
    }
    // The dive: down into the arm on the camera's right, levelling out inside the disc, stars streaming past.
    const float s = smooth01((t - dive) / std::max(static_cast<float>(f.chapterDuration) - dive, 1e-3f));
    const Key top = orbit({}, 38.f, 89.5f, 176.f);
    const Key keys[] = {top,
                        {{10.f, 120.f, 40.f}, {16.f, 0.f, 12.f}, normalize(Vec3{0.2f, 0.4f, -1.f})},
                        {{22.f, 34.f, 44.f}, {30.f, 0.f, 4.f}, normalize(Vec3{0.1f, 0.8f, -0.6f})},
                        {{30.f, 4.5f, 34.f}, {36.f, 1.f, -14.f}, {0.f, 1.f, 0.f}}};
    return makeCamera(alongKeys(keys, s), kLens35, 1);
}

// ASCENDANT: shot 0 on 85 mm, the galaxy's core in the sky; from four bars before D, shot 1 on 35 mm as the
// particles stream in; from D, shot 2: one slow 50 mm orbit about the logo, the sun from camera-right.
Camera ascendantCamera(const show::TimelineFrame& f) {
    const Bars b = barsOf(f);
    const auto t = static_cast<float>(f.localSeconds);
    const float lift = b.at(-4);
    if (t < lift) {
        const float s = smooth01(t / std::max(lift, 1e-3f));
        const Key keys[] = {{{0.f, 0.4f, 15.f}, kAscendantGalaxyCentre + Vec3{4.f, -6.f, 0.f}, {0.f, 1.f, 0.f}},
                            {{-0.6f, 0.6f, 14.f}, kAscendantGalaxyCentre + Vec3{0.f, -2.f, 0.f}, {0.f, 1.f, 0.f}}};
        return makeCamera(alongKeys(keys, s), kLens85, 0);
    }
    if (t < b.d) {
        const float s = smooth01((t - lift) / std::max(b.d - lift, 1e-3f));
        const Key keys[] = {{{-3.5f, 1.4f, 22.f}, {2.f, 14.f, -150.f}, {0.f, 1.f, 0.f}},
                            {{-3.f, 1.1f, 19.f}, {1.f, 5.f, -50.f}, {0.f, 1.f, 0.f}},
                            {{-2.f, 0.9f, 16.f}, {0.f, 0.6f, -3.f}, {0.f, 1.f, 0.f}}};
        return makeCamera(alongKeys(keys, s), kLens35, 1);
    }
    const auto end = static_cast<float>(f.chapterDuration);
    const float orbitEnd = b.at(4);
    const float s1 = smooth01((t - b.d) / std::max(orbitEnd - b.d, 1e-3f));
    const float s2 = smooth01((t - orbitEnd) / std::max(end - 1.2f - orbitEnd, 1e-3f));
    const float azimuth = -16.f + 34.f * s1 + 14.f * s2;
    const float elevation = 3.f + 4.f * s1 + 5.f * s2;
    const float distance = 13.2f - 0.8f * s1 - 1.2f * s2;
    Key k = orbit({0.f, 0.15f, 0.f}, azimuth, elevation, distance);
    k.up = {0.f, 1.f, 0.f};
    return makeCamera(k, kLens50, 2, 0.06f, distance);
}

// worldToMedium of the dust's box in that frame.
rt::Mat34 dustBox(Vec3 centre, float radius, float tilt, float yaw) {
    const rt::Mat34 toGalaxy =
        compose(scale({1.f / radius, 1.f / radius, 1.f / radius}),
                compose(rotate({1.f, 0.f, 0.f}, -tilt), compose(rotate({0.f, 1.f, 0.f}, -yaw), translate(-centre))));
    return compose(mediumBox({-1.f, -kGalaxyDustHalfHeight, -1.f}, {1.f, kGalaxyDustHalfHeight, 1.f}), toGalaxy);
}

rt::Material emitter(Vec3 color, float intensity) {
    rt::Material m;
    setColor(m.baseColor, {0.f, 0.f, 0.f});
    setColor(m.emission, color);
    m.emissionScale = intensity;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    return m;
}

rt::Mat34 rotationFromTo(Vec3 a, Vec3 b) {
    const Vec3 axis = cross(a, b);
    const float c = std::clamp(dot(a, b), -1.f, 1.f);
    if (length(axis) < 1e-6f)
        return identity();
    return rotate(axis, std::acos(c));
}

const Vec3 kSun = normalize(Vec3{0.85f, 0.3f, 0.42f}); // camera-right in ASCENDANT's orbit

} // namespace

// --- REBOOT's crossing
// -------------------------------------------------------------------------------------------

RebootMarks rebootMarks(const show::TimelineFrame& f) {
    const double galaxyStart = f.showSeconds - f.localSeconds;
    const int reboot = static_cast<int>(show::Effect::Reboot);
    const double start = show::chapterStart(reboot), end = show::chapterStart(reboot + 1);
    const show::TransitionMarks marks = show::transitionMarks(show::sampleTimeline(0.5 * (start + end)));
    return {static_cast<float>(marks.downbeat - galaxyStart), static_cast<float>(marks.accent - galaxyStart)};
}

Vec3 galaxyCubeStar(std::uint32_t id, const std::array<float, 16>& params) {
    constexpr float kCubeDistance = 150.f, kCubeScale = kCubeDistance / 6.f; // cosmos_splats.comp's
    const float a = params[11] * kDegrees, e = params[12] * kDegrees, distance = params[13];
    const Vec3 out{std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
    const Vec3 up{-std::sin(e) * std::sin(a), std::cos(e), -std::sin(e) * std::cos(a)};
    const Vec3 forward = -out;
    const Vec3 right = cross(forward, up);
    const Vec3 lattice = Vec3{static_cast<float>(id % 8u), static_cast<float>(id / 64u), static_cast<float>((id / 8u) % 8u)} *
                             (1.8f / 7.f) -
                         Vec3{0.9f, 0.9f, 0.9f};
    const float cy = std::cos(params[14]), sy = std::sin(params[14]), cx = std::cos(params[15]), sx = std::sin(params[15]);
    const float x = cy * lattice.x + sy * lattice.z, z = -sy * lattice.x + cy * lattice.z;
    const Vec3 q{x, cx * lattice.y - sx * z, sx * lattice.y + cx * z};
    return out * distance + forward * kCubeDistance + kCubeScale * (q.x * right + q.y * up + q.z * forward);
}

// --- Counts
// ----------------------------------------------------------------------------------------------------

std::uint32_t galaxyStarCount(SceneDetail detail) {
    const double scale = std::clamp(static_cast<double>(detail.scale), 0.0, 1.0);
    return std::max(kMinGalaxyStars, static_cast<std::uint32_t>(std::llround(kGalaxyStars * scale)));
}

std::uint32_t ascendantParticleCount(SceneDetail detail) {
    const double scale = std::clamp(static_cast<double>(detail.scale), 0.0, 1.0);
    return std::max(kMinAscendantParticles,
                    static_cast<std::uint32_t>(std::llround(kAscendantParticles * scale)));
}

std::array<std::uint32_t, 3> galaxyDustSize(SceneDetail detail) {
    const double f = std::cbrt(std::clamp(static_cast<double>(detail.scale), 0.0, 1.0));
    std::array<std::uint32_t, 3> size{};
    for (int a = 0; a < 3; ++a)
        size[a] = std::max(8u, static_cast<std::uint32_t>(std::llround(kGalaxyDust[a] * f)));
    return size;
}

// --- The dust
// --------------------------------------------------------------------------------------------------

Vec2 galaxyDust(Vec3 p) {
    const float radius = std::sqrt(p.x * p.x + p.z * p.z);
    const float azimuth = std::atan2(p.z, p.x);
    const float lr = std::log(std::max(radius, 1e-4f) / kGalaxyArmStart);
    const float vertical = std::exp(-std::abs(p.y) / 0.011f);
    const float envelope = smoothstep(0.05f, 0.16f, radius) * (1.f - smoothstep(0.82f, 1.05f, radius));
    const float laneWidth = 0.08f + 0.05f * radius;
    float lane = 0.f, feathers = 0.f;
    for (int arm = 0; arm < 2; ++arm) {
        const float off = galaxyArmOffset(radius, azimuth, arm);
        const float strength = arm == 0 ? 1.f : 0.8f;
        lane += strength * (std::exp(-std::pow((off + kGalaxyLaneOffset) / laneWidth, 2.f)) +
                            0.5f * std::exp(-std::pow((off + 0.6f) / (1.6f * laneWidth), 2.f)));
        const float gap = smoothstep(-1.5f, -1.1f, off) * (1.f - smoothstep(-0.42f, -0.3f, off));
        const float spur = std::pow(std::max(std::sin(8.f * (off + 0.5f * lr) + 1.3f * static_cast<float>(arm)), 0.f), 10.f);
        feathers += strength * gap * spur;
    }
    const float branch = galaxyArmOffset(radius, azimuth, 2);
    lane += 0.65f * smoothstep(kGalaxyBranchStart, kGalaxyBranchStart + 0.1f, radius) *
            std::exp(-std::pow((branch + 0.22f) / (0.8f * laneWidth), 2.f));
    const float n = 0.65f * noise(p * 22.f) + 0.35f * noise(p * 61.f + Vec3{17.f, 17.f, 17.f});
    const float filaments = smoothstep(0.3f, 0.7f, n);
    const float disc = 0.22f * std::exp(-radius / 0.4f) * smoothstep(0.02f, 0.08f, radius);
    const float density =
        vertical * envelope * (4.f * lane * filaments + 1.6f * feathers * filaments + disc * (0.5f + filaments));
    const float spine = std::exp(-std::pow((galaxyArmPhase(radius, azimuth) - 0.03f) / 0.07f, 2.f));
    const float knot = std::pow(noise(p * 48.f + Vec3{5.f, 5.f, 5.f}), 4.f) * spine * envelope *
                       std::exp(-std::abs(p.y) / 0.008f);
    return {density + 2.f * knot, 22.f * knot};
}

float galaxyDustDensity(Vec3 world, float turn) {
    const Vec3 p = turnAboutY(world / kGalaxyRadius, -turn);
    if (std::abs(p.x) > 1.f || std::abs(p.z) > 1.f || std::abs(p.y) > kGalaxyDustHalfHeight)
        return 0.f;
    return kDustDensityScale * galaxyDust(p).x;
}

float galaxyTurn(const show::TimelineFrame& frame) {
    // A slow, steady turn (the arms trail): about 3 degrees over a chapter.
    return 0.002f * static_cast<float>(frame.localSeconds) + (frame.effect == show::Effect::Ascendant ? 1.1f : 0.f);
}

SplatFrame galaxySplatFrame(const show::TimelineFrame& frame, std::uint32_t stars) {
    const bool galaxy = frame.effect != show::Effect::Ascendant;
    const Vec3 centre = galaxy ? Vec3{} : kAscendantGalaxyCentre;
    const float radius = galaxy ? kGalaxyRadius : kAscendantGalaxyRadius;
    const float tilt = galaxy ? 0.f : kAscendantGalaxyTilt, yaw = galaxy ? 0.f : kAscendantGalaxyYaw;
    SplatFrame s;
    s.count = stars;
    s.fixedPointScale = 16384.f;
    s.windowFootprint = 3; // 16.7M stars: 5x5 in exports, 3x3 in the window
    const float flux = galaxy ? 2.6e5f : 1.3e5f;
    const float bulge = 1.f + 0.35f * std::clamp(frame.energy, 0.f, 1.f);
    s.params = {centre.x, centre.y, centre.z, radius, tilt, yaw, galaxyTurn(frame), flux,
                static_cast<float>(stars), bulge, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    if (galaxy) {
        // The 1993 cube's 512 points, in front of the camera on REBOOT's accent as the CRT framed them.
        const std::array<float, 2> turn = rebootCubeTurn(frame, static_cast<float>(frame.localSeconds));
        s.params[11] = kCubeKey.azimuth;
        s.params[12] = kCubeKey.elevation;
        s.params[13] = kCubeKey.distance;
        s.params[14] = turn[0];
        s.params[15] = turn[1];
    }
    return s;
}

// --- ASCENDANT's flight
// ----------------------------------------------------------------------------------------

AscendantTimes ascendantTimes(const show::TimelineFrame& frame) {
    const Bars b = barsOf(frame);
    AscendantTimes t{};
    t.accent = static_cast<float>(frame.accentSeconds);
    t.waypoint = b.at(-1);
    t.hold = kWaypointHold;
    t.bar = b.bar;
    float last = b.d;
    std::size_t lastIndex = 0;
    for (std::size_t i = 0; i < b.downbeats.size(); ++i)
        if (b.downbeats[i] <= static_cast<float>(frame.chapterDuration) - 0.25f) {
            last = b.downbeats[i];
            lastIndex = i;
        }
    t.release = last;
    // The rows light from the fourth bar after D, or, when the release comes sooner, from the downbeat seven
    // bars before it, so the top row is lit a bar before the release; never before D.
    t.firstRow = b.at(4);
    if (lastIndex >= 7)
        t.firstRow = std::min(t.firstRow, b.downbeats[lastIndex - 7]);
    t.firstRow = std::max(t.firstRow, b.d);
    t.liftStart = b.at(-4);
    t.liftSpread = std::max(t.waypoint - 1.2f - t.liftStart, 0.5f);
    return t;
}

ParticleFrame ascendantParticleFrame(const show::TimelineFrame& frame, std::uint32_t count,
                                     const WaypointTargets* waypoint) {
    const AscendantTimes t = ascendantTimes(frame);
    ParticleFrame p;
    p.count = count;
    p.params = {static_cast<float>(count),
                t.accent,
                t.liftStart,
                t.liftSpread,
                t.waypoint,
                t.hold,
                waypoint ? 1.f : 0.f, // the waypoint formation: the text, or the vortex
                t.release,
                t.firstRow,
                t.bar,
                std::clamp(frame.kick, 0.f, 1.f),
                kLogoCell,
                0.f,
                0.f,
                0.f,
                0.f};
    if (waypoint)
        std::copy(waypoint->packed.begin(), waypoint->packed.end(), p.params.begin() + 12);
    p.material = Cosmos::kParticles;
    return p;
}

Vec3 ascendantTarget(std::uint32_t id, float cell) {
    return logoTarget(id, cell);
}

// --- The text waypoint (waypoint_text.glsl) -------------------------------------------------------------

namespace {

std::uint32_t waypointCode(const std::array<float, 4>& packed, std::uint32_t i) {
    const auto word = static_cast<std::uint32_t>(packed[i / 4]);
    return (word >> (6 * (i % 4))) & 63u;
}

std::uint32_t waypointBits(const std::array<float, 4>& packed, std::uint32_t glyph, std::uint32_t row) {
    return text::glyph(static_cast<char>(32 + waypointCode(packed, glyph)))[row];
}

} // namespace

WaypointTargets waypointTargets(const std::string& line) {
    WaypointTargets w;
    std::array<std::uint32_t, 4> words{};
    for (std::size_t i = 0; i < kWaypointGlyphs; ++i) {
        char c = i < line.size() ? static_cast<char>(std::toupper(static_cast<unsigned char>(line[i]))) : ' ';
        if (c < 32 || c > 95)
            c = '?';
        w.text += c;
        words[i / 4] |= static_cast<std::uint32_t>(c - 32) << (6 * (i % 4));
    }
    while (!w.text.empty() && w.text.back() == ' ')
        w.text.pop_back();
    for (std::size_t k = 0; k < 4; ++k)
        w.packed[k] = static_cast<float>(words[k]);
    std::uint32_t loColumn = 1000, loRow = 1000, hiColumn = 0, hiRow = 0;
    for (std::uint32_t row = 0; row < 8; ++row)
        for (std::uint32_t glyph = 0; glyph < kWaypointGlyphs; ++glyph) {
            const std::uint32_t bits = waypointBits(w.packed, glyph, row);
            for (std::uint32_t c = 0; c < 8; ++c)
                if ((bits & (0x80u >> c)) != 0) {
                    w.pixels.push_back({glyph * 8 + c, row});
                    loColumn = std::min(loColumn, glyph * 8 + c), hiColumn = std::max(hiColumn, glyph * 8 + c);
                    loRow = std::min(loRow, row), hiRow = std::max(hiRow, row);
                }
        }
    if (!w.pixels.empty())
        w.centre = {0.5f * static_cast<float>(loColumn + hiColumn), 0.5f * static_cast<float>(loRow + hiRow)};
    return w;
}

Vec3 WaypointTargets::target(std::uint32_t id, float cell) const {
    const auto lit = static_cast<std::uint32_t>(pixels.size());
    if (lit == 0)
        return {};
    const std::uint32_t voxels = lit * kWaypointDepth;
    const std::uint32_t v = id % voxels, k = id / voxels;
    const LogoPixel pixel = pixels[v % lit];
    const std::uint32_t layer = v / lit;
    const std::uint32_t h = logoHash(v);
    const float jx = logoUnit(h + k * 0xD1B54A32u);
    const float jy = logoUnit(logoHash(h) + k * 0xABC98388u);
    const float jz = logoUnit(logoHash(h ^ 0x68E31DA4u) + k * 0x8CB92BA7u);
    return {(static_cast<float>(pixel.column) + jx - 0.5f - centre.x) * cell,
            (centre.y - static_cast<float>(pixel.row) - jy + 0.5f) * cell,
            (1.f - static_cast<float>(layer) - jz) * cell};
}

Vec3 ascendantWaypoint(std::uint32_t formation, std::uint32_t id, float cell, const std::array<float, 4>& text) {
    const float width = 71.f * cell;
    if (formation == 1) {
        const WaypointTargets w = waypointTargets(decodeWaypointText(text));
        if (!w.pixels.empty())
            return w.target(id, cell) + Vec3{0.f, 0.12f * width, -0.35f * width};
    }
    const float a = kTau * golden(id) + 2.5f * hashUnit(id ^ 0x77AAu);
    const float r = width * (0.12f + 0.42f * std::sqrt(hashUnit(id ^ 0x1F3Du)));
    const float wobble = 0.06f * width * (hashUnit(id ^ 0x5151u) - 0.5f);
    return {r * std::cos(a), 0.55f * r * std::sin(a) + 0.12f * width + wobble,
            -0.55f * width + 0.25f * r * std::sin(a)};
}

std::string decodeWaypointText(const std::array<float, 4>& packed) {
    std::string out;
    for (std::uint32_t i = 0; i < kWaypointGlyphs; ++i)
        out += static_cast<char>(32 + waypointCode(packed, i));
    return out;
}

rt::SphereRecord ascendantParticle(std::uint32_t id, float t, const std::array<float, 16>& params) {
    const auto count = static_cast<std::uint32_t>(params[0]);
    const float accent = params[1], liftStart = params[2], liftSpread = params[3];
    const float wayTime = params[4], wayHold = params[5], release = params[7];
    const auto formation = static_cast<std::uint32_t>(params[6]);
    const float rowStart = params[8], bar = params[9], flash = params[10], cell = params[11];
    const Vec3 core = kAscendantGalaxyCentre;
    const std::array<float, 4> text{params[12], params[13], params[14], params[15]};

    const LogoPixel pixel = logoPixel((id % (kLogoPixels * kLogoDepth)) % kLogoPixels);
    const std::uint32_t row = pixel.row;
    const Vec3 target = logoTarget(id, cell);
    const std::uint32_t h = logoHash(id ^ 0x41534344u);
    const std::uint32_t strand = logoHash(id ^ 0x5354u) % kStrands;
    const std::uint32_t sh = logoHash(strand * 0x9E3779B9u + 0x51u);
    const float g1 = std::sqrt(-2.f * std::log(std::max(hashUnit(h + 1u), 1e-7f)));
    Vec3 ball{std::cos(kTau * hashUnit(h + 2u)) * g1, std::sin(kTau * hashUnit(h + 2u)) * g1, 0.f};
    ball.z = std::sqrt(-2.f * std::log(std::max(hashUnit(h + 3u), 1e-7f))) * std::cos(kTau * hashUnit(h + 4u));
    const Vec3 start = core + kAscendantCoreRadius * Vec3{ball.x, 0.45f * ball.y, ball.z};
    const float lift = liftStart + liftSpread * (0.8f * hashUnit(sh + 5u) + 0.2f * hashUnit(h + 5u));
    const Vec3 way = ascendantWaypoint(formation, id, cell, text);
    const float width = 71.f * cell;

    Vec3 p;
    float flight = 0.f;
    if (t <= lift) {
        p = start;
    } else if (t < wayTime) {
        const float s = (t - lift) / (wayTime - lift);
        const Vec3 axis = normalize(way - start);
        const Vec3 side = normalize(cross(axis, Vec3{0.f, 1.f, 0.f}));
        const Vec3 up = cross(side, axis);
        const float a = kTau * hashUnit(sh + 6u);
        const float bow = length(way - start) * (0.12f + 0.25f * hashUnit(sh + 7u));
        const float stray = width * 0.035f * (0.3f + s);
        const Vec3 jitter = Vec3{hashUnit(h + 11u), hashUnit(h + 12u), hashUnit(h + 13u)} - Vec3{0.5f, 0.5f, 0.5f};
        const Vec3 control =
            mixv(start, way, 0.6f) + bow * (std::cos(a) * side + std::sin(a) * up) + (stray * 6.f) * jitter;
        p = bezier(start, control, way, smooth01(s));
        flight = std::sin(kPi * s);
    } else if (t < wayTime + wayHold) {
        p = way;
        flight = 0.4f;
    } else if (t < accent) {
        const float s = (t - wayTime - wayHold) / (accent - wayTime - wayHold);
        const Vec3 d = way - target;
        const Vec3 control = mixv(way, target, 0.55f) + 0.35f * Vec3{-d.y, d.x, 0.15f * width};
        p = bezier(way, control, target, smooth01(s));
        flight = 0.4f * (1.f - s);
    } else {
        const float since = t - accent;
        p = turn(target, 0.05f * std::sin(kTau * since / 7.f), 0.03f * std::sin(kTau * since / 5.3f));
    }

    const float released = std::max(t - release, 0.f);
    if (t > release) {
        const Vec3 jitter = Vec3{hashUnit(h + 7u), hashUnit(h + 8u), hashUnit(h + 9u)} - Vec3{0.5f, 0.5f, 0.5f};
        const Vec3 outward = normalize(Vec3{target.x, 2.2f * target.y, target.z} + Vec3{0.f, 0.f, 0.25f * width} +
                                       0.3f * width * jitter);
        p = p + outward * (width * (0.25f * released + 1.1f * released * released));
    }

    const float shock = t >= accent ? std::exp(-(t - accent) / 0.45f) : 0.f;
    const float rowTime = rowStart + static_cast<float>(6u - std::min(row, 6u)) * bar;
    const float lit = t >= accent ? smooth01((t - rowTime) / 0.4f) : 0.f;
    const float front = smoothstep(1.5f * cell, 2.6f * cell, target.z);
    const Vec3 colour = identityColour(target.x / (0.5f * width), h) * (0.55f + 0.45f * front);
    rt::SphereRecord r{};
    setColor(r.center, p);
    const float far = std::clamp(length(p) / std::max(length(core), 1e-3f), 0.f, 1.f);
    const float grown = smooth01((t - lift) / 0.6f);
    r.radius = kParticleRadius * (cell / kReferenceCell) * std::pow(kFullCount / static_cast<float>(count), 1.f / 3.f) *
               (1.f + 5.f * far) * (1.f + 3.f * released) * (0.25f + 0.75f * grown);
    const bool spark = hashUnit(h + 10u) < 0.05f;
    r.emission = (spark ? 5.f * grown : 0.f) + 1.6f * flight * (1.f + flash) + 1.4f * shock + 3.2f * lit +
                 released * (6.f + 14.f * released);
    setColor(r.color, mixv(colour, Vec3{1.f, 0.86f, 0.66f}, 0.5f * lit));
    return r;
}

// --- Cameras and exposure
// --------------------------------------------------------------------------------------

Camera cosmosCamera(const show::TimelineFrame& frame) {
    return frame.effect == show::Effect::Ascendant ? ascendantCamera(frame) : galaxyCamera(frame);
}

float cosmosExposureEV(const show::TimelineFrame& frame) {
    const auto t = static_cast<float>(frame.localSeconds);
    if (frame.effect != show::Effect::Ascendant) {
        const Bars b = barsOf(frame);
        // Edge-on the light is concentrated in a thin band; face-on it spreads over the frame.
        return 0.5f - 0.7f * smooth01((t - b.at(-4)) / std::max(b.d - b.at(-4), 1e-3f));
    }
    // The release's white-out is post::Look's authored arc; the particles' own light carries the scatter.
    return -0.4f;
}

// --- The scene
// -------------------------------------------------------------------------------------------------

Cosmos::Cosmos(SceneDetail detail)
    : stars_(galaxyStarCount(detail)), particles_(ascendantParticleCount(detail)), dust_(galaxyDustSize(detail)) {
    static_.meshes = {quad(1.f, 1.f)};
    std::vector<rt::Material>& m = static_.materials;
    m.resize(kMaterialCount);
    // The particles: polished metal, coloured per particle (DOTS' identity colours), lit by the sun.
    // Glazed ceramic beads: their own colour lit by the sun under a clear coat that catches its glint.
    setColor(m[kParticles].baseColor, {0.92f, 0.92f, 0.94f});
    m[kParticles].metallic = 0.f;
    m[kParticles].roughness = 0.4f;
    m[kParticles].coat = 1.f;
    m[kParticles].coatRoughness = 0.06f;
    m[kFill] = emitter({0.62f, 0.74f, 1.f}, 1.6f);
    m[kRim] = emitter({1.f, 0.78f, 0.55f}, 20.f);
    static_.capacity.instances = 8;
    static_.capacity.lights = kLightCount;
    static_.capacity.materials = kMaterialCount;
    static_.capacity.particles = particles_;
    static_.capacity.splats = stars_;
    static_.capacity.mediumGrid = dust_;
    static_.particleShader = kCosmosParticleShader;
    static_.splatShader = kCosmosSplatShader;
    static_.mediumShader = kCosmosMediumShader;

    // A cool fill from the left and a warm rim below and behind the logo, both out of every camera's view
    // (the orbit's lowest pitch leaves the rim 20 degrees under the frame's bottom edge).
    struct Box {
        Vec3 position;
        float width, height;
        MaterialIndex material;
        Light light;
    };
    const Box boxes[] = {{{-16.f, 1.5f, 3.f}, 5.f, 4.f, kFill, kFillLight},
                         {{0.f, -14.f, -12.f}, 9.f, 2.f, kRim, kRimLight}};
    for (const Box& box : boxes) {
        const Vec3 aim = normalize(Vec3{0.f, 0.2f, 0.f} - box.position);
        const rt::Mat34 transform =
            compose(translate(box.position),
                    compose(rotationFromTo({0.f, 0.f, 1.f}, aim), scale({box.width, box.height, 1.f})));
        lightInstances_.push_back({0, static_cast<std::uint32_t>(box.material), transform,
                                   static_cast<std::uint32_t>(box.light)});
        const rt::Material& e = m[box.material];
        lights_.push_back(rectLightForQuad(transform, 1.f, 1.f, rt::LightKind::Emitter,
                                           Vec3{e.emission[0], e.emission[1], e.emission[2]} * e.emissionScale, 1.f));
    }
}

SceneFrame Cosmos::frame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    f.camera = cosmosCamera(frame);
    f.exposureEV = cosmosExposureEV(frame);
    f.splats = galaxySplatFrame(frame, stars_);
    const bool ascendant = frame.effect == show::Effect::Ascendant;

    GridMediumDesc dust;
    dust.size = dust_;
    dust.worldToMedium = ascendant ? dustBox(kAscendantGalaxyCentre, kAscendantGalaxyRadius, kAscendantGalaxyTilt,
                                             kAscendantGalaxyYaw)
                                   : dustBox({}, kGalaxyRadius, 0.f, 0.f);
    dust.densityScale = kDustDensityScale * kGalaxyRadius / (ascendant ? kAscendantGalaxyRadius : kGalaxyRadius);
    dust.albedo = {0.f, 0.f, 0.f}; // the dust absorbs (nothing lights it but the stars, which are splats)
    dust.phaseG = 0.f;
    dust.emissionScale = 1.f;
    dust.params = {galaxyTurn(frame), 1.f, kDustHii, 0.f};
    f.medium = MediumFrame{std::nullopt, dust};

    EnvironmentDesc& env = f.environment;
    env = environmentDefaults(EnvironmentKind::Space);
    env.starBrightness = ascendant ? 0.2f : 0.12f; // faint: the galaxy's own stars are the subject
    // Deep space is never quite black: a faint blue-grey of unresolved stars.
    env.zenith = env.horizon = env.ground = Vec3{0.0035f, 0.0045f, 0.008f};
    if (!ascendant) {
        env.select = 0.f;
        return f;
    }
    // The sun from camera-right, a cool fill and a warm rim; the release burns the sky to phosphor white.
    env.discDirection = kSun;
    env.discAngularRadius = 0.3f * kDegrees;
    env.discRadiance = {160000.f, 150000.f, 135000.f};
    env.select = 0.6f;
    const AscendantTimes times = ascendantTimes(frame);
    const float white = smooth01((static_cast<float>(frame.localSeconds) - times.release) /
                                 std::max(static_cast<float>(frame.chapterDuration) - times.release, 0.1f));
    const Vec3 phosphor = env.zenith * (1.f - white) + Vec3{0.86f, 0.93f, 1.f} * (3.f * white * white);
    env.zenith = env.horizon = env.ground = phosphor;
    f.instances = lightInstances_;
    f.lights = lights_;
    static const WaypointTargets waypoint = waypointTargets(kAscendantWaypointText);
    f.particles = ascendantParticleFrame(frame, particles_, &waypoint);
    SceneInstance field;
    field.mesh = 0;
    field.material = kParticles;
    field.transform = identity();
    field.kind = rt::GeometryKind::Spheres;
    field.dynamicSpheres = true;
    field.firstSphere = 0;
    field.sphereCount = particles_;
    f.instances.push_back(field);
    return f;
}

// --- The epoch cards
// ----------------------------------------------------------------------------------------------

std::string Cosmos::nowCard(show::Effect effect) const {
    const std::string year = "2026", stars = groupThousands(kGalaxyStars) + " STARS";
    if (effect == show::Effect::Galaxy)
        return year + kCardDot + stars + kCardDot + std::to_string(kGalaxyDust[0]) + "×" +
               std::to_string(kGalaxyDust[1]) + "×" + std::to_string(kGalaxyDust[2]) + " DUST" + kCardDot +
               "ONE SPLAT PASS";
    if (effect == show::Effect::Ascendant)
        return year + kCardDot + groupThousands(kAscendantParticles) + " PARTICLES" + kCardDot + stars + kCardDot +
               "ASCENDANT";
    return {};
}

} // namespace evo::modern
