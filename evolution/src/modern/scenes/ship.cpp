#include "modern/scenes/ship.hpp"

#include "modern/environment.hpp"
#include "retro/flyby_flight.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evo::modern {

namespace {

constexpr double kPiD = std::numbers::pi;

double smoothD(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

struct DVec3 {
    double x = 0, y = 0, z = 0;
};
DVec3 operator+(DVec3 a, DVec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
DVec3 operator-(DVec3 a, DVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
DVec3 operator*(DVec3 a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
DVec3 mix(DVec3 a, DVec3 b, double t) {
    return a + (b - a) * t;
}
Vec3 single(DVec3 v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

// --- The beat grid
// ------------------------------------------------------------------------------------------

struct Grid {
    show::BeatGridView view;
    double start; // the chapter's start in track seconds
};

Grid gridOf(const show::TimelineFrame& f) {
    return {show::beatGrid(f.track), f.trackSeconds - f.localSeconds};
}

// The chapter's downbeats in chapter seconds.
std::vector<double> downbeats(const Grid& g, double duration) {
    std::vector<double> out;
    for (int b = std::max(g.view.beatAt(g.start), 0); b < g.view.count(); ++b) {
        const double t = g.view.beatTime(b) - g.start;
        if (t > duration)
            break;
        if (t >= -1e-9 && g.view.bar(b) == 0)
            out.push_back(t);
    }
    return out;
}

// Beats since chapter second `from` (a beat of the grid) to `t`, fractional.
double beatsBetween(const Grid& g, double from, double t) {
    const auto at = [&](double local) {
        const int beat = g.view.beatAt(g.start + local);
        if (beat < 0 || beat + 1 >= g.view.count())
            return double(std::max(beat, 0));
        const double a = g.view.beatTime(beat), b = g.view.beatTime(beat + 1);
        return double(beat) + std::clamp((g.start + local - a) / std::max(b - a, 1e-6), 0.0, 1.0);
    };
    return at(t) - at(from);
}

// --- The world
// ------------------------------------------------------------------------------------------------

// The planet below the crossing: an ocean world, the ship 380 m over it as it passes, its centre ahead.
constexpr double kPlanetAltitude = 380.0;
const DVec3 kPlanetCentre{0.0, -double(kShipPlanetRadius) - kPlanetAltitude, 900.0};
// The planet's own frame turned so its pole lies far off the flight (no pole under the camera).
constexpr double kPlanetVisible = 9500.0; // drawn while its centre is nearer than this

// The sun: 0.6 degrees across, a 5800 K white (at full radiance; see frame()).
constexpr float kSunAngularRadius = 0.3f * std::numbers::pi_v<float> / 180.f;
constexpr float kSunRadiance = 48000.f;

// Lenses (36 mm equivalent, vertical fields of view at 16:9): 24 mm and 35 mm.
constexpr float kLens24 = 45.7f, kLens35 = 32.3f;

// The pose: the attitude's rows.
std::array<Vec3, 3> attitudeRows(double local) {
    const retro::FlybyAttitude a = retro::flybyAttitude(local);
    std::array<Vec3, 3> rows;
    for (int i = 0; i < 3; ++i)
        rows[i] = {float(a.m[i][0]), float(a.m[i][1]), float(a.m[i][2])};
    return rows;
}

// R^T d: world to the render frame.
DVec3 toRender(const std::array<Vec3, 3>& r, DVec3 d) {
    return {r[0].x * d.x + r[1].x * d.y + r[2].x * d.z, r[0].y * d.x + r[1].y * d.y + r[2].y * d.z,
            r[0].z * d.x + r[1].z * d.y + r[2].z * d.z};
}

// The rigid transform of a part placed in the world at `centre` with the rotation `world` (rows), carried
// into the render frame: R^T world, R^T (centre - P).
rt::Mat34 inRender(const std::array<Vec3, 3>& r, double shipZ, DVec3 centre,
                   const std::array<Vec3, 3>& world = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
    rt::Mat34 m{};
    for (int j = 0; j < 3; ++j) {
        const DVec3 column{world[0].x * (j == 0) + world[0].y * (j == 1) + world[0].z * (j == 2),
                           world[1].x * (j == 0) + world[1].y * (j == 1) + world[1].z * (j == 2),
                           world[2].x * (j == 0) + world[2].y * (j == 1) + world[2].z * (j == 2)};
        const DVec3 c = toRender(r, column);
        m.m[0][j] = float(c.x), m.m[1][j] = float(c.y), m.m[2][j] = float(c.z);
    }
    const DVec3 t = toRender(r, centre - DVec3{0, 0, shipZ});
    m.m[0][3] = float(t.x), m.m[1][3] = float(t.y), m.m[2][3] = float(t.z);
    return m;
}

// The ring: a torus about z, radius kWormholeRingRadius, tube kWormholeRingTube; uv in metres (u around the
// ring, v around the tube), the seams duplicated.
rt::Mesh ringMesh() {
    constexpr int kAround = 192, kTube = 20;
    rt::Mesh m;
    const float big = kWormholeRingRadius, small = kWormholeRingTube;
    for (int i = 0; i <= kAround; ++i) {
        const float a = 2.f * std::numbers::pi_v<float> * float(i) / float(kAround);
        const Vec3 radial{std::cos(a), std::sin(a), 0.f};
        for (int j = 0; j <= kTube; ++j) {
            const float b = 2.f * std::numbers::pi_v<float> * float(j) / float(kTube);
            const Vec3 n = radial * std::cos(b) + Vec3{0.f, 0.f, std::sin(b)};
            const Vec3 p = radial * big + n * small;
            m.vertices.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, {big * a, small * b}});
        }
    }
    const auto at = [](int i, int j) { return std::uint32_t(i * (kTube + 1) + j); };
    for (int i = 0; i < kAround; ++i)
        for (int j = 0; j < kTube; ++j) {
            const std::uint32_t a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
            m.indices.insert(m.indices.end(), {a, c, b, a, d, c}); // counter-clockwise from outside
        }
    return m;
}

// The wormhole's wall: a tube round the flight path facing its axis, from the gate's mouth (z from
// -kWormholeMouthLength, where it opens as a horn kWormholeMouthRadius wide, narrowing to kWormholeWallRadius at
// the gate) on to kWormholeLength. uv in metres: u round the tube (at the wall's radius), v along it from the
// gate.
float wallRadius(float z) {
    if (z >= 0.f)
        return kWormholeWallRadius;
    const float s = std::clamp(-z / kWormholeMouthLength, 0.f, 1.f);
    return kWormholeWallRadius + (kWormholeMouthRadius - kWormholeWallRadius) * s * s;
}

rt::Mesh wallMesh() {
    constexpr int kAround = 160;
    std::vector<float> rows;
    for (float z = -kWormholeMouthLength; z < 0.f; z += 30.f)
        rows.push_back(z);
    for (float z = 0.f; z < kWormholeLength; z += z < 4000.f ? 100.f : 400.f)
        rows.push_back(z);
    rows.push_back(kWormholeLength);
    rt::Mesh m;
    for (const float z : rows) {
        const float r = wallRadius(z);
        const float dr = (wallRadius(z + 1.f) - wallRadius(z - 1.f)) * 0.5f; // the horn's slope
        for (int i = 0; i <= kAround; ++i) {
            const float a = 2.f * std::numbers::pi_v<float> * float(i) / float(kAround);
            const Vec3 radial{std::cos(a), std::sin(a), 0.f};
            const Vec3 n = normalize(radial * -1.f + Vec3{0.f, 0.f, dr}); // towards the axis
            const Vec3 p = radial * r + Vec3{0.f, 0.f, z};
            m.vertices.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, {kWormholeWallRadius * a, z}});
        }
    }
    const auto at = [](std::size_t row, int i) { return std::uint32_t(row * (kAround + 1) + std::size_t(i)); };
    for (std::size_t row = 0; row + 1 < rows.size(); ++row)
        for (int i = 0; i < kAround; ++i) {
            const std::uint32_t a = at(row, i), b = at(row, i + 1), c = at(row + 1, i + 1), d = at(row + 1, i);
            m.indices.insert(m.indices.end(), {a, b, c, a, c, d}); // facing the axis
        }
    return m;
}

enum SceneMaterial : std::uint32_t { kRing, kPlanet, kWall, kLamp, kSceneMaterials };
enum SceneMesh : std::uint32_t { kRingMesh, kWallMesh, kSceneMeshes };

// A ring's lamps: spheres round its inner face, in the ring's own frame (lights of the light tree).
constexpr float kLampRadius = 1.4f;
constexpr float kLampAt = kWormholeRingRadius - kWormholeRingTube - 0.5f;
constexpr Vec3 kLampColor{1.f, 0.72f, 0.38f};
constexpr float kLampEmission = 90.f;
// How far ahead rings are drawn.
constexpr double kRingReach = 9500.0;
// The lamps' spheres are laid out in kilometres.
constexpr float kLampUnit = 1000.f;
// The wall's glow (its emission scale) before the white-out.
constexpr float kWallGlow = 0.12f;

std::uint32_t scaled(std::uint32_t count, SceneDetail detail) {
    const double n = std::round(double(count) * std::clamp(double(detail.scale), 0.0, 1.0));
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(n));
}

// --- The cameras
// ----------------------------------------------------------------------------------------------

// The white-out's flood, 0 to 1 over the last bars (cubic: it lands on the chapter's end).
float shipFlood(const show::TimelineFrame& f);
double wallShift(const show::TimelineFrame& f);

struct WorldCamera {
    DVec3 eye, target; // relative to the ship's position, world axes
    float fov = kLens24, aperture = 0, focus = 1;
    std::uint32_t shot = 0;
};

// A direction from yaw (about +y, 0 along +z, positive towards +x) and pitch.
DVec3 heading(double yaw, double pitch) {
    return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
}

// How far a follower that starts at rest and reaches `speed` after `ramp` seconds (a smoothstep) has gone.
double follow(double s, double speed, double ramp) {
    if (s <= 0)
        return 0;
    if (s >= ramp)
        return speed * ramp * 0.5 + speed * (s - ramp);
    const double x = s / ramp;
    return speed * ramp * (x * x * x - 0.5 * x * x * x * x);
}

WorldCamera worldCamera(const show::TimelineFrame& f) {
    const ShipTimes t = shipTimes(f);
    const double local = std::clamp(f.localSeconds, 0.0, t.duration);
    const double ship = shipDistance(f);
    WorldCamera c;
    const double v0 = kShipCruise;
    if (local < t.crossing) {
        // The gate (a reveal): a crane up from the planet's limb and in towards the gate, the wormhole's
        // mouth glowing behind it and the sun at its far end; the limb sinks to the lower third as the
        // camera rises, and the gate slides across the mouth.
        const double u = smoothD(local / std::max(t.crossing, 1e-3));
        const double gate = shipGateZ(f);
        const DVec3 eye{mix(DVec3{150.0, -95.0, gate - 2300.0}, DVec3{70.0, 25.0, gate - 1500.0}, u)};
        // Aimed 8 degrees under the gate: the limb (18 to 24 degrees under the eye's level) in the lower
        // third, the gate and the mouth above the centre.
        const DVec3 aim = mix(DVec3{-40.0, -420.0, gate}, DVec3{0.0, -185.0, gate}, u);
        c.eye = eye - DVec3{0.0, 0.0, ship};
        c.target = aim - DVec3{0.0, 0.0, ship};
        c.fov = kLens35;
        c.shot = 0;
        return c;
    }
    if (local < t.tunnel) {
        // The crossing: a camera locked off 22 m beside the path, aimed where the ship will be on the
        // downbeat. The ship comes in from the right at 300 m/s, crosses the picture (the export shutter's
        // motion blur), engines trailing fire, and on the downbeat is at its centre; then the camera pans
        // after it towards the gate and follows it. After the pass the camera gets up to speed behind the
        // ship (it falls about 90 m back), then gains on it until it rides 40 m behind, the planet's limb low
        // in the picture.
        const double s = local - t.pass;
        const DVec3 aim{0.0, 0.0, v0 * t.pass};
        const double w = smoothD((s - 0.05) / 0.7);
        const double lag = s > 0 ? s - follow(s, 1.0, 0.7) : 0.0;
        const double eyeZ =
            v0 * t.pass - 11.0 + v0 * (std::max(s, 0.0) - lag) + 50.0 * smoothD((s - 0.6) / 1.8);
        const DVec3 side = mix(DVec3{22.0, 4.0, 0.0}, DVec3{6.0, 4.5, 0.0}, smoothD((s - 0.2) / 1.8));
        const DVec3 eye{side.x, side.y, eyeZ};
        const DVec3 shipAt{0.0, 0.0, ship};
        const DVec3 look = mix(aim, shipAt, w);
        c.eye = eye - shipAt;
        c.target = look - shipAt + DVec3{0.0, -1.0 * w, 0.0};
        c.fov = kLens24;
        c.shot = 1;
        return c;
    }
    if (local < t.drop) {
        // Into the tunnel: behind and above, falling back as the ship accelerates away.
        const double u = smoothD((local - t.tunnel) / std::max(t.drop - t.tunnel, 1e-3));
        c.eye = DVec3{2.0, 4.6, -24.0 - 16.0 * u};
        c.target = DVec3{0.0, -1.0, 40.0};
        c.fov = kLens24;
        c.shot = 2;
        return c;
    }
    if (local < t.release) {
        // Riding: from D the camera closes in behind the ship, a little above it, opposite the low sun, so
        // the sun's reflection runs along the brushed titanium (the anisotropic streak) while the rings come
        // at them out of the light; from the accent it holds there, drifting slowly round the tail.
        const double w = smoothD((local - t.drop) / std::max(t.accent - t.drop, 0.25));
        const double u = smoothD((local - t.accent) / std::max(t.release - t.accent, 1e-3));
        const DVec3 start{10.0, 9.0, -48.0};
        const double yaw = (206.0 - 22.0 * u) * kPiD / 180.0;
        const DVec3 riding = heading(yaw, 0.27) * 30.0 + DVec3{0.0, 0.6, 0.0};
        c.eye = mix(start, riding, w);
        c.target = DVec3{0.0, -0.8, 5.0 + 2.0 * u};
        c.fov = kLens35;
        c.aperture = 0.06f;
        c.focus = static_cast<float>(std::sqrt(c.eye.x * c.eye.x + c.eye.y * c.eye.y + c.eye.z * c.eye.z));
        c.shot = 3;
        return c;
    }
    // Into the light: the camera lets the ship go; it pulls away into the sun.
    const double u = smoothD((local - t.release) / std::max(t.duration - t.release, 1e-3));
    c.eye = DVec3{-6.0 + 3.0 * u, 4.5 - 1.5 * u, -26.0 - 90.0 * u * u};
    c.target = c.eye + DVec3{0.0, -0.08, 1.0} * 60.0;
    c.fov = kLens35;
    c.shot = 4;
    return c;
}

Camera carry(const WorldCamera& wc, const std::array<Vec3, 3>& rows) {
    Camera c;
    c.eye = single(toRender(rows, wc.eye));
    c.target = single(toRender(rows, wc.target));
    c.up = single(toRender(rows, {0, 1, 0}));
    c.fovYDegrees = wc.fov;
    c.aperture = wc.aperture;
    c.focusDistance = wc.focus;
    c.shot = wc.shot;
    return c;
}

} // namespace

