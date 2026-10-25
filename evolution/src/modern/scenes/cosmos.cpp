#include "modern/scenes/cosmos.hpp"

#include "modern/mesh_builder.hpp"
#include "modern/scenes/logo_targets.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace evo::modern {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTau = 6.28318531f;
constexpr float kDegrees = kPi / 180.f;

// --- galaxy.glsl's dust on the CPU
// -------------------------------------------------------------------------------

constexpr float kGalaxyPitch = 0.26f, kGalaxyArmStart = 0.07f, kGalaxyLaneOffset = 0.3f;
constexpr float kDustDensityScale = 0.7f; // sigma_t per world unit of a dust value of 1 (GALAXY's radius)
constexpr float kDustHii = 0.45f;          // the HII knots' emission

float galaxyArmPhase(float radius, float azimuth) {
    const float spine = std::log(std::max(radius, 1e-4f) / kGalaxyArmStart) / kGalaxyPitch;
    const float a = azimuth - spine;
    return a - kPi * std::floor(a / kPi + 0.5f);
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

Vec3 identityColour(std::uint32_t h) {
    const float u = logoUnit(h);
    return u < 0.35f ? Vec3{1.f, 0.42f, 0.12f} : u < 0.8f ? Vec3{0.95f, 0.9f, 0.82f} : Vec3{0.55f, 0.78f, 1.f};
}

void setColor(float out[3], Vec3 c) {
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
}

// --- The beat grid
// --------------------------------------------------------------------------------------------

// The chapter's downbeats, in chapter seconds.
std::vector<float> downbeats(const show::TimelineFrame& f) {
    const show::BeatGridView grid = show::beatGrid(f.track);
    const double trackToLocal = (f.showSeconds - f.trackSeconds) - (f.showSeconds - f.localSeconds);
    std::vector<float> out;
    for (int i = 0; i < grid.count(); ++i) {
        if (grid.bar(i) != 0)
            continue;
        const double local = grid.beatTime(i) + trackToLocal;
        if (local >= -0.05 && local <= f.chapterDuration + 0.05)
            out.push_back(static_cast<float>(local));
    }
    return out;
}

struct Bars {
    float d = 0, bar = 1.74f; // the downbeat at or before the accent, and the bar's length
    std::vector<float> downbeats;
    // The downbeat k bars from D (negative: before), extrapolated past the grid.
    float at(int k) const {
        const auto found = std::find(downbeats.begin(), downbeats.end(), d);
        const long i = (found - downbeats.begin()) + k;
        if (found != downbeats.end() && i >= 0 && i < static_cast<long>(downbeats.size()))
            return downbeats[static_cast<std::size_t>(i)];
        return d + static_cast<float>(k) * bar;
    }
};

Bars barsOf(const show::TimelineFrame& f) {
    Bars b;
    b.downbeats = downbeats(f);
    const auto accent = static_cast<float>(f.accentSeconds);
    b.d = accent;
    for (const float t : b.downbeats)
        if (t <= accent + 0.02f)
            b.d = t;
    if (b.downbeats.size() >= 2)
        b.bar = (b.downbeats.back() - b.downbeats.front()) / static_cast<float>(b.downbeats.size() - 1);
    return b;
}

// --- Cameras
// -------------------------------------------------------------------------------------------------

struct Key {
    Vec3 eye, target, up;
};

Vec3 catmullRom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.f * p1) + (p2 - p0) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                   (3.f * p1 - p0 - 3.f * p2 + p3) * t3);
}

Key alongKeys(std::span<const Key> keys, float s) {
    const int n = static_cast<int>(keys.size());
    const float x = std::clamp(s, 0.f, 1.f) * static_cast<float>(n - 1);
    const int i = std::min(static_cast<int>(x), n - 2);
    const float t = x - static_cast<float>(i);
    const auto at = [&](int k) { return keys[static_cast<std::size_t>(std::clamp(k, 0, n - 1))]; };
    return {catmullRom(at(i - 1).eye, at(i).eye, at(i + 1).eye, at(i + 2).eye, t),
            catmullRom(at(i - 1).target, at(i).target, at(i + 1).target, at(i + 2).target, t),
            normalize(catmullRom(at(i - 1).up, at(i).up, at(i + 1).up, at(i + 2).up, t))};
}

