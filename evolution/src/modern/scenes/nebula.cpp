#include "modern/scenes/nebula.hpp"

#include "modern/environment.hpp"
#include "modern/mesh_builder.hpp"
#include "modern/scenes/shots.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <span>

namespace evo::modern {

namespace {

constexpr float kPi = 3.14159265f;
// 24 mm and 35 mm (36 mm-equivalent, horizontal 73 and 54 degrees) at 16:9, as vertical fields of view.
constexpr float kFov24 = 45.19f;
constexpr float kFov35 = 32.0f;
// PLASMA's grey albedo (the shader's kPlasmaAlbedo).
constexpr float kPlasmaAlbedo = 0.55f;
// NEBULA's ionising opacity per unit of the bake's density.
constexpr float kNebulaOpacity = 1.6f;
// NEBULA's density scale (sigma_t per world unit inside a pillar).
constexpr float kNebulaDensityScale = 1.1f;

float smooth01(float x) {
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

void setColor(float out[3], Vec3 c) {
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
}

Vec3 colorOf(const float (&c)[3]) {
    return {c[0], c[1], c[2]};
}

// --- The shaders' hashes (nebula_stars.comp) -----------------------------------------------------------------

std::uint32_t starHash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float starUnit(std::uint32_t x) {
    return static_cast<float>(x >> 8) * (1.f / 16777216.f);
}

constexpr std::array<Vec3, kNebulaStars> kHeroColour{{{0.72f, 0.82f, 1.f}, {1.f, 0.93f, 0.84f}, {1.f, 0.74f, 0.48f}}};
constexpr std::array<float, kNebulaStars> kHeroEmission{{2800.f, 600.f, 800.f}};

// Pillar i's bent axis and its radius at height fraction h (the bake shader's pillarAxis and pillarRadius).
Vec3 pillarAxis(std::uint32_t i, float h) {
    const NebulaPillar& pillar = kNebulaPillars[i];
    const auto fi = static_cast<float>(i);
    return pillar.base + (pillar.top - pillar.base) * h + pillar.bow * std::sin(kPi * h) +
           Vec3{1.6f * std::sin(5.1f * h + 1.7f * fi), 0.f, 1.6f * std::cos(4.3f * h + 2.3f * fi)};
}

float pillarRadius(std::uint32_t i, float h) {
    const NebulaPillar& pillar = kNebulaPillars[i];
    const float head = (h - 0.9f) / 0.08f;
    return (pillar.baseRadius + (pillar.topRadius - pillar.baseRadius) * h) *
               (1.f + 0.16f * std::sin(8.7f * h + 1.3f * static_cast<float>(i))) +
           0.45f * pillar.topRadius * std::exp(-head * head);
}

// --- Time: the beat grid ---------------------------------------------------------------------------------------

// The chapter's start in its track's seconds.
double trackStart(const show::TimelineFrame& f) {
    return f.trackSeconds - f.localSeconds;
}

double chapterEnd(const show::TimelineFrame& f) {
    return std::max(f.chapterDuration, f.accentSeconds + 1e-3);
}

// The downbeat at or before the accent (chapter seconds) and the bar's length; the accent itself and 2 s
// when the grid has none inside the chapter.
std::pair<double, double> downbeatAtAccent(const show::TimelineFrame& f) {
    const show::BeatGridView grid = show::beatGrid(f.track);
    const double start = trackStart(f);
    int beat = grid.beatAt(start + f.accentSeconds);
    if (beat < 0 || grid.beatTime(beat) < start - 1e-6)
        return {f.accentSeconds, 2.0};
    while (beat > 0 && grid.bar(beat) != 0 && grid.beatTime(beat - 1) >= start - 1e-6)
        --beat;
    const double at = grid.beatTime(beat);
    const double next = beat + 1 < grid.count() ? grid.beatTime(beat + 1) : at + 0.5;
    return {at - start, 4.0 * std::max(next - at, 1e-3)};
}

// The bar line `bars` bars before chapter time `seconds` (a downbeat of the grid; seconds - bars * barSeconds
// when the grid runs out), never before the chapter's start.
double barLinesBefore(const show::TimelineFrame& f, double seconds, int bars, double barSeconds) {
    const show::BeatGridView grid = show::beatGrid(f.track);
    const double start = trackStart(f);
    int beat = grid.beatAt(start + seconds + 1e-6);
    int found = 0;
    for (; beat >= 0 && grid.beatTime(beat) >= start - 1e-6; --beat)
        if (grid.bar(beat) == 0 && grid.beatTime(beat) < start + seconds - 1e-6 && ++found == bars)
            return grid.beatTime(beat) - start;
    return std::max(seconds - bars * barSeconds, 0.0);
}

// --- Camera paths ----------------------------------------------------------------------------------------------

struct Key {
    double time;
    Vec3 value;
};

// A C1 cubic through keys at their times (Catmull-Rom tangents for uneven spacing; a key's tangent is 0 where
// `hold` says so, so a move can start from rest). Clamped to the first and last keys.
Vec3 throughKeys(std::span<const Key> keys, double t, std::initializer_list<std::size_t> holds = {}) {
    const std::size_t n = keys.size();
    if (t <= keys.front().time)
        return keys.front().value;
    if (t >= keys.back().time)
        return keys.back().value;
    std::size_t i = 0;
    while (i + 2 < n && t >= keys[i + 1].time)
        ++i;
    const auto tangent = [&](std::size_t k) -> Vec3 {
        for (const std::size_t h : holds)
            if (h == k)
                return {};
        if (k == 0 || k + 1 == n)
            return {};
        return (keys[k + 1].value - keys[k - 1].value) / static_cast<float>(keys[k + 1].time - keys[k - 1].time);
    };
    const auto h = static_cast<float>(keys[i + 1].time - keys[i].time);
    const auto s = static_cast<float>((t - keys[i].time) / (keys[i + 1].time - keys[i].time));
    const float s2 = s * s, s3 = s2 * s;
    return (2.f * s3 - 3.f * s2 + 1.f) * keys[i].value + (s3 - 2.f * s2 + s) * h * tangent(i) +
           (-2.f * s3 + 3.f * s2) * keys[i + 1].value + (s3 - s2) * h * tangent(i + 1);
}

Camera lookAt(Vec3 eye, Vec3 target, float fov, std::uint32_t shot) {
    Camera c;
    c.eye = eye;
    c.target = target;
    c.up = {0.f, 1.f, 0.f};
    c.fovYDegrees = fov;
    c.aperture = 0.f;
    c.focusDistance = std::max(length(target - eye), 0.1f);
    c.shot = shot;
    return c;
}

Camera nebulaFlight(const show::TimelineFrame& f) {
    const NebulaTimes times = nebulaTimes(f);
    const double t = std::clamp(f.localSeconds, 0.0, chapterEnd(f));
    const double end = chapterEnd(f);
    if (t < times.approachEnd) {
        // Shot 0: a slow drift towards the wall from outside the cloud.
        const float u = smooth01(static_cast<float>(t / std::max(times.approachEnd, 1e-3)));
        const Vec3 eye = Vec3{-4.f, 15.f, 196.f} + (Vec3{-14.f, 7.f, 136.f} - Vec3{-4.f, 15.f, 196.f}) * u;
        const Vec3 target = Vec3{-6.f, 12.f, -40.f} + (Vec3{-12.f, 4.f, -40.f} - Vec3{-6.f, 12.f, -40.f}) * u;
        return lookAt(eye, target, kFov24, 0);
    }
    // Shot 1: into the gap between the left pillars, the reveal of star A round the centre pillar, then a
    // slow swing round A.
    const double a = times.approachEnd, d = times.downbeat, accent = std::max(f.accentSeconds, d + 0.2);
    const double after = std::min(accent + 3.0, end - 2.0);
    // After the reveal the push settles and the camera arcs back and to the left round A, holding A on the
    // left and the centre pillar's lit edge on the right.
    const Key eyes[] = {{a, {-19.f, 7.f, 72.f}},
                        {d - 0.45 * (d - a), {-20.f, 4.f, 12.f}},
                        {d, {-21.f, 2.f, -20.f}},
                        {accent, {-21.5f, 2.5f, -29.f}},
                        {after, {-26.5f, 3.5f, -27.f}},
                        {after + 0.5 * (end - after), {-32.f, 5.f, -21.f}},
                        {end, {-35.5f, 7.f, -15.f}}};
    const Vec3 hero = kNebulaStarCentres[0];
    const Key targets[] = {{a, {-27.f, 1.f, -40.f}},
                           {d - 0.45 * (d - a), {-30.f, 0.f, -60.f}},
                           {d, {-14.f, 4.f, -80.f}},
                           {accent, hero + Vec3{-6.f, -1.f, 4.f}},
                           {after, hero + Vec3{-8.f, -5.f, 18.f}},
                           {end, hero + Vec3{-9.f, -6.f, 22.f}}};
    const Vec3 eye = throughKeys(eyes, t);
    const Vec3 target = throughKeys(targets, t);
    const float fov = kFov24 + (kFov35 - kFov24) * smooth01(static_cast<float>((t - d) / std::max(end - d, 1e-3)));
    return lookAt(eye, target, fov, 1);
}

// PLASMA: the slab's mid-plane fills the 1993 screen's 4:3 picture exactly from kPlasmaEye, held until the
// wipe starts (two bars before the downbeat at the accent); a slow push during the wipe, then the dolly into
// the cloud, crossing its face on the accent and easing out inside.
// Half the picture's height (100 pixels of kPlasmaPixel * 1.2) over tan(24 mm's half vertical field of view).
constexpr float kPlasmaEyeZ = 100.f * kPlasmaPixel * 1.2f / 0.41626f;

Camera plasmaCamera(const show::TimelineFrame& f) {
    const auto [downbeat, bar] = downbeatAtAccent(f);
    const double end = chapterEnd(f);
    const double wipe = std::max(downbeat - 2.0 * bar, 0.0);
    const double accent = std::max(f.accentSeconds, downbeat + 0.2);
    const Key eyes[] = {{0.0, {0.f, 0.f, kPlasmaEyeZ}},
                        {wipe, {0.f, 0.f, kPlasmaEyeZ}},
                        {downbeat, {0.1f, 0.05f, kPlasmaEyeZ - 3.3f}},
                        {accent, {0.6f, 0.3f, 7.5f}},
                        {end, {1.6f, 0.8f, 1.f}}};
    const double t = std::clamp(f.localSeconds, 0.0, end);
    const Vec3 eye = throughKeys(eyes, t, {1});
    // Inside, the camera turns slowly to look along the slab instead of through it, so the strands recede
    // in layers to the end of the shot.
    const float turn = smooth01(static_cast<float>((t - accent) / std::max(end - accent, 1e-3)));
    const Vec3 look = normalize(Vec3{-0.02f * smooth01(static_cast<float>((t - downbeat) / (end - downbeat))) + 0.7f * turn,
                                     0.06f * turn, -1.f});
    return lookAt(eye, eye + 10.f * look, kFov24, 0);
}

// --- PLASMA PLANET ----------------------------------------------------------------------------------------

constexpr float kFov50 = 22.9f; // 50 mm at 16:9, vertical

// The terminator's point under the sunrise, the eye on D (behind it along the sun's line: the sun grazes the
// limb there), and the directions of the crane: up away from the planet, across the sun's line.
struct PlanetFrame {
    Vec3 sun, up, side, limb, sunriseEye;
};

PlanetFrame planetFrame() {
    PlanetFrame p;
    p.sun = plasmaPlanetSun();
    p.up = normalize(Vec3{0.f, 1.f, 0.f} - p.sun * p.sun.y);
    p.side = normalize(cross(p.sun, p.up));
    // The sunrise point: a little off the top of the terminator, so the limb runs across the frame on a slant.
    p.limb = normalize(p.up + 0.22f * p.side) * kPlasmaPlanetRadius;
    p.sunriseEye = p.limb - p.sun * 1500.f;
    return p;
}

// A point at `altitude` over the sea in direction d from the centre.
Vec3 overSea(Vec3 d, float altitude) {
    return normalize(d) * (kPlasmaPlanetRadius + altitude);
}

// The low pass: along the great circle through the sea under (0, 1, 0), heading towards the low sun, at
// cloud height; u in [0, 1] over the shot.
Vec3 lowPassHeading() {
    return normalize(Vec3{0.95f, 0.f, -0.31f});
}

Camera planetCamera(const show::TimelineFrame& f) {
    const PlasmaPlanetTimes times = plasmaPlanetTimes(f);
    const PlanetFrame p = planetFrame();
    const double end = chapterEnd(f);
    const double t = std::clamp(f.localSeconds, 0.0, end);
    const double d = times.sunrise;
    if (t < times.craneStart) {
        // Shot 0: high over the night side, drifting along the terminator; the sea's glow and the thin air of
        // the limb, the sun still far behind the planet.
        const float u = smooth01(static_cast<float>(t / std::max(times.craneStart, 1e-3)));
        const Vec3 eye = -p.sun * (2400.f - 200.f * u) + p.side * (-320.f + 220.f * u) + p.up * (480.f + 60.f * u);
        const Vec3 target = p.limb * 0.6f - p.side * (80.f * u) - p.up * 60.f;
        Camera c = lookAt(eye, target, kFov35, 0);
        c.up = p.up;
        return c;
    }
    // The eye's path through the sunrise: craning up the tangent until the sun clears the limb on D, then
    // towards the sun and down over the terminator into the morning.
    const Vec3 forward = p.sun * std::cos(0.2f) - p.side * std::sin(0.2f); // the sun on the right third
    const Vec3 lookSun = normalize(forward - p.up * 0.14f);              // the limb in the upper third
    const double crane = d - times.craneStart;
    const Vec3 morning = overSea(p.up + 0.55f * p.sun - 0.1f * p.side, 160.f);
    if (t < times.lowPass) {
        const Key eyes[] = {{times.craneStart, p.sunriseEye - p.up * static_cast<float>(90.0 * crane) - p.sun * 60.f},
                            {d, p.sunriseEye},
                            {d + 0.45 * (times.lowPass - d), p.limb * 1.12f - p.sun * 550.f + p.up * 60.f},
                            {times.lowPass, morning}};
        const Vec3 eye = throughKeys(eyes, t);
        // The view: on the sun and the limb through the crane and the sunrise, then down and ahead onto the
        // morning side of the terminator as the camera descends.
        const float turn = smooth01(static_cast<float>((t - d - 0.5 * times.barSeconds) / (times.lowPass - d - 0.5 * times.barSeconds)));
        const Vec3 down = normalize(p.sun * 0.8f - normalize(eye) * 0.6f + p.side * 0.15f);
        const Vec3 look = normalize(lookSun * (1.f - turn) + down * turn);
        Camera c = lookAt(eye, eye + look * 500.f, kFov35, t < d ? 1u : 2u);
        c.up = normalize(p.up * (1.f - turn) + normalize(eye) * turn);
        return c;
    }
    // Shot 3: the low pass over the cloud tops, into the low sun, along the great circle under (0, 1, 0).
    const float u = static_cast<float>((t - times.lowPass) / std::max(end - times.lowPass, 1e-3));
    const Vec3 heading = lowPassHeading();
    const float arc = -0.006f + 0.028f * u; // radians along the circle: 28 units of travel
    const Vec3 radial = Vec3{0.f, 1.f, 0.f} * std::cos(arc) + heading * std::sin(arc);
    const Vec3 along = heading * std::cos(arc) - Vec3{0.f, 1.f, 0.f} * std::sin(arc);
    const float altitude = 6.1f + 0.35f * std::sin(3.1f * u); // skimming the cloud tops
    const Vec3 eye = radial * (kPlasmaPlanetRadius + altitude) + cross(radial, along) * (3.f * u);
    const Vec3 look = normalize(along - radial * 0.13f - cross(radial, along) * 0.08f);
    Camera c = lookAt(eye, eye + look * 20.f, kFov24, 3);
    c.up = radial;
    return c;
}

} // namespace

// --- Configuration ---------------------------------------------------------------------------------------------

NebulaStats nebulaStats() {
    return {kNebulaGrid,
            std::uint64_t{kNebulaGrid} * kNebulaGrid * kNebulaGrid,
            kNebulaStars,
            kNebulaEmbeddedStars,
            kNebulaBackgroundStars,
            std::uint64_t{kPlasmaGrid[0]} * kPlasmaGrid[1] * kPlasmaGrid[2]};
}

std::array<std::uint32_t, 3> nebulaGridSize(show::Effect effect, SceneDetail detail) {
    if (effect == show::Effect::PlasmaPlanet) {
        // The larger of the two grids per axis (the orbit's and the low pass's).
        show::TimelineFrame f{};
        f.effect = effect;
        const auto orbit = plasmaPlanetGrid(f, detail).size;
        f.localSeconds = 1e9;
        const auto shell = plasmaPlanetGrid(f, detail).size;
        return {std::max(orbit[0], shell[0]), std::max(orbit[1], shell[1]), std::max(orbit[2], shell[2])};
    }
    const std::array<std::uint32_t, 3> full =
        effect == show::Effect::Plasma ? kPlasmaGrid : std::array<std::uint32_t, 3>{kNebulaGrid, kNebulaGrid, kNebulaGrid};
    const double scale = std::cbrt(std::clamp(static_cast<double>(detail.scale), 0.0, 1.0));
    std::array<std::uint32_t, 3> size{};
    for (int a = 0; a < 3; ++a)
        size[a] = std::max(kMinNebulaGrid, static_cast<std::uint32_t>(std::lround(full[a] * scale)));
    return size;
}

std::uint32_t nebulaSplatCount(SceneDetail detail) {
    const double scale = std::clamp(static_cast<double>(detail.scale), 0.0, 1.0);
    return std::max(kMinNebulaSplats, static_cast<std::uint32_t>(std::llround(kNebulaBackgroundStars * scale)));
}

std::uint32_t nebulaStarCount(show::Effect effect) {
    return effect == show::Effect::Plasma ? kPlasmaStars : kNebulaStars + kNebulaEmbeddedStars;
}

std::array<float, 16> nebulaStarParams(const show::TimelineFrame& frame) {
    std::array<float, 16> p{};
    p[0] = frame.effect == show::Effect::Plasma ? 1.f : 0.f;
    p[1] = static_cast<float>(nebulaStarCount(frame.effect));
    p[2] = std::clamp(frame.energy, 0.f, 1.f);
    p[3] = std::clamp(frame.kick, 0.f, 1.f);
    return p;
}

rt::SphereRecord nebulaStar(std::uint32_t id, float, const std::array<float, 16>& params) {
    rt::SphereRecord s{};
    const float energy = params[2], kick = params[3];
    if (params[0] == 1.f) {
        const bool warm = id == 0u;
        setColor(s.center, warm ? Vec3{-7.f, 3.f, 0.5f} : Vec3{8.f, -4.f, -1.5f});
        s.radius = 0.22f * (1.f + 0.15f * energy);
        setColor(s.color, warm ? Vec3{1.f, 0.62f, 0.22f} : Vec3{0.35f, 0.5f, 1.f});
        s.emission = 220.f * (1.f + 0.5f * kick);
        return s;
    }
    if (id < kNebulaStars) {
        setColor(s.center, kNebulaStarCentres[id]);
        s.radius = kNebulaStarRadii[id] * (id == 0u ? 1.f + 0.22f * energy : 1.f);
        setColor(s.color, kHeroColour[id]);
        s.emission = kHeroEmission[id] * (1.f + 0.3f * kick);
        return s;
    }
    const std::uint32_t h = starHash(id * 0x9E3779B9u + 0x51EDu);
    const std::uint32_t i = id % 4u;
    const NebulaPillar& pillar = kNebulaPillars[i];
    const float along = 0.15f + 0.8f * starUnit(h);
    const float angle = 6.2831853f * starUnit(starHash(h));
    const float reach = 0.55f * starUnit(starHash(h ^ 0x68E31DA4u));
    const Vec3 axis = pillar.top - pillar.base;
    const Vec3 side = normalize(cross(axis, Vec3{0.f, 0.f, 1.f}));
    const Vec3 other = normalize(cross(axis, side));
    setColor(s.center, pillarAxis(i, along) + pillarRadius(i, along) * reach *
                                                   (std::cos(angle) * side + std::sin(angle) * other));
    const float shade = starUnit(starHash(h + 7u));
    s.radius = 0.12f + 0.1f * shade;
    setColor(s.color, {1.f, 0.72f + 0.18f * shade, 0.45f + 0.35f * shade});
    s.emission = 260.f + 200.f * shade;
    return s;
}

// PLASMA PLANET's sun: the environment's disc (radiance and angular radius) and the irradiance it gives.
constexpr float kPlanetSunRadiance = 160000.f, kPlanetSunRadius = 0.27f * 3.14159265f / 180.f;
constexpr float kPlanetSunIrradiance = kPlanetSunRadiance * 3.14159265f * kPlanetSunRadius * kPlanetSunRadius;
// The clouds' albedo (the air's colour is baked: see the bake's mode 2) and phase.
constexpr Vec3 kPlanetCloudAlbedo{0.9f, 0.91f, 0.93f};
constexpr float kPlanetCloudPhaseG = 0.6f;

std::array<float, 16> nebulaMediumParams(const show::TimelineFrame& frame) {
    const bool plasma = frame.effect == show::Effect::Plasma;
    std::array<float, 16> p{};
    if (frame.effect == show::Effect::PlasmaPlanet) {
        const PlasmaPlanetGrid grid = plasmaPlanetGrid(frame, {});
        const Vec3 eye = nebulaCamera(frame).eye;
        p[0] = 2.f;
        p[1] = static_cast<float>(frame.localSeconds);
        p[2] = kPlanetSunIrradiance;
        p[4] = grid.lo.x, p[5] = grid.lo.y, p[6] = grid.lo.z, p[7] = kPlasmaPlanetRadius;
        p[8] = grid.hi.x, p[9] = grid.hi.y, p[10] = grid.hi.z;
        p[11] = frame.localSeconds >= plasmaPlanetTimes(frame).lowPass ? 1.f : 0.f; // the cloud shell
        p[12] = eye.x, p[13] = eye.y, p[14] = eye.z;
        return p;
    }
    p[0] = plasma ? 1.f : 0.f;
    p[1] = static_cast<float>(frame.localSeconds);
    p[2] = std::clamp(frame.bass, 0.f, 1.f);
    p[3] = std::clamp(frame.energy, 0.f, 1.f);
    const Vec3 lo = plasma ? kPlasmaBoxLo : kNebulaBoxLo, hi = plasma ? kPlasmaBoxHi : kNebulaBoxHi;
    p[4] = lo.x, p[5] = lo.y, p[6] = lo.z;
    p[8] = hi.x, p[9] = hi.y, p[10] = hi.z;
    if (plasma) {
        // The 1993 palette turns one ring entry per 1993 frame; here continuously.
        p[12] = static_cast<float>(std::fmod(frame.localSeconds * kPlasmaRingRate, 224.0));
        p[13] = 0.62f;
    } else {
        p[12] = kNebulaOpacity;
        p[13] = kNebulaDensityScale;
        // Star A's shafts, towards the frame's eye: strongest from the reveal on.
        const Vec3 eye = nebulaCamera(frame).eye;
        p[7] = eye.x, p[11] = eye.y, p[14] = eye.z;
        const NebulaTimes times = nebulaTimes(frame);
        p[15] = 0.35f + 0.65f * smooth01(static_cast<float>((frame.localSeconds - times.downbeat) / times.barSeconds));
    }
    return p;
}

GridMediumDesc nebulaMedium(const show::TimelineFrame& frame, SceneDetail detail) {
    const bool plasma = frame.effect == show::Effect::Plasma;
    GridMediumDesc g;
    if (frame.effect == show::Effect::PlasmaPlanet) {
        const PlasmaPlanetGrid grid = plasmaPlanetGrid(frame, detail);
        g.size = grid.size;
        g.worldToMedium = mediumBox(grid.lo, grid.hi);
        g.densityScale = 1.f;
        g.albedo = kPlanetCloudAlbedo;
        g.phaseG = kPlanetCloudPhaseG;
        g.emissionScale = 1.f;
        g.params = nebulaMediumParams(frame);
        return g;
    }
    g.size = nebulaGridSize(frame.effect, detail);
    g.worldToMedium = plasma ? mediumBox(kPlasmaBoxLo, kPlasmaBoxHi) : mediumBox(kNebulaBoxLo, kNebulaBoxHi);
    g.densityScale = plasma ? 0.16f : kNebulaDensityScale;
    g.albedo = plasma ? Vec3{kPlasmaAlbedo, kPlasmaAlbedo, kPlasmaAlbedo} : kNebulaAlbedo;
    g.phaseG = plasma ? 0.45f : kNebulaPhaseG;
    g.emissionScale = 1.f;
    g.params = nebulaMediumParams(frame);
    return g;
}

// --- The 1993 plasma -------------------------------------------------------------------------------------------

float plasmaField(float px, float py, float t) {
    constexpr float kPixelAspect = 1.2f;
    const float sx = px, sy = py * kPixelAspect;
    const float c1x = 160.f + 120.f * std::sin(t * 0.31f), c1y = (100.f + 75.f * std::sin(t * 0.43f + 1.f)) * kPixelAspect;
    const float c2x = 160.f + 110.f * std::cos(t * 0.27f + 2.f), c2y = (100.f + 80.f * std::cos(t * 0.37f)) * kPixelAspect;
    float field = std::sin(px / 41.f + t * 0.83f);
    field += std::sin(py / 29.f - t * 0.61f);
    field += std::sin((px + py) / 53.f + t * 0.47f);
    field += std::sin(std::hypot(sx - c1x, sy - c1y) / 17.f - t * 1.30f);
    field += std::sin(std::hypot(sx - c2x, sy - c2y) / 23.f + t * 0.90f);
    field += 0.5f * std::sin(px / 13.f + 2.f * std::sin(py / 19.f + t * 0.70f) + t);
    field += 0.5f * std::sin(py / 11.f + 2.f * std::sin(px / 23.f - t * 0.55f));
    return field / 6.f;
}

std::uint32_t plasmaRingIndex(float field) {
    const float level = std::clamp(0.5f + 0.5f * field, 0.f, 1.f);
    return static_cast<std::uint32_t>(std::floor(level * static_cast<float>(kPlasmaFolds) * 224.f)) % 224u;
}

Vec3 plasmaRingColour(std::uint32_t entry) {
    struct Stop {
        float r, g, b;
    };
    constexpr Stop kViolet{40, 0, 90}, kMagenta{210, 20, 170}, kOrange{255, 120, 20}, kYellow{255, 235, 90},
        kCyan{40, 220, 235}, kDeepBlue{10, 30, 140};
    entry %= 224u;
    Stop from = kViolet, middle = kMagenta, to = kOrange;
    float span = 75.f, i = static_cast<float>(entry);
    if (entry >= 150u) {
        from = kCyan, middle = kDeepBlue, to = kViolet, span = 74.f, i -= 150.f;
    } else if (entry >= 75u) {
        from = kOrange, middle = kYellow, to = kCyan, i -= 75.f;
    }
    const float t = 2.f * i / span;
    const auto mix = [](Stop a, Stop b, float s) {
        return Vec3{std::floor(a.r + (b.r - a.r) * s + 0.5f), std::floor(a.g + (b.g - a.g) * s + 0.5f),
                    std::floor(a.b + (b.b - a.b) * s + 0.5f)};
    };
    return (t < 1.f ? mix(from, middle, t) : mix(middle, to, t - 1.f)) / 255.f;
}

float nebulaPillarDistance(Vec3 p) {
    float d = 1e9f;
    for (std::uint32_t i = 0; i < kNebulaPillars.size(); ++i) {
        const NebulaPillar& pillar = kNebulaPillars[i];
        const Vec3 ab = pillar.top - pillar.base;
        const float h = std::clamp(dot(p - pillar.base, ab) / dot(ab, ab), 0.f, 1.f);
        d = std::min(d, length(p - pillarAxis(i, h)) - pillarRadius(i, h));
    }
    return d;
}

// --- PLASMA PLANET ----------------------------------------------------------------------------------------

Vec3 plasmaPlanetSun() {
    return normalize(Vec3{0.95f, 0.27f, 0.12f});
}

PlasmaPlanetTimes plasmaPlanetTimes(const show::TimelineFrame& f) {
    const shots::Bars b = shots::barsOf(f);
    return {b.at(-2), b.d, b.at(5), b.bar};
}

float plasmaPlanetSunClearance(Vec3 eye) {
    const Vec3 sun = plasmaPlanetSun();
    const float along = dot(eye, sun);
    if (along >= 0.f)
        return std::numeric_limits<float>::infinity();
    return length(eye - sun * along) - kPlasmaPlanetRadius;
}

PlasmaPlanetGrid plasmaPlanetGrid(const show::TimelineFrame& f, SceneDetail detail) {
    const double scale = std::cbrt(std::clamp(static_cast<double>(detail.scale), 0.0, 1.0));
    const auto scaled = [&](std::uint32_t n) {
        return std::max(kMinNebulaGrid, static_cast<std::uint32_t>(std::lround(n * scale)));
    };
    PlasmaPlanetGrid g;
    if (f.localSeconds < plasmaPlanetTimes(f).lowPass) {
        const float r = kPlasmaPlanetRadius + kPlasmaPlanetAir;
        g.lo = {-r, -r, -r};
        g.hi = {r, r, r};
        g.size = {scaled(kPlasmaPlanetOrbitGrid), scaled(kPlasmaPlanetOrbitGrid), scaled(kPlasmaPlanetOrbitGrid)};
        return g;
    }
    // The low pass's stretch of sea (along x, across z; the sea curves down by up to 4 units inside it).
    g.lo = {-34.f, kPlasmaPlanetRadius - 4.6f, -62.f};
    g.hi = {126.f, kPlasmaPlanetRadius + kPlasmaPlanetCloudTop + 1.5f, 18.f};
    g.size = {scaled(kPlanetCloudGrid[0]), scaled(kPlanetCloudGrid[2]), scaled(kPlanetCloudGrid[1])};
    return g;
}

Vec3 plasmaSeaParams(const show::TimelineFrame& f) {
    // The 1993 effect's own clock (its field moves at 1993's pace) and its palette turning one entry per 1993
    // frame; the cracks flare on the kick.
    const auto seconds = static_cast<float>(f.localSeconds);
    return {seconds, static_cast<float>(std::fmod(f.localSeconds * kPlasmaRingRate, 224.0)),
            1.6f + 1.2f * std::clamp(f.kick, 0.f, 1.f)};
}

// --- Cameras ---------------------------------------------------------------------------------------------------

NebulaTimes nebulaTimes(const show::TimelineFrame& f) {
    const auto [downbeat, bar] = downbeatAtAccent(f);
    return {downbeat, barLinesBefore(f, downbeat, 4, bar), bar};
}

Camera nebulaCamera(const show::TimelineFrame& frame) {
    if (frame.effect == show::Effect::PlasmaPlanet)
        return planetCamera(frame);
    return frame.effect == show::Effect::Plasma ? plasmaCamera(frame) : nebulaFlight(frame);
}

float nebulaExposureEV(const show::TimelineFrame& frame) {
    if (frame.effect == show::Effect::Plasma)
        return 0.f;
    if (frame.effect != show::Effect::PlasmaPlanet)
        return -0.5f;
    // The night side by the sea's glow, then the day: the exposure follows the sunrise over the bar after D
    // (the sun's glare does the rest), and the low pass in the morning light.
    const PlasmaPlanetTimes times = plasmaPlanetTimes(frame);
    const float day = smooth01(static_cast<float>((frame.localSeconds - times.sunrise) / times.barSeconds));
    if (frame.localSeconds >= times.lowPass)
        return -2.3f;
    return 0.4f - 2.6f * day;
}

// --- The scene -------------------------------------------------------------------------------------------------

Nebula::Nebula(SceneDetail detail) : detail_(detail) {
    static_.meshes = {quad(1.f, 1.f)}; // unused: the stars are spheres
    static_.materials.resize(kMaterialCount);
    rt::Material& star = static_.materials[kStar];
    setColor(star.baseColor, {0.f, 0.f, 0.f});
    setColor(star.emission, {0.f, 0.f, 0.f});
    star.emissionScale = 1.f;
    star.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    SceneCapacity& c = static_.capacity;
    c.instances = 4;
    c.lights = 2;
    c.materials = kMaterialCount;
    c.particles = kNebulaStars + kNebulaEmbeddedStars;
    c.treeLights = kNebulaStars + kNebulaEmbeddedStars;
    c.splats = nebulaSplatCount(detail);
    const auto nebula = nebulaGridSize(show::Effect::Nebula, detail), plasma = nebulaGridSize(show::Effect::Plasma, detail);
    const auto planet = nebulaGridSize(show::Effect::PlasmaPlanet, detail);
    for (int a = 0; a < 3; ++a)
        c.mediumGrid[a] = std::max({nebula[a], plasma[a], planet[a]});

    // PLASMA PLANET's sea: basalt crust over the glowing plasma (the PlasmaSea pattern), one analytic sphere.
    rt::Material& sea = static_.materials[kSea];
    setColor(sea.baseColor, {0.045f, 0.04f, 0.038f});
    sea.roughness = 0.55f;
    setColor(sea.emission, {1.f, 1.f, 1.f});
    sea.emissionScale = 1.4f;
    sea.pattern = static_cast<std::uint32_t>(rt::Pattern::PlasmaSea);
    sea.patternScale = kPlasmaPlanetPixels;
    rt::SphereRecord sphere{};
    sphere.radius = kPlasmaPlanetRadius;
    sphere.color[0] = sphere.color[1] = sphere.color[2] = 1.f;
    sphere.material = kSea;
    static_.spheres = {sphere};
    c.spheres = 1;
    static_.particleShader = kNebulaStarsShader;
    static_.splatShader = kNebulaSplatsShader;
    static_.mediumShader = kNebulaMediumShader;
}

SceneFrame Nebula::planetFrame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    f.camera = nebulaCamera(frame);
    f.exposureEV = nebulaExposureEV(frame);
    f.clampIndirect = 16.f;
    const PlasmaPlanetTimes times = plasmaPlanetTimes(frame);
    const bool lowPass = frame.localSeconds >= times.lowPass;
    const Vec3 sun = plasmaPlanetSun();