// --- Times and the flight
// -------------------------------------------------------------------------------------

ShipTimes shipTimes(const show::TimelineFrame& f) {
    ShipTimes t;
    t.duration = std::max(f.chapterDuration, 1e-3);
    t.accent = std::clamp(f.accentSeconds, 0.0, t.duration);
    const Grid g = gridOf(f);
    const std::vector<double> bars = downbeats(g, t.duration);
    const auto firstAfter = [&](double at, double otherwise) {
        for (const double b : bars)
            if (b >= at)
                return b;
        return otherwise;
    };
    t.drop = t.accent;
    for (const double b : bars)
        if (b <= t.accent + 1e-6)
            t.drop = b;
    t.pass = std::min(firstAfter(0.21 * t.duration, 0.21 * t.duration), t.drop - 2.0);
    t.crossing = 0.6 * t.pass;
    for (const double b : bars)
        if (b < t.pass - 1.2)
            t.crossing = b;
    t.tunnel = std::clamp(firstAfter(0.3 * t.duration, 0.32 * t.duration), t.pass + 1.0, t.drop - 1.0);
    t.release = t.drop + 0.25 * (t.duration - t.drop);
    int after = 0;
    for (const double b : bars)
        if (b > t.drop + 1e-6 && ++after == 4) {
            t.release = b;
            break;
        }
    t.release = std::min(t.release, t.duration - 1.0);
    t.whiteout =
        std::min(firstAfter(t.release + 1e-6, t.release + 0.4 * (t.duration - t.release)), t.duration - 0.5);
    const int dropBeat = g.view.beatAt(g.start + t.drop + 1e-6);
    if (dropBeat >= 0 && dropBeat + 1 < g.view.count())
        t.beat = std::clamp(g.view.beatTime(dropBeat + 1) - g.view.beatTime(dropBeat), 0.2, 1.0);
    return t;
}

