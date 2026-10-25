#include "modern/scenes/nebula.hpp"

#include "modern/mesh_builder.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
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
                        {after, {-24.5f, 3.5f, -28.f}},
                        {after + 0.5 * (end - after), {-28.f, 4.5f, -24.f}},
                        {end, {-30.5f, 6.f, -19.f}}};
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
    const Vec3 look = normalize(Vec3{-0.02f * smooth01(static_cast<float>((t - downbeat) / (end - downbeat))), 0.f, -1.f});
    return lookAt(eye, eye + 10.f * look, kFov24, 0);
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

std::array<float, 16> nebulaMediumParams(const show::TimelineFrame& frame) {
    const bool plasma = frame.effect == show::Effect::Plasma;
    std::array<float, 16> p{};
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
    }
    return p;
}

GridMediumDesc nebulaMedium(const show::TimelineFrame& frame, SceneDetail detail) {
    const bool plasma = frame.effect == show::Effect::Plasma;
    GridMediumDesc g;
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

// --- Cameras ---------------------------------------------------------------------------------------------------

NebulaTimes nebulaTimes(const show::TimelineFrame& f) {
    const auto [downbeat, bar] = downbeatAtAccent(f);
    return {downbeat, barLinesBefore(f, downbeat, 4, bar), bar};
}

Camera nebulaCamera(const show::TimelineFrame& frame) {
    return frame.effect == show::Effect::Plasma ? plasmaCamera(frame) : nebulaFlight(frame);
}

float nebulaExposureEV(show::Effect effect) {
    return effect == show::Effect::Plasma ? 0.f : -0.5f;
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
    for (int a = 0; a < 3; ++a)
        c.mediumGrid[a] = std::max(nebula[a], plasma[a]);
    static_.particleShader = kNebulaStarsShader;
    static_.splatShader = kNebulaSplatsShader;
    static_.mediumShader = kNebulaMediumShader;
}

SceneFrame Nebula::frame(const show::TimelineFrame& frame) const {
    const bool plasma = frame.effect == show::Effect::Plasma;
    SceneFrame f;
    f.camera = nebulaCamera(frame);
    f.exposureEV = nebulaExposureEV(frame.effect);
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

} // namespace evo::modern