    // The sea, its plasma turning.
    f.materials = static_.materials;
    setColor(f.materials[kSea].baseColor2, plasmaSeaParams(frame));
    SceneInstance planet;
    planet.mesh = 0;
    planet.material = kSea;
    planet.transform = identity();
    planet.kind = rt::GeometryKind::Spheres;
    planet.firstSphere = 0;
    planet.sphereCount = 1;
    f.instances.push_back(planet);

    MediumFrame medium;
    medium.grid = nebulaMedium(frame, detail_);
    EnvironmentDesc& env = f.environment;
    if (!lowPass) {
        // Space: the sun's disc and the stars; the air is the grid's.
        env = environmentDefaults(EnvironmentKind::Space);
        env.starBrightness = 0.25f;
    } else {
        // Under the air: a morning sky from the sea's horizon (which lies a few degrees below the eye's level)
        // up to a deep zenith; the haze over the sea is the grid's air.
        env = environmentDefaults(EnvironmentKind::Day);
        env.zenith = {0.02f, 0.05f, 0.17f};
        env.horizon = {0.3f, 0.3f, 0.33f};
        env.ground = env.horizon;
        env.haze = 0.3f;
    }
    env.discDirection = sun;
    env.discAngularRadius = kPlanetSunRadius;
    env.discRadiance = Vec3{1.f, 0.96f, 0.9f} * kPlanetSunRadiance;
    env.select = 1.f; // the sun is the only light
    f.medium = medium;
    return f;
}

SceneFrame Nebula::frame(const show::TimelineFrame& frame) const {
    if (frame.effect == show::Effect::PlasmaPlanet)
        return planetFrame(frame);
    const bool plasma = frame.effect == show::Effect::Plasma;
    SceneFrame f;
    f.camera = nebulaCamera(frame);
    f.exposureEV = nebulaExposureEV(frame);
    f.clampIndirect = 16.f;
    f.environment = environmentDefaults(EnvironmentKind::Space);
    f.environment.starBrightness = plasma ? 0.5f : 0.f; // NEBULA's stars are splats
    f.environment.select = 0.f;

    MediumFrame medium;
    medium.grid = nebulaMedium(frame, detail_);
    f.medium = medium;

    // The stars: a sphere field that is also the light tree.
    ParticleFrame stars;
    stars.count = nebulaStarCount(frame.effect);
    stars.params = nebulaStarParams(frame);
    stars.material = kStar;
    f.particles = stars;
    SceneInstance field;
    field.mesh = 0;
    field.material = kStar;
    field.transform = identity();
    field.kind = rt::GeometryKind::Spheres;
    field.dynamicSpheres = true;
    field.firstSphere = 0;
    field.sphereCount = stars.count;
    rt::RectLight tree{};
    tree.kind = static_cast<std::uint32_t>(rt::LightKind::Tree);
    tree.selectWeight = 1.f;
    tree.gate = rt::kNoLight;
    field.light = static_cast<std::uint32_t>(f.lights.size());
    f.lights.push_back(tree);
    f.instances.push_back(field);
    const auto seconds = static_cast<float>(frame.localSeconds);
    f.treeLights.reserve(stars.count);
    for (std::uint32_t k = 0; k < stars.count; ++k) {
        const rt::SphereRecord s = nebulaStar(k, seconds, stars.params);
        f.treeLights.push_back({colorOf(s.center), s.radius, colorOf(s.color) * s.emission, 1.f, k});
    }

    if (!plasma) {
        SplatFrame splats;
        splats.count = nebulaSplatCount(detail_);
        splats.params[0] = static_cast<float>(splats.count);
        splats.params[1] = 1.f; // the flux scale
        f.splats = splats;
    }
    return f;
}

// --- The epoch cards
// ----------------------------------------------------------------------------------------------

std::string Nebula::nowCard(show::Effect effect) const {
    if (effect == show::Effect::PlasmaPlanet)
        return std::string("2026") + kCardDot + "PLASMA PLANET" + kCardDot + std::to_string(kPlanetCloudGrid[0]) +
               "\u00D7" + std::to_string(kPlanetCloudGrid[1]) + "\u00D7" + std::to_string(kPlanetCloudGrid[2]) +
               " CLOUD SHELL" + kCardDot + "SUNRISE";
    if (effect == show::Effect::Plasma)
        return std::string("2026") + kCardDot + "VOLUMETRIC NEBULA" + kCardDot + "DELTA TRACKING" + kCardDot +
               "MULTIPLE SCATTERING";
    if (effect != show::Effect::Nebula)
        return {};
    const NebulaStats stats = nebulaStats();
    return std::string("2026") + kCardDot + "NEBULA" + kCardDot + groupThousands(stats.voxels) + " VOXELS" +
           kCardDot + "MULTIPLE SCATTERING" + kCardDot + groupThousands(stats.stars) + " STARS";
}

} // namespace evo::modern