std::vector<ShipShot> shipShots(const show::TimelineFrame& f) {
    const ShipTimes t = shipTimes(f);
    return {{0, t.crossing},
            {t.crossing, t.tunnel},
            {t.tunnel, t.drop},
            {t.drop, t.release},
            {t.release, t.duration}};
}

double shipDistance(const show::TimelineFrame& f) {
    const ShipTimes t = shipTimes(f);
    const double local = std::clamp(f.localSeconds, 0.0, t.duration);
    const double v0 = kShipCruise;
    if (local < t.tunnel)
        return v0 * local;
    // Through the tunnel the ship accelerates (a cubic in time) to one ring per beat at D, covering whole
    // kilometres, so the rings pass on the beats from D on.
    const double z1 = v0 * t.tunnel, span = t.drop - t.tunnel;
    const double v1 = double(kWormholeRingSpacing) / t.beat;
    const double rings = std::max(1.0, std::round(0.5 * (v0 + v1) * span / kWormholeRingSpacing));
    const double dz = rings * kWormholeRingSpacing;
    if (local < t.drop) {
        const double s = (local - t.tunnel) / span, s2 = s * s, s3 = s2 * s;
        return z1 + (s3 - 2 * s2 + s) * span * v0 + (-2 * s3 + 3 * s2) * dz + (s3 - s2) * span * v1;
    }
    return z1 + dz + kWormholeRingSpacing * beatsBetween(gridOf(f), t.drop, local);
}