// A camera on the sphere of `distance` about `centre` at azimuth and elevation (degrees; azimuth 0 on +z,
// towards +x), looking at the centre, its up the direction of rising elevation.
Key orbit(Vec3 centre, float azimuth, float elevation, float distance) {
    const float a = azimuth * kDegrees, e = elevation * kDegrees;
    const Vec3 out{std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
    const Vec3 up{-std::sin(e) * std::sin(a), std::cos(e), -std::sin(e) * std::cos(a)};
    return {centre + out * distance, centre, up};
}

// Focal lengths (36 mm equivalent) as vertical fields of view at 16:9.
constexpr float kLens85 = 13.7f, kLens50 = 23.4f, kLens35 = 31.8f;

Camera makeCamera(const Key& key, float fov, std::uint32_t shot, float aperture = 0.f, float focus = 0.f) {
    Camera c;
    c.eye = key.eye;
    c.target = key.target;
    c.up = key.up;
    c.fovYDegrees = fov;
    c.aperture = aperture;
    c.shot = shot;
    c.focusDistance = focus > 0.f ? focus : std::max(length(key.target - key.eye), 0.1f);
    return c;
}

// GALAXY: shot 0 the 85 mm edge-on approach; from the bar line four bars before D, shot 1 on 35 mm: the swing
// above the plane to face-on at D, the hold through the accent, and from four bars after D the dive into an
// arm.
Camera galaxyCamera(const show::TimelineFrame& f) {
    const Bars b = barsOf(f);
    const auto t = static_cast<float>(f.localSeconds);
    const float cut = b.at(-4), dive = b.at(4);
    if (t < cut) {
        const float s = smooth01(t / std::max(cut, 1e-3f));
        const Key keys[] = {orbit({}, 8.f, 1.6f, 470.f), orbit({}, 5.f, 1.5f, 400.f), orbit({}, 2.f, 1.4f, 290.f)};
        return makeCamera(alongKeys(keys, s), kLens85, 0);
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
    const float phase = galaxyArmPhase(radius, azimuth);
    const float vertical = std::exp(-std::abs(p.y) / 0.011f);
    const float envelope = smoothstep(0.05f, 0.16f, radius) * (1.f - smoothstep(0.82f, 1.05f, radius));
    const float laneWidth = 0.1f + 0.05f * radius;
    const float lane = std::exp(-std::pow((phase + kGalaxyLaneOffset) / laneWidth, 2.f)) +
                       0.6f * std::exp(-std::pow((phase + 0.55f) / (1.5f * laneWidth), 2.f));
    const float n = 0.65f * noise(p * 22.f) + 0.35f * noise(p * 61.f + Vec3{17.f, 17.f, 17.f});
    const float filaments = smoothstep(0.25f, 0.75f, n);
    const float disc = 0.22f * std::exp(-radius / 0.4f) * smoothstep(0.02f, 0.08f, radius);
    const float density = vertical * envelope * (2.6f * lane * filaments + disc * (0.5f + filaments));
    const float spine = std::exp(-std::pow((phase - 0.03f) / 0.07f, 2.f));
    const float knot = std::pow(noise(p * 48.f + Vec3{5.f, 5.f, 5.f}), 5.f) * spine * envelope *
                       std::exp(-std::abs(p.y) / 0.008f);
    return {density + 2.f * knot, 14.f * knot};
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
    t.firstRow = b.at(4);
    t.bar = b.bar;
    float last = b.d;
    for (const float d : b.downbeats)
        if (d <= static_cast<float>(frame.chapterDuration) - 0.25f)
            last = d;
    t.release = last;
    t.liftStart = b.at(-4);
    t.liftSpread = std::max(t.waypoint - 1.2f - t.liftStart, 0.5f);
    return t;
}

ParticleFrame ascendantParticleFrame(const show::TimelineFrame& frame, std::uint32_t count) {
    const AscendantTimes t = ascendantTimes(frame);
    ParticleFrame p;
    p.count = count;
    p.params = {static_cast<float>(count),
                t.accent,
                t.liftStart,
                t.liftSpread,
                t.waypoint,
                t.hold,
                0.f, // the waypoint formation: the vortex
                t.release,
                t.firstRow,
                t.bar,
                std::clamp(frame.kick, 0.f, 1.f),
                kLogoCell,
                kAscendantGalaxyCentre.x,
                kAscendantGalaxyCentre.y,
                kAscendantGalaxyCentre.z,
                2.5f};
    p.material = Cosmos::kParticles;
    return p;
}

Vec3 ascendantTarget(std::uint32_t id, float cell) {
    return logoTarget(id, cell);
}

Vec3 ascendantWaypoint(std::uint32_t /*formation*/, std::uint32_t id, float cell) {
    const float width = 71.f * cell;
    const float a = kTau * golden(id) + 2.5f * hashUnit(id ^ 0x77AAu);
    const float r = width * (0.12f + 0.42f * std::sqrt(hashUnit(id ^ 0x1F3Du)));
    const float wobble = 0.06f * width * (hashUnit(id ^ 0x5151u) - 0.5f);
    return {r * std::cos(a), 0.55f * r * std::sin(a) + 0.12f * width + wobble,
            -0.55f * width + 0.25f * r * std::sin(a)};
}

rt::SphereRecord ascendantParticle(std::uint32_t id, float t, const std::array<float, 16>& params) {
    const auto count = static_cast<std::uint32_t>(params[0]);
    const float accent = params[1], liftStart = params[2], liftSpread = params[3];
    const float wayTime = params[4], wayHold = params[5], release = params[7];
    const auto formation = static_cast<std::uint32_t>(params[6]);
    const float rowStart = params[8], bar = params[9], flash = params[10], cell = params[11];
    const Vec3 core{params[12], params[13], params[14]};
    const float coreRadius = params[15];

    const LogoPixel pixel = logoPixel((id % (kLogoPixels * kLogoDepth)) % kLogoPixels);
    const std::uint32_t row = pixel.row;
    const Vec3 target = logoTarget(id, cell);
    const std::uint32_t h = logoHash(id ^ 0x41534344u);
    const float g1 = std::sqrt(-2.f * std::log(std::max(hashUnit(h + 1u), 1e-7f)));
    Vec3 ball{std::cos(kTau * hashUnit(h + 2u)) * g1, std::sin(kTau * hashUnit(h + 2u)) * g1, 0.f};
    ball.z = std::sqrt(-2.f * std::log(std::max(hashUnit(h + 3u), 1e-7f))) * std::cos(kTau * hashUnit(h + 4u));
    const Vec3 start = core + coreRadius * Vec3{ball.x, 0.45f * ball.y, ball.z};
    const float lift = liftStart + liftSpread * hashUnit(h + 5u);
    const Vec3 way = ascendantWaypoint(formation, id, cell);
    const float width = 71.f * cell;

    Vec3 p;
    float flight = 0.f, inCore = 0.f;
    if (t <= lift) {
        p = start;
        inCore = 1.f;
    } else if (t < wayTime) {
        const float s = (t - lift) / (wayTime - lift);
        const Vec3 axis = normalize(way - start);
        const Vec3 side = normalize(cross(axis, Vec3{0.f, 1.f, 0.f}));
        const Vec3 up = cross(side, axis);
        const float a = kTau * golden(id * 7u + 3u);
        const float bow = length(way - start) * (0.08f + 0.22f * hashUnit(h + 6u));
        const Vec3 control = mixv(start, way, 0.6f) + bow * (std::cos(a) * side + std::sin(a) * up);
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
    const Vec3 colour = identityColour(h);
    rt::SphereRecord r{};
    setColor(r.center, p);
    const float far = std::clamp(length(p) / std::max(length(core), 1e-3f), 0.f, 1.f);
    r.radius = kParticleRadius * (cell / kReferenceCell) * std::pow(kFullCount / static_cast<float>(count), 1.f / 3.f) *
               (1.f + 5.f * far) * (1.f + 3.f * released);
    const bool spark = hashUnit(h + 10u) < 0.05f;
    r.emission = (spark ? 5.f : 0.f) + 0.3f * inCore + 1.6f * flight * (1.f + flash) + 1.4f * shock + 3.2f * lit +
                 released * (8.f + 60.f * released);
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
    const AscendantTimes a = ascendantTimes(frame);
    // The release burns to white over the last second.
    return -0.4f + 5.f * smooth01((t - a.release) / std::max(static_cast<float>(frame.chapterDuration) - a.release, 0.1f));
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
    m[kRim] = emitter({1.f, 0.78f, 0.55f}, 14.f);
    static_.capacity.instances = 8;
    static_.capacity.lights = kLightCount;
    static_.capacity.materials = kMaterialCount;
    static_.capacity.particles = particles_;
    static_.capacity.splats = stars_;
    static_.capacity.mediumGrid = dust_;
    static_.particleShader = kCosmosParticleShader;
    static_.splatShader = kCosmosSplatShader;
    static_.mediumShader = kCosmosMediumShader;

    // A cool fill from the left and a warm rim above and behind the logo, both out of every camera's view.
    struct Box {
        Vec3 position;
        float width, height;
        MaterialIndex material;
        Light light;
    };
    const Box boxes[] = {{{-16.f, 1.5f, 3.f}, 5.f, 4.f, kFill, kFillLight},
                         {{0.f, -6.f, -8.f}, 7.f, 1.5f, kRim, kRimLight}};
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
    env.starBrightness = ascendant ? 0.35f : 0.5f;
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
    f.particles = ascendantParticleFrame(frame, particles_);
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

} // namespace evo::modern