double shipRingZ(const show::TimelineFrame& f, int k) {
    const ShipTimes t = shipTimes(f);
    show::TimelineFrame atDrop = f;
    atDrop.trackSeconds = f.trackSeconds - f.localSeconds + t.drop;
    atDrop.localSeconds = t.drop;
    return shipDistance(atDrop) + double(k) * kWormholeRingSpacing;
}

ShipPose shipPose(const show::TimelineFrame& f) {
    return {shipDistance(f), attitudeRows(std::clamp(f.localSeconds, 0.0, f.chapterDuration))};
}

Camera shipCamera(const show::TimelineFrame& f) {
    return carry(worldCamera(f), shipPose(f).rows);
}

float shipThrottle(const show::TimelineFrame& f) {
    const ShipTimes t = shipTimes(f);
    const double local = f.localSeconds;
    const float flutter = 0.08f * std::clamp(f.bass, 0.f, 1.f);
    if (local < t.tunnel)
        return 0.7f + flutter;
    const auto flash = [&](double at, double size) {
        return local >= at ? size * std::exp(-(local - at) / 0.25) : 0.0;
    };
    const double burner = 1.0 + flash(t.tunnel, 0.55) + flash(t.drop, 0.45);
    return static_cast<float>(burner) + flutter;
}

float shipExposureEV(const show::TimelineFrame& f) {
    const ShipTimes t = shipTimes(f);
    const double local = f.localSeconds;
    if (local < t.crossing)
        return 0.2f;
    if (local < t.tunnel)
        return 0.2f;
    if (local < t.drop)
        return 0.f;
    return 0.5f;
}

float shipAnamorphic(const show::TimelineFrame& f) {
    const ShipTimes t = shipTimes(f);
    if (f.localSeconds < t.release)
        return 0.f;
    return static_cast<float>(smoothD((f.localSeconds - t.release) / std::max(t.whiteout - t.release, 1e-3)));
}

double shipGateZ(const show::TimelineFrame& f) {
    const ShipTimes t = shipTimes(f);
    show::TimelineFrame atTunnel = f;
    atTunnel.trackSeconds = f.trackSeconds - f.localSeconds + t.tunnel;
    atTunnel.localSeconds = t.tunnel;
    const double ring0 = shipRingZ(f, 0);
    const double gate = std::ceil((shipDistance(atTunnel) + 400.0 - ring0) / kWormholeRingSpacing);
    return ring0 + gate * kWormholeRingSpacing;
}

namespace {

// How far the wall's mesh is moved on from the gate: whole kilometres, so that it starts at least 2 km behind
// the ship (its instance stays near the render frame's origin, and its far end at least 18 km ahead).
double wallShift(const show::TimelineFrame& f) {
    const double behind = shipDistance(f) - shipGateZ(f) - 2000.0;
    return behind > 0.0 ? std::floor(behind / 1000.0) * 1000.0 : 0.0;
}

float shipFlood(const show::TimelineFrame& f) {
    const ShipTimes t = shipTimes(f);
    const double w = smoothD((f.localSeconds - t.whiteout) / std::max(t.duration - t.whiteout, 1e-3));
    return static_cast<float>(w * w * w);
}

} // namespace

ShipStats shipStats(SceneDetail detail) {
    return Ship(detail).stats();
}

// --- The scene
// ------------------------------------------------------------------------------------------------

Ship::Ship(SceneDetail detail) {
    static_.meshes = {ringMesh(), wallMesh()};
    static_.materials.resize(kSceneMaterials);
    // The rings: basalt blocks, gold inlays glowing round their inner faces (seen, not sampled: the lamps
    // between them are the rings' lights).
    rt::Material& ring = static_.materials[kRing];
    ring.baseColor[0] = 0.05f, ring.baseColor[1] = 0.047f, ring.baseColor[2] = 0.044f;
    ring.roughness = 0.45f;
    ring.coat = 0.4f;
    ring.coatRoughness = 0.15f;
    ring.pattern = static_cast<std::uint32_t>(rt::Pattern::WormholeRing);
    ring.patternScale = 23.f;
    ring.emission[0] = 1.f, ring.emission[1] = 0.72f, ring.emission[2] = 0.3f;
    ring.emissionScale = 5.f;
    ring.flags = rt::kMaterialCameraOnlyEmission;
    // The wall: basalt dust under a glacial-blue glow with gold filaments. Its light is not sampled, but it is
    // large and smooth, so the hull's reflections find it (the ship is lit by the tunnel round it).
    rt::Material& wall = static_.materials[kWall];
    wall.baseColor[0] = 0.02f, wall.baseColor[1] = 0.021f, wall.baseColor[2] = 0.024f;
    wall.roughness = 0.65f;
    wall.pattern = static_cast<std::uint32_t>(rt::Pattern::WormholeWall);
    wall.patternScale = kWormholeWallRadius;
    wall.emission[0] = 0.32f, wall.emission[1] = 0.55f, wall.emission[2] = 1.f;
    wall.emissionScale = kWallGlow;
    wall.baseColor2[0] = 1.f, wall.baseColor2[1] = 0.66f, wall.baseColor2[2] = 0.3f;
    // The rings' lamps: their radiance is each sphere's colour times its emission.
    rt::Material& lamp = static_.materials[kLamp];
    lamp.baseColor[0] = lamp.baseColor[1] = lamp.baseColor[2] = 0.05f;
    lamp.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    lamp.emissionScale = 1.f;
    // The planet: ocean under clouds, the air thickening to the limb.
    rt::Material& planet = static_.materials[kPlanet];
    planet.baseColor[0] = 0.012f, planet.baseColor[1] = 0.034f, planet.baseColor[2] = 0.07f;
    planet.roughness = 0.12f;
    planet.baseColor2[0] = 0.8f, planet.baseColor2[1] = 0.82f, planet.baseColor2[2] = 0.85f;
    planet.pattern = static_cast<std::uint32_t>(rt::Pattern::Planet);
    planet.patternScale = 42.f;
    planet.emission[0] = 0.12f, planet.emission[1] = 0.24f, planet.emission[2] = 0.52f;
    planet.emissionScale = 0.6f;
    planet.flags = rt::kMaterialCameraOnlyEmission;
    rt::SphereRecord sphere{};
    sphere.radius = kShipPlanetRadius;
    sphere.color[0] = sphere.color[1] = sphere.color[2] = 1.f;
    sphere.material = kPlanet;
    static_.spheres = {sphere};
    // The lamps of kWormholeRingSlots rings, slot j at z = j km in the first slot's frame.
    for (std::uint32_t j = 0; j < kWormholeRingSlots; ++j)
        for (std::uint32_t k = 0; k < kWormholeRingLights; ++k) {
            const float a = 2.f * std::numbers::pi_v<float> * (float(k) + 0.5f) / float(kWormholeRingLights);
            rt::SphereRecord l{};
            l.center[0] = l.prevCenter[0] = kLampAt * std::cos(a) / kLampUnit;
            l.center[1] = l.prevCenter[1] = kLampAt * std::sin(a) / kLampUnit;
            l.center[2] = l.prevCenter[2] = float(j) * kWormholeRingSpacing / kLampUnit;
            l.radius = kLampRadius / kLampUnit;
            l.color[0] = kLampColor.x, l.color[1] = kLampColor.y, l.color[2] = kLampColor.z;
            l.emission = kLampEmission;
            l.material = kLamp;
            static_.spheres.push_back(l);
        }
    lamps_ = kWormholeRingSlots * kWormholeRingLights;
    static_.sphereRanges = {{0, 1}, {1, lamps_}}; // the planet, the lamps
    static_.capacity.spheres = 1 + lamps_;
    static_.capacity.treeLights = lamps_;

    stars_ = scaled(kShipStars, detail);
    air_ = scaled(kShipAirSplats, detail);
    asset_ = std::make_unique<ShipAsset>(static_, detail, stars_ + air_ + 1);
    static_.capacity.instances = asset_->counts().instances + 40;
    static_.capacity.lights = 2; // the dash light and the tree

    stats_.stars = stars_;
    stats_.airSplats = air_;
    stats_.splats = stars_ + air_ + 1;
    stats_.plumeGrid = asset_->plumeGrid();
    stats_.plumeVoxels = stats_.plumeGrid[0] * stats_.plumeGrid[1] * stats_.plumeGrid[2];
    stats_.triangles = asset_->counts().triangles;
    stats_.lamps = lamps_;
}

std::uint32_t Ship::ringMaterial() const {
    return kRing;
}

std::vector<rt::Material> Ship::materials(const show::TimelineFrame& f) const {
    std::vector<rt::Material> m = static_.materials;
    asset_->animate(m, f, shipThrottle(f));
    const ShipTimes t = shipTimes(f);
    const float kick = std::clamp(f.kick, 0.f, 1.f);
    m[kRing].emissionScale = 5.f * (1.f + 1.4f * kick) * (f.localSeconds >= t.drop ? 1.6f : 1.f);
    // The wall breathes with the kick, and in the white-out its glow floods to white.
    const float flood = shipFlood(f);
    const float glow = kWallGlow * (1.f + 0.35f * kick) * (1.f + 30.f * flood * flood);
    m[kWall].emissionScale = glow;
    m[kWall].coatRoughness = static_cast<float>(wallShift(f)); // the pattern's v offset (pbr.rchit)
    for (int c = 0; c < 3; ++c)
        m[kWall].emission[c] += (1.f - m[kWall].emission[c]) * flood;
    return m;
}

SceneFrame Ship::frame(const show::TimelineFrame& f) const {
    SceneFrame out;
    const ShipPose pose = shipPose(f);
    const WorldCamera wc = worldCamera(f);
    out.camera = carry(wc, pose.rows);
    asset_->place(out, identity());
    out.materials = materials(f);
    const float flood = shipFlood(f);
    GridMediumDesc plumes = asset_->plumes(shipThrottle(f), identity());
    plumes.emissionScale *= 1.f - flood; // the fire fades into the white, not to pink
    out.medium = MediumFrame{std::nullopt, plumes};
    out.exposureEV = shipExposureEV(f);
    out.anamorphic = shipAnamorphic(f);
    // The plumes are lights nothing samples: a hull path that happens on their white-hot core would sparkle,
    // so contributions after a rough vertex are held lower than the default.
    out.clampIndirect = 1.5f;

    // The wormhole's wall, from the gate's mouth on (moved on in whole kilometres as the ship flies, its
    // pattern carried with it: wallShift).
    const double gate = shipGateZ(f);
    out.instances.push_back({kWallMesh, kWall, inRender(pose.rows, pose.z, {0.0, 0.0, gate + wallShift(f)})});

    // The rings from the gate on, from just behind the ship: kWormholeRingSlots of them, their lamps the
    // light tree's lights.
    const double ring0 = shipRingZ(f, 0);
    const int gateRing = int(std::lround((gate - ring0) / kWormholeRingSpacing));
    const int first = std::max(gateRing, int(std::floor((pose.z - 600.0 - ring0) / kWormholeRingSpacing)));
    // (Rings beyond kRingReach are not drawn, nor their lamps lit: too far to tell, and the render frame
    // stays within 10 km.)
    for (int k = first; k < first + int(kWormholeRingSlots); ++k) {
        const double z = ring0 + double(k) * kWormholeRingSpacing;
        if (z - pose.z <= kRingReach)
            out.instances.push_back({kRingMesh, kRing, inRender(pose.rows, pose.z, {0.0, 0.0, z})});
    }
    const auto treeLight = static_cast<std::uint32_t>(out.lights.size());
    rt::RectLight entry{};
    entry.kind = static_cast<std::uint32_t>(rt::LightKind::Tree);
    entry.selectWeight = 1.f;
    entry.gate = rt::kNoLight;
    out.lights.push_back(entry);
    SceneInstance tree{0, kLamp,
                       inRender(pose.rows, pose.z, {0.0, 0.0, ring0 + double(first) * kWormholeRingSpacing})};
    // The spheres are laid out in kilometres (a uniform scale of 1000, so that the scale's check is not lost
    // in the float rounding of a translation kilometres long), in a frame made exactly orthonormal (the 1993
    // attitude is so only to a few parts in 10^4).
    {
        auto column = [&](int j) {
            return Vec3{tree.transform.m[0][j], tree.transform.m[1][j], tree.transform.m[2][j]};
        };
        const Vec3 x = normalize(column(0));
        const Vec3 y = normalize(column(1) - x * dot(x, column(1)));
        const Vec3 z = cross(x, y);
        for (int j = 0; j < 3; ++j) {
            const Vec3 c = j == 0 ? x : j == 1 ? y : z;
            tree.transform.m[0][j] = c.x * kLampUnit, tree.transform.m[1][j] = c.y * kLampUnit,
            tree.transform.m[2][j] = c.z * kLampUnit;
        }
    }
    tree.kind = rt::GeometryKind::Spheres;
    tree.firstSphere = 1;
    tree.sphereCount = lamps_;
    tree.light = treeLight;
    out.instances.push_back(tree);
    out.treeLights = treeLightsOf(static_, tree, out.materials, lamps_);
    const ShipTimes t = shipTimes(f);
    const float kick = std::clamp(f.kick, 0.f, 1.f);
    const float lampScale = (1.f + 0.6f * kick) * (f.localSeconds >= t.drop ? 1.3f : 1.f) * (1.f - flood);
    for (std::uint32_t k = 0; k < lamps_; ++k) {
        const double z = ring0 + double(first + int(k / kWormholeRingLights)) * kWormholeRingSpacing;
        out.treeLights[k].scale = z - pose.z <= kRingReach ? lampScale : 0.f;
    }

    // The planet while it is near: its own frame turned (local y to world x) so no pole lies under the
    // flight.
    const DVec3 toPlanet = kPlanetCentre - DVec3{0, 0, pose.z};
    const double planetDistance =
        std::sqrt(toPlanet.x * toPlanet.x + toPlanet.y * toPlanet.y + toPlanet.z * toPlanet.z);
    const bool planet = planetDistance < kPlanetVisible;
    if (planet) {
        SceneInstance in{
            0, kPlanet,
            inRender(pose.rows, pose.z, kPlanetCentre, {Vec3{0, 1, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}})};
        in.kind = rt::GeometryKind::Spheres;
        in.firstSphere = 0;
        in.sphereCount = 1;
        out.instances.push_back(in);
    }

    // The sky: a 5800 K sun disc at the wormhole's far end (the key light), the stars as splats fixed to the
    // world (the wall hides them inside).
    const Vec3 sun = normalize(
        single(toRender(pose.rows, {kShipSunDirection.x, kShipSunDirection.y, kShipSunDirection.z})));
    EnvironmentDesc env = environmentDefaults(EnvironmentKind::Space);
    env.discDirection = sun;
    env.discAngularRadius = kSunAngularRadius;
    const Vec3 white = shipBlackbody(5800.f);
    // Seen down the wormhole the sun is a quarter of its full radiance (the tube's light and the rings' are
    // the picture's); in the last shot, as the ship heads into it, it comes up to full.
    env.discRadiance = white / std::max({white.x, white.y, white.z}) * kSunRadiance * (0.25f + 0.75f * out.anamorphic);
    env.starDensity = 0.f;
    // A faint glacial-blue glow all round; in the white-out it floods to white.
    const Vec3 fill{0.0015f, 0.0024f, 0.0045f};
    env.zenith = env.horizon = env.ground = fill + (Vec3{2.4f, 2.3f, 2.15f} - fill) * flood;
    env.select = 0.9f; // the rest samples the cockpit's dash light and the rings' lamps
    out.environment = env;

    ShipSky sky;
    sky.stars = stars_;
    sky.air = planet ? air_ : 0;
    sky.eye = out.camera.eye;
    std::array<Vec3, 3> renderRows; // R^T as rows: the columns of R
    renderRows[0] = {pose.rows[0].x, pose.rows[1].x, pose.rows[2].x};
    renderRows[1] = {pose.rows[0].y, pose.rows[1].y, pose.rows[2].y};
    renderRows[2] = {pose.rows[0].z, pose.rows[1].z, pose.rows[2].z};
    sky.worldToRender = quaternionOfRows(renderRows);
    sky.sun = sun;
    sky.planetCentre = single(toRender(pose.rows, toPlanet));
    sky.planetRadius = kShipPlanetRadius;
    out.splats = shipSplats(sky);
    return out;
}

// --- The epoch cards
// ----------------------------------------------------------------------------------------------

std::string Ship::nowCard(show::Effect effect) const {
    if (effect != show::Effect::Wormhole)
        return {};
    const auto km = static_cast<std::uint64_t>(std::lround(kWormholeRingSpacing / 1000.f));
    return std::string("2026") + kCardDot + "WORMHOLE" + kCardDot + groupThousands(km) + " KM RINGS" + kCardDot +
           "VOLUMETRIC" + kCardDot + "MOTION BLUR";
}

} // namespace evo::modern
