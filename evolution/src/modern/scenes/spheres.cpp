#include "modern/scenes/spheres.hpp"

#include "modern/environment.hpp"
#include "modern/mesh_builder.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>

namespace evo::modern {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTau = 2.f * kPi;
constexpr float kDegrees = kPi / 180.f;

// --- Small helpers -----------------------------------------------------------------------------------------

void setColor(float out[3], Vec3 c) {
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
}

rt::Material plastic(Vec3 color, float roughness) {
    rt::Material m;
    setColor(m.baseColor, color);
    m.roughness = roughness;
    return m;
}

rt::Material metal(Vec3 color, float roughness) {
    rt::Material m = plastic(color, roughness);
    m.metallic = 1.f;
    return m;
}

std::uint32_t hashU(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float unitOf(std::uint32_t x) {
    return float(x >> 8) * (1.f / 16777216.f);
}

float smooth(float x) {
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

Vec3 colorOf(const float c[3]) {
    return {c[0], c[1], c[2]};
}

// A quad (facing +z) at `position` facing towards `aim`, without mirroring.
rt::Mat34 facing(Vec3 position, Vec3 aim) {
    const Vec3 z = normalize(aim - position);
    const Vec3 x = normalize(cross(Vec3{0, 1, 0}, z));
    const Vec3 y = cross(z, x);
    return {{{x.x, y.x, z.x, position.x}, {x.y, y.y, z.y, position.y}, {x.z, y.z, z.z, position.z}}};
}

// The floor quad laid flat (facing +y) under the camera, moved in steps of two squares so the checker stays
// put and the board never ends.
rt::Mat34 floorUnder(Vec3 eye, float height = 0.f) {
    const Vec3 at{2.f * std::floor(0.5f * eye.x), height, 2.f * std::floor(0.5f * eye.z)};
    return compose(translate(at), rotate({1, 0, 0}, -0.5f * kPi));
}

SceneInstance sphereInstance(std::uint32_t first, std::uint32_t count, std::uint32_t material,
                             const rt::Mat34& transform) {
    SceneInstance s{};
    s.mesh = 0;
    s.material = material;
    s.transform = transform;
    s.kind = rt::GeometryKind::Spheres;
    s.firstSphere = first;
    s.sphereCount = count;
    return s;
}

// --- RAYTRACE's layout -------------------------------------------------------------------------------------

constexpr std::uint32_t kExtraBalls = kRaytraceSphereCount - kRaytraceRetroSpheres;

// Glass tints: emerald (1993's glass sphere), ruby, sapphire, amber, clear, champagne.
constexpr Vec3 kEmerald{0.3f, 0.95f, 0.62f};
constexpr std::array<Vec3, 5> kGlassTints{
    {{1.f, 0.22f, 0.26f}, {0.3f, 0.5f, 1.f}, {1.f, 0.66f, 0.2f}, {1.f, 1.f, 1.f}, {1.f, 0.86f, 0.6f}}};

struct BallLook {
    float radius;
    std::uint32_t material;
    Vec3 colour;
};

struct ExtraBall {
    float x, z, radius;
    std::uint32_t material;
    Vec3 colour;
    std::uint32_t period; // beats per bounce
    float offset;         // in beats
};

// Where the 42 extra balls stand: a golden-angle spiral out from the 1993 orbit (radius 4.6 to 13), each
// turned on until it overlaps nothing, keeps clear of the 1993 camera and of the low orbit's path (shot 1).
std::array<ExtraBall, kExtraBalls> extraLayout() {
    std::array<ExtraBall, kExtraBalls> balls{};
    for (std::uint32_t i = 0; i < kExtraBalls; ++i) {
        const std::uint32_t h = hashU(i * 0x9E3779B9u + 77u);
        const float u = unitOf(h), v = unitOf(hashU(h));
        ExtraBall b{};
        b.radius = 0.3f + 0.42f * u;
        const float ring = 4.6f + 8.4f * std::sqrt((float(i) + 0.5f) / float(kExtraBalls));
        // A mix of finishes: mirror chrome, brushed steel, satin gold, coated copper, black lacquer, pearl
        // and tinted glass.
        constexpr std::array<std::uint32_t, 10> kFinishes{
            Spheres::kChrome, Spheres::kChrome,  Spheres::kBrushedSteel, Spheres::kGold,  Spheres::kGold,
            Spheres::kCopper, Spheres::kLacquer, Spheres::kPearl,        Spheres::kGlass, Spheres::kGlass};
        b.material =
            kFinishes[std::min<std::size_t>(std::size_t(v * float(kFinishes.size())), kFinishes.size() - 1)];
        b.colour =
            b.material == Spheres::kGlass ? kGlassTints[hashU(h + 3u) % kGlassTints.size()] : Vec3{1, 1, 1};
        b.period = i % 3 == 0 ? 1u : 2u;
        b.offset = i % 3 == 2 ? 1.f : 0.f;
        float angle = float(i) * 2.39996323f + 0.4f;
        for (int attempt = 0; attempt < 64; ++attempt, angle += 0.37f) {
            const float x = ring * std::cos(angle), z = ring * std::sin(angle);
            // Clear of the 1993 camera at (0, -8) and of the low orbit (radius 4.3..7.2 on its arc).
            const float camera = std::hypot(x, z + 8.f);
            const float azimuth = std::atan2(z, x);
            const bool orbit = ring + b.radius > 4.2f && ring - b.radius < 7.3f &&
                               azimuth > -0.5f * kPi - 0.5f && azimuth < -0.5f * kPi + 1.45f;
            bool clear = camera > b.radius + 2.2f && !orbit;
            for (std::uint32_t k = 0; k < i && clear; ++k)
                clear = std::hypot(x - balls[k].x, z - balls[k].z) > b.radius + balls[k].radius + 0.25f;
            if (clear || attempt == 63) {
                b.x = x;
                b.z = z;
                break;
            }
        }
        balls[i] = b;
    }
    return balls;
}

const std::array<ExtraBall, kExtraBalls>& extras() {
    static const std::array<ExtraBall, kExtraBalls> balls = extraLayout();
    return balls;
}

BallLook ballLook(std::uint32_t i) {
    if (i < kRaytraceRetroSpheres) {
        if (i == kRaytraceGlassBall)
            return {0.6f, Spheres::kGlass, kEmerald};
        return {i == 0 ? 1.3f : 0.6f, Spheres::kChrome, {1, 1, 1}};
    }
    const ExtraBall& b = extras()[i - kRaytraceRetroSpheres];
    return {b.radius, b.material, b.colour};
}

// The colossal mirror sphere resting on the board's horizon on the right third of 1993's view (the
// background layer): centre, radius and finish. It breathes up and down a little.
struct Giant {
    Vec3 centre;
    float radius;
    std::uint32_t material;
};
constexpr std::array<Giant, kRaytraceGiants> kGiants{{{{-1100.f, 380.f, 3000.f}, 420.f, Spheres::kChrome}}};

// The tiles of the board near the camera: kRaytraceTileSide^2 bevelled tiles, 1 unit apart with a groove
// between them, round a point ahead of the camera; tile (i, j) covers [i, i + 1) x [j, j + 1) and is white
// when i + j is even (as 1993's squares). Each has its own finish and sits a little unevenly.
constexpr float kTileHalf = 0.47f, kTileHalfHeight = 0.05f;

// The sun's disc radius (degrees) before the drop, and how much it widens on it.
constexpr float kRaytraceSunRadius = 1.1f, kRaytraceSunWiden = 0.15f;
// The caustic receiver: a square of the board round the balls, and its photons at full detail.
constexpr float kRaytraceReceiverSide = 32.f;
constexpr std::uint32_t kRaytraceReceiverResolution = 1024, kRaytracePhotons = 1u << 20;

std::uint32_t raytracePhotons(SceneDetail detail) {
    const double n = std::llround(double(kRaytracePhotons) * std::clamp(double(detail.scale), 0.0, 1.0));
    return std::uint32_t(std::max(n, 4096.0));
}

// The light: the sun is the warm key (where 1993's point light stood), the sky the cool fill with a cool
// softbox behind and left of 1993's camera, and a warm strip high behind the balls their rim (the balls
// reflect both).
constexpr Vec3 kSoftboxA{-7.f, 9.f, -13.f}, kRimLight{0.f, 20.f, 24.f}, kSoftboxAim{0.f, 0.8f, 0.f};
constexpr Vec3 kSoftboxColor{0.78f, 0.88f, 1.f}, kRimColor{1.f, 0.78f, 0.52f};
constexpr float kSoftboxIntensity = 4.f, kRimIntensity = 9.f;
constexpr float kRimWidth = 14.f, kRimHeight = 2.5f;
// The sun lights the board far more than the softboxes (which matter most as reflections in the balls), so
// next-event estimation picks the sky (and its sun) 0.8 of the time and the softboxes little.
constexpr float kSoftboxWeight = 0.25f;

// RAYTRACE's sky: Day with the sun where 1993's point light stood (behind the camera, up left), its disc
// widened to 1.1 degrees (radiance lowered to keep its irradiance) for soft shadows, and to 1.25 over the
// beat from the drop (every shadow visibly softens as 2026 takes over). A deep blue zenith over a pale
// horizon, which the height fog and the cirrus (raytraceMedium) turn from a gradient into air.
EnvironmentDesc raytraceSky(float sunRadiusDegrees) {
    EnvironmentDesc d = environmentDefaults(EnvironmentKind::Day);
    d.discDirection = normalize(Vec3{-5.f, 9.f, -6.f});
    const float k = 0.5f / sunRadiusDegrees;
    d.discAngularRadius = sunRadiusDegrees * kDegrees;
    d.discRadiance = Vec3{d.discRadiance.x, d.discRadiance.y * 0.93f, d.discRadiance.z * 0.8f} * (k * k);
    d.zenith = {0.04f, 0.13f, 0.5f};
    d.horizon = {0.45f, 0.55f, 0.72f};
    d.haze = 0.35f;
    d.select = 0.8f;
    return d;
}

// SPHERE STORM's sky: a storm at dusk, night blue overhead over an ember band, the sun a warm sliver on the
// horizon behind the vortex (its key and rim; a cool softbox far to the left fills).
EnvironmentDesc stormSky() {
    EnvironmentDesc d = environmentDefaults(EnvironmentKind::Dusk);
    d.discDirection = normalize(Vec3{-0.62f, 0.03f, -0.78f});
    d.select = 0.6f;
    d.zenith = {0.004f, 0.016f, 0.045f}; // deep night blue
    d.horizon = {0.1f, 0.06f, 0.03f};    // an amber dusk band (redder bands mix with the blue to mauve)
    d.ground = {0.006f, 0.006f, 0.007f};
    d.townDensity = 0.f;
    d.glow = 0.12f; // a tight glow round the sliver, not an orange sky
    d.haze = 0.f;
    d.discRadiance = d.discRadiance * 0.35f;
    d.select = 1.f;
    return d;
}

// RAYTRACE INFINITY's sky: dusk, a low warm sun, cool slate below the horizon (seen down the lattice's
// channels).
EnvironmentDesc infinitySky() {
    EnvironmentDesc d = environmentDefaults(EnvironmentKind::Dusk);
    d.discDirection = normalize(Vec3{-0.6f, 0.18f, 1.f});
    d.ground = {0.025f, 0.03f, 0.04f};
    d.select = 1.f;
    return d;
}

// RAYTRACE's air: a height fog that turns the board's far squares and the horizon into haze, and a thin
// cirrus deck high over it (shaders/modern/spheres_medium.comp), drifting with the wind.
constexpr float kCirrusBase = 170.f, kCirrusTop = 215.f, kCirrusHalf = 3200.f;
constexpr std::array<std::uint32_t, 3> kCirrusGrid{512, 8, 512};

MediumFrame raytraceMedium(const show::TimelineFrame& frame) {
    MediumFrame m;
    FogDesc fog;
    fog.density = 0.003f; // thick along the board, thin overhead (a scale height of 12 units)
    fog.baseHeight = 0.f;
    fog.falloff = 0.08f;
    fog.albedo = {0.6f, 0.75f, 0.98f}; // cool: the warm sun scatters to a neutral blue-grey, never mauve
    fog.phaseG = 0.55f;
    m.fog = fog;
    GridMediumDesc grid;
    grid.size = kCirrusGrid;
    grid.worldToMedium = mediumBox({-kCirrusHalf, kCirrusBase, -kCirrusHalf}, {kCirrusHalf, kCirrusTop, kCirrusHalf});
    grid.densityScale = 1.f;
    grid.albedo = {0.97f, 0.97f, 0.98f};
    grid.phaseG = 0.7f;
    grid.emissionScale = 0.f;
    grid.params[0] = 0.07f;                                      // density at full cover (per unit)
    grid.params[1] = 0.52f;                                      // coverage
    grid.params[2] = 0.0012f * float(frame.localSeconds);        // drift
    m.grid = grid;
    return m;
}

// --- Cameras -----------------------------------------------------------------------------------------------

// Chapter-local seconds of a chapter's end.
double chapterEnd(const show::TimelineFrame& f) {
    return std::max(f.chapterDuration, f.accentSeconds + 1e-3);
}

// The shot of a chapter whose cuts fall at chapter-local `cuts` (ascending), with eased progress through it.
struct ShotTime {
    std::uint32_t shot;
    float u; // smoothstep progress through the shot
    float t; // seconds into the shot
};

ShotTime shotAt(const show::TimelineFrame& f, std::initializer_list<double> cuts) {
    double from = 0, to = chapterEnd(f);
    std::uint32_t shot = 0;
    for (const double cut : cuts) {
        if (f.localSeconds >= cut) {
            from = cut;
            ++shot;
        } else {
            to = cut;
            break;
        }
    }
    if (shot == cuts.size())
        to = chapterEnd(f);
    const double t = f.localSeconds - from;
    return {shot, smooth(float(t / std::max(to - from, 1e-3))), float(t)};
}

// The second cut: the first bar line after `fraction` of the way from the drop to the chapter's end.
double lateCut(const show::TimelineFrame& f, double fraction) {
    const SpheresDrop drop = spheresDrop(f);
    const double cut = nextBarLine(f, drop.seconds + fraction * (chapterEnd(f) - drop.seconds));
    return std::min(cut, chapterEnd(f) - 0.5);
}

// The tracked bar line k bars from the drop (negative: before it).
double barFromDrop(const show::TimelineFrame& f, const SpheresDrop& drop, double k) {
    return nextBarLine(f, drop.seconds + (k - 0.25) * drop.barSeconds);
}

// A camera looking from eye towards a horizontal heading (degrees from +z towards -x, so 0 looks along
// 1993's view and positive turns right), pitched up by `pitch` degrees.
Camera lookAlong(Vec3 eye, float heading, float pitch, float fov, std::uint32_t shot) {
    const float h = heading * kDegrees, p = pitch * kDegrees;
    const Vec3 d{-std::sin(h) * std::cos(p), std::sin(p), std::cos(h) * std::cos(p)};
    Camera c{eye, eye + d * 10.f, {0, 1, 0}, fov};
    c.shot = shot;
    return c;
}

// The lenses (vertical fields of view at 16:9).
constexpr float kLens24 = 45.2f, kLens35 = 31.8f, kLens85 = 13.7f;
// The pitch that puts the horizon on the lower third line of a lens.
float lowerThird(float fov) {
    return std::atan(std::tan(0.5f * fov * kDegrees) / 3.f) / kDegrees;
}

// RAYTRACE's shots, cut on the bars after the drop:
//   0  until the drop 1993's camera, then (no cut: the camera breaks free) a crane back and up over two
//      bars while the lens closes from 1993's 46 degrees to 35 mm, holding the centre sphere's size;
//   1  2 bars: 85 mm at ball height, tracking right past the bouncing ring, the horizon on the lower third;
//   2  1 bar: 85 mm on a glass ball and the caustic it throws on the board;
//   3  1 bar: 85 mm, chrome reflecting chrome, drifting left;
//   4  1 bar: 85 mm low on the centre sphere, the ring reflected in it;
//   5  to the end: wide and low on 35 mm, the mirror moon on the right third, the balls settling.
Camera raytraceCamera(const show::TimelineFrame& f) {
    const SpheresDrop drop = spheresDrop(f);
    const double bar = drop.barSeconds;
    const ShotTime s = shotAt(f, {barFromDrop(f, drop, 2), barFromDrop(f, drop, 4), barFromDrop(f, drop, 5),
                                  barFromDrop(f, drop, 6), barFromDrop(f, drop, 7)});
    if (s.shot == 0) {
        const Camera retro = raytraceRetroCamera();
        if (f.localSeconds < drop.seconds)
            return retro;
        const float u = smooth(float((f.localSeconds - drop.seconds) / (2.0 * bar)));
        const float fov = lerp(retro.fovYDegrees, kLens35, u);
        // Hold the centre sphere's size: the distance grows as the lens closes.
        const float distance = length(retro.eye - retro.target) * std::tan(0.5f * retro.fovYDegrees * kDegrees) /
                               std::tan(0.5f * fov * kDegrees);
        const float a = lerp(-0.5f * kPi, -0.5f * kPi - 0.35f, u), rise = lerp(std::atan2(1.f, 8.f), 0.42f, u);
        const Vec3 target{0.f, lerp(1.f, 1.3f, u), 0.f};
        const Vec3 out{std::cos(a) * std::cos(rise), std::sin(rise), std::sin(a) * std::cos(rise)};
        Camera c{target + out * distance, target, {0, 1, 0}, fov};
        c.shot = 0;
        return c;
    }
    const float tilt = lowerThird(kLens85);
    const auto u = s.u;
    if (s.shot == 1) { // tracking right at ball height, the ring between us and the centre sphere
        const float a = lerp(-0.5f * kPi + 0.55f, -0.5f * kPi - 0.15f, u);
        const Vec3 eye{19.f * std::cos(a), 0.55f, 19.f * std::sin(a)};
        const Vec3 aim = normalize(Vec3{-eye.x, 0.f, -eye.z});
        Camera c = lookAlong(eye, std::atan2(-aim.x, aim.z) / kDegrees, tilt, kLens85, 1);
        c.aperture = 0.04f;
        c.focusDistance = length(eye) - 9.f;
        return c;
    }
    if (s.shot == 2) { // a glass ball and its caustic, the sun behind the camera
        const RaytraceBall glass = raytraceBalls(f)[kRaytraceGlassBall];
        const Vec3 at{glass.centre.x, glass.radius, glass.centre.z};
        // Across the sunlight, from the outside of its orbit and above the bounces: the ball and, on the board
        // beside its shadow, the caustic it focuses.
        const Vec3 across = normalize(Vec3{6.f, 0.f, -5.f});
        const RaytraceBall first = raytraceRetroBalls(f.localSeconds - double(s.t))[kRaytraceGlassBall];
        const Vec3 outward = normalize(Vec3{first.centre.x, 0.f, first.centre.z}); // chosen once per shot
        const Vec3 side = dot(across, outward) >= 0.f ? across : across * -1.f;
        const Vec3 eye = at + side * (5.2f - 0.4f * u) + Vec3{0.f, 2.6f, 0.f};
        Camera c{eye, at + Vec3{0.25f, -0.45f, 0.3f}, {0, 1, 0}, kLens85};
        c.aperture = 0.04f;
        c.focusDistance = length(at - eye);
        c.shot = 2;
        return c;
    }
    // (Shots 3 and 4 stay in the corridor the extra balls keep clear: radius 4.2 to 7.3 on the arc from
    // 0.5 rad right of 1993's camera to 1.45 left of it.)
    if (s.shot == 3) { // chrome reflecting chrome, drifting left
        const float a = lerp(-0.5f * kPi + 1.0f, -0.5f * kPi + 0.6f, u);
        const Vec3 eye{6.1f * std::cos(a), 0.9f, 6.1f * std::sin(a)};
        Camera c = lookAlong(eye, std::atan2(eye.x, -eye.z) / kDegrees + 8.f, tilt + 1.f, kLens85, 3);
        c.aperture = 0.035f;
        c.focusDistance = length(eye) - 1.3f; // the centre sphere's face
        return c;
    }
    if (s.shot == 4) { // the centre sphere close, the whole ring in it
        const float a = lerp(-0.5f * kPi + 0.3f, -0.5f * kPi + 0.1f, u);
        const Vec3 eye{6.6f * std::cos(a), 1.1f, 6.6f * std::sin(a)};
        Camera c{eye, {0.f, 1.25f, 0.f}, {0, 1, 0}, kLens85};
        c.aperture = 0.04f;
        c.focusDistance = length(eye - Vec3{0.f, 1.3f, 0.f}) - 1.3f;
        c.shot = 4;
        return c;
    }
    // Wide and low: the board runs to the fog, the mirror moon low on the right third.
    const Vec3 eye{lerp(3.5f, 2.f, u), lerp(1.6f, 1.35f, u), lerp(-21.f, -19.f, u)};
    Camera c = lookAlong(eye, lerp(4.f, 7.f, u), lowerThird(kLens35), kLens35, 5);
    return c;
}

Camera stormCamera(const show::TimelineFrame& f) {
    const SpheresDrop drop = spheresDrop(f);
    // 85 mm at floor level until the rise begins (4 bars before D); 35 mm backing off as it rises; on D under
    // the vortex on 24 mm looking up through the ribbons against the dusk; from 3 bars on, an orbit at mid
    // height.
    const ShotTime s = shotAt(f, {barFromDrop(f, drop, -4), drop.seconds, barFromDrop(f, drop, 3)});
    // The dusk is behind the vortex: the cameras stand on the side away from the sun.
    const Vec3 away = normalize(Vec3{0.62f, 0.f, 0.78f});
    const Vec3 side{away.z, 0.f, -away.x};
    if (s.shot == 0) {
        const Vec3 eye = away * 30.f + side * lerp(-1.5f, 1.5f, s.u) + Vec3{0.f, 0.22f, 0.f};
        // The sun's sliver off-centre, near the right third, not behind the vortex's heart.
        Camera c{eye, side * 4.f + Vec3{0.f, 0.45f, 0.f}, {0, 1, 0}, kLens85};
        c.aperture = 0.25f;
        c.focusDistance = 30.f;
        return c;
    }
    if (s.shot == 1) {
        const Vec3 eye = away * lerp(16.f, 21.f, s.u) + side * lerp(2.f, 3.5f, s.u) + Vec3{0.f, lerp(0.9f, 2.4f, s.u), 0.f};
        Camera c{eye, {0.f, lerp(1.2f, 2.6f, s.u), 0.f}, {0, 1, 0}, kLens35};
        c.shot = 1;
        return c;
    }
    if (s.shot == 2) {
        // Low beyond the skirt, the lens tilted up 22 degrees: the funnel climbs the frame over the dusk band.
        const float a = 0.3f * (s.u - 0.5f);
        const Vec3 dir = away * std::cos(a) + side * std::sin(a);
        const Vec3 eye = dir * 14.f + Vec3{0.f, 0.45f, 0.f};
        Camera c{eye, Vec3{0.f, 0.45f + 14.f * std::tan(22.f * kDegrees), 0.f}, {0, 1, 0}, kLens24};
        c.shot = 2;
        return c;
    }
    const float a = 0.9f + 1.1f * s.u;
    const Vec3 dir = away * std::cos(a) + side * std::sin(a);
    const Vec3 eye = dir * lerp(17.f, 15.f, s.u) + Vec3{0.f, lerp(5.5f, 4.8f, s.u), 0.f};
    Camera c{eye, {0.f, 4.6f, 0.f}, {0, 1, 0}, kLens35};
    c.shot = 3;
    return c;
}

constexpr float kFallSpeed = 2.2f;
constexpr float kInfinityFog = 0.0035f, kInfinityFogFalloff = 0.08f; // per unit at the camera; per unit up

Camera infinityCamera(const show::TimelineFrame& f) {
    const SpheresDrop drop = spheresDrop(f);
    const ShotTime s = shotAt(f, {drop.seconds, lateCut(f, 0.62)});
    const float t = float(f.localSeconds);
    const float half = 0.5f * kInfinitySpacing;
    const Vec3 eye{half, 5.f * kInfinitySpacing - kFallSpeed * t, half};
    if (s.shot == 0) {
        const float yaw = 0.7f + 0.45f * s.u, pitch = (52.f - 14.f * s.u) * kDegrees;
        const Vec3 d{std::cos(pitch) * std::cos(yaw), -std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
        return {eye, eye + d, {0, 1, 0}, kLens24};
    }
    if (s.shot == 1) {
        const float pitch = (-6.f + 9.f * s.u) * kDegrees, yaw = 0.5f * kPi + 0.05f * (s.u - 0.5f);
        const Vec3 d{std::cos(pitch) * std::cos(yaw), -std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
        Camera c{eye, eye + d, {0, 1, 0}, kLens24};
        c.shot = 1;
        return c;
    }
    const float roll = 0.3f + 1.4f * s.u;
    Camera c{eye, eye + Vec3{0.f, -1.f, 0.f}, {std::cos(roll), 0.f, std::sin(roll)}, kLens24};
    c.shot = 2;
    return c;
}

} // namespace

// --- The music
// -------------------------------------------------------------------------------------------------

SpheresDrop spheresDrop(const show::TimelineFrame& f) {
    const show::BeatGridView grid = show::beatGrid(f.track);
    const double start = f.trackSeconds - f.localSeconds;
    SpheresDrop d{f.accentSeconds, -1, 2.0};
    int beat = grid.beatAt(start + f.accentSeconds);
    if (beat < 0 || grid.beatTime(beat) < start - 1e-6)
        return d;
    while (beat > 0 && grid.bar(beat) != 0 && grid.beatTime(beat - 1) >= start - 1e-6)
        --beat;
    const double at = grid.beatTime(beat);
    const double next = beat + 1 < grid.count() ? grid.beatTime(beat + 1) : at + 0.5;
    d.seconds = at - start;
    d.beat = beat;
    d.barSeconds = 4.0 * std::max(next - at, 1e-3);
    return d;
}

double nextBarLine(const show::TimelineFrame& f, double seconds) {
    const show::BeatGridView grid = show::beatGrid(f.track);
    const double start = f.trackSeconds - f.localSeconds;
    for (int beat = std::max(grid.beatAt(start + seconds), 0); beat < grid.count(); ++beat)
        if (grid.bar(beat) == 0 && grid.beatTime(beat) >= start + seconds)
            return grid.beatTime(beat) - start;
    return seconds;
}

double beatsAt(const show::TimelineFrame& f) {
    const show::BeatGridView grid = show::beatGrid(f.track);
    const int beat = grid.beatAt(f.trackSeconds);
    if (beat < 0 || beat + 1 >= grid.count())
        return double(std::max(beat, 0));
    const double at = grid.beatTime(beat), next = grid.beatTime(beat + 1);
    return double(beat) + std::clamp((f.trackSeconds - at) / std::max(next - at, 1e-6), 0.0, 1.0);
}

// --- SPHERE STORM's particles
// --------------------------------------------------------------------------------

std::uint32_t stormCount(SceneDetail detail) {
    const double count = std::llround(double(kStormSpheres) * double(detail.scale));
    return std::uint32_t(std::clamp(count, 64.0, double(kStormSpheres)));
}

float stormRise(const show::TimelineFrame& frame) {
    const SpheresDrop drop = spheresDrop(frame);
    const double t = frame.localSeconds - drop.seconds, bar = drop.barSeconds;
    if (t < 0.0)
        return 0.15f * smooth(float((t + 4.0 * bar) / (4.0 * bar)));
    return 0.15f + 0.85f * smooth(float(t / bar));
}

std::uint32_t stormEmissiveCount(SceneDetail detail) {
    return stormCount(detail) / (kStormSpheres / kStormEmissive);
}

std::array<float, 16> stormParams(const show::TimelineFrame& frame, SceneDetail detail) {
    const std::uint32_t count = stormCount(detail);
    std::array<float, 16> p{};
    p[0] = float(count);
    p[1] = float(stormEmissiveCount(detail));
    p[2] = std::min(4.f, std::cbrt(float(kStormSpheres) / float(count)));
    p[4] = std::clamp(frame.energy, 0.f, 1.f);
    p[5] = std::clamp(frame.kick, 0.f, 1.f);
    p[6] = stormRise(frame);
    const SpheresDrop drop = spheresDrop(frame);
    p[8] = float(drop.seconds);
    p[9] = float(drop.barSeconds);
    return p;
}

namespace {

rt::SphereRecord stormParticle(std::uint32_t id, float seconds, std::span<const float, 16> params);
rt::SphereRecord latticeParticle(std::uint32_t id, std::span<const float, 16> params);

float stormPhase(float x) {
    const float turns = x / kTau + 0.5f;
    return kTau * (turns - std::floor(turns) - 0.5f);
}

// Mirrors stormParticle() in shaders/modern/particles_spheres.comp operation for operation.
rt::SphereRecord stormParticle(std::uint32_t id, float seconds, std::span<const float, 16> params) {
    const auto emissiveCount = std::uint32_t(params[1]);
    const float radiusScale = params[2];
    const float energy = params[4], kick = params[5], rise = params[6];

    const std::uint32_t h0 = hashU(id * 0x9E3779B9u + 0x5bd1e995u);
    const std::uint32_t h1 = hashU(h0), h2 = hashU(h1), h3 = hashU(h2), h4 = hashU(h3), h5 = hashU(h4);
    const float u1 = unitOf(h1), u2 = unitOf(h2), u3 = unitOf(h3), u4 = unitOf(h4), u5 = unitOf(h5);

    float r, y, angle;
    bool iceRibbon = false;
    if (u1 < 0.72f) {
        const float f = u2 * u2 * (1.6f - 0.6f * u2);
        y = 0.35f + 11.f * f;
        r = 0.5f + 5.2f * f * f + (u3 - 0.5f) * (0.2f + 0.6f * f);
        y = lerp(0.12f + 0.3f * u3, y, rise);
        r = lerp(2.f + 8.f * f, r, rise);
        const float omega = 2.4f / (0.7f + r);
        const float ribbon = std::floor(6.f * u4) / 6.f;
        iceRibbon = ribbon == 0.f;
        const float x = std::max(seconds - params[8], 0.f) / params[9];
        const float twist = kTau * (x >= 1.f ? x - 0.5f : x * x * x - 0.5f * x * x * x * x);
        angle = stormPhase(kTau * (ribbon + 1.3f * f) + 0.24f * (u5 - 0.5f) + omega * seconds + twist);
        r *= 1.f + 0.16f * energy + 0.3f * kick * f;
        y += 0.22f * std::sin(stormPhase(3.f * angle + 1.3f * seconds + kTau * u5));
    } else {
        const float s = std::sqrt(u2);
        r = 1.6f + 9.4f * s;
        const float arm = u5 < 0.5f ? 0.f : 0.5f * kTau;
        const float spread = (u4 - 0.5f) * 1.1f;
        const float omega = 1.7f / (0.8f + 0.25f * r);
        angle = stormPhase(arm + 1.9f * std::log(r) + spread + omega * seconds);
        y = 0.1f + 0.55f * u3 * (1.f - 0.8f * s);
        r *= 1.f + 0.12f * kick;
    }

    rt::SphereRecord p{};
    p.center[0] = r * std::cos(angle);
    p.center[1] = y;
    p.center[2] = r * std::sin(angle);
    const bool glows = id < emissiveCount;
    const float size = glows ? 0.05f + 0.05f * u5 : 0.035f + 0.11f * u3 * u3;
    p.radius = size * radiusScale;
    Vec3 colour{1, 1, 1};
    float emission = 0.f;
    const std::uint32_t pick = h5 % 8u;
    if (glows) {
        colour = iceRibbon   ? Vec3{0.55f, 0.78f, 1.f}
                 : pick < 6u ? Vec3{1.f, 0.28f, 0.04f}
                             : Vec3{1.f, 0.5f, 0.16f};
        emission = (0.9f + 1.3f * u4) * (1.f + 1.5f * kick);
    } else if (iceRibbon) {
        colour = {0.82f, 0.9f, 1.f};
    }
    setColor(p.color, colour);
    p.material = 0;
    for (int k = 0; k < 3; ++k)
        p.prevCenter[k] = p.center[k];
    p.emission = emission;
    return p;
}

std::uint32_t latticeHash(int x, int y, int z) {
    return hashU(std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u ^
                 std::uint32_t(z) * 0xcb1ab31fu);
}

// Mirrors latticeParticle() in shaders/modern/particles_spheres.comp operation for operation.
rt::SphereRecord latticeParticle(std::uint32_t id, std::span<const float, 16> params) {
    const Vec3 camera{params[8], params[9], params[10]};
    const float spacing = params[11];
    const auto n = std::uint32_t(params[12]);
    const float radius = params[13], fadeStart = params[14], fadeEnd = params[15];
    const int half = int(n / 2u);
    const int cx = int(std::floor(camera.x / spacing)) + int(id % n) - half;
    const int cy = int(std::floor(camera.y / spacing)) + int((id / n) % n) - half;
    const int cz = int(std::floor(camera.z / spacing)) + int(id / (n * n)) - half;
    const Vec3 centre = Vec3{float(cx), float(cy), float(cz)} * spacing;
    const std::uint32_t h = latticeHash(cx, cy, cz);
    const float d = length(centre - camera);
    float fade = std::clamp((d - fadeStart) / (fadeEnd - fadeStart), 0.f, 1.f);
    fade = fade * fade * (3.f - 2.f * fade);
    rt::SphereRecord p{};
    setColor(p.center, centre);
    setColor(p.color, {1, 1, 1});
    float size = radius;
    if (h % 26u == 0u) { // a lantern
        const std::uint32_t hue = (h >> 8) % 4u;
        setColor(p.color, hue < 2u    ? Vec3{1.f, 0.42f, 0.1f}
                          : hue == 2u ? Vec3{1.f, 0.82f, 0.55f}
                                      : Vec3{0.45f, 0.72f, 1.f});
        p.emission = 18.f * std::clamp((30.f * params[4] - d) * 0.25f, 0.f, 1.f);
        size = 0.32f * radius;
    } else if (h % 11u == 0u) {
        setColor(p.color, {1.f, 0.8f, 0.42f});
    }
    p.radius = size * (1.f - fade);
    p.material = 0;
    setColor(p.prevCenter, centre);
    return p;
}

} // namespace

rt::SphereRecord spheresParticle(std::uint32_t id, float seconds, std::span<const float, 16> params) {
    return params[3] > 0.5f ? latticeParticle(id, params) : stormParticle(id, seconds, params);
}

std::uint32_t infinitySide(SceneDetail detail) {
    const double side = std::round(double(kInfinitySide) * std::cbrt(double(detail.scale)));
    return std::uint32_t(std::clamp(side, 4.0, double(kInfinitySide)));
}

std::array<float, 16> infinityParams(Vec3 eye, SceneDetail detail, double sinceDrop) {
    const std::uint32_t side = infinitySide(detail);
    std::array<float, 16> p{};
    p[0] = float(side * side * side);
    p[3] = 1.f;
    p[4] = float(std::clamp(sinceDrop, -1.0, 1000.0));
    p[8] = eye.x;
    p[9] = eye.y;
    p[10] = eye.z;
    p[11] = kInfinitySpacing;
    p[12] = float(side);
    p[13] = kInfinityRadius;
    p[15] = (0.5f * float(side) - 1.f) * kInfinitySpacing;
    p[14] = 0.3f * p[15];
    return p;
}

// --- RAYTRACE
// ------------------------------------------------------------------------------------------------

namespace {

// retro::raytracePhase: an angle reduced to [-pi, pi).
double retroPhase(double x) {
    constexpr double kTauD = 2 * std::numbers::pi;
    const double turns = x / kTauD + 0.5;
    return kTauD * (turns - std::floor(turns) - 0.5);
}

} // namespace

// retro::raytraceSpheres in double precision, then rounded to float.
std::array<RaytraceBall, kRaytraceRetroSpheres> raytraceRetroBalls(double seconds) {
    std::array<RaytraceBall, kRaytraceRetroSpheres> balls{};
    balls[0] = {{0.f, 1.3f, 0.f}, 1.3f};
    for (std::uint32_t i = 1; i < kRaytraceRetroSpheres; ++i) {
        const double angle = retroPhase(0.6 * seconds + double(i - 1) * 2 * std::numbers::pi / 5);
        const double radius = 0.6;
        const double bounce = std::abs(std::sin(retroPhase(1.7 * seconds + 1.3 * double(i))));
        balls[i] = {
            {float(3.0 * std::sin(angle)), float(radius + 0.9 * bounce), float(-3.0 * std::cos(angle))},
            float(radius)};
    }
    return balls;
}

Camera raytraceRetroCamera() {
    return {{0.f, 2.f, -8.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, 46.f};
}

std::array<RaytraceBall, kRaytraceSphereCount> raytraceBalls(const show::TimelineFrame& frame) {
    std::array<RaytraceBall, kRaytraceSphereCount> balls{};
    const std::array<RaytraceBall, kRaytraceRetroSpheres> retroBalls = raytraceRetroBalls(frame.localSeconds);
    std::copy(retroBalls.begin(), retroBalls.end(), balls.begin());
    // The extra balls rest until four bars before the drop, then hop on every beat, higher and higher; on
    // the drop all of them leap together and land on the next bar line; then each bounces on the beat (every
    // beat or every other one), harder with energy.
    const double beats = beatsAt(frame);
    const SpheresDrop drop = spheresDrop(frame);
    const double dropBeat = drop.beat >= 0 ? double(drop.beat) : 1e9;
    const float energy = std::clamp(frame.energy, 0.f, 1.f);
    for (std::uint32_t i = 0; i < kExtraBalls; ++i) {
        const ExtraBall& b = extras()[i];
        const float scale = 0.35f + 0.9f * b.radius;
        float height = 0.f;
        if (beats >= dropBeat + 4.0) {
            const double period = double(b.period);
            const auto p = float(std::fmod(beats - dropBeat + double(b.offset), period) / period);
            height = scale * (0.65f + 0.7f * energy) * 4.f * p * (1.f - p);
        } else if (beats >= dropBeat) {
            const auto p = float((beats - dropBeat) / 4.0);
            height = (2.4f + 2.f * b.radius) * 4.f * p * (1.f - p);
        } else if (beats >= dropBeat - 16.0) {
            const auto build = float((beats - (dropBeat - 16.0)) / 16.0);
            const auto p = float(beats - std::floor(beats));
            height = scale * (0.06f + 0.5f * build * build) * 4.f * p * (1.f - p);
        }
        balls[kRaytraceRetroSpheres + i] = {{b.x, b.radius + height, b.z}, b.radius};
    }
    return balls;
}

Camera spheresCamera(const show::TimelineFrame& frame) {
    switch (frame.effect) {
    case show::Effect::SphereStorm:
        return stormCamera(frame);
    case show::Effect::RaytraceInfinity:
        return infinityCamera(frame);
    default:
        return raytraceCamera(frame);
    }
}

float spheresExposureEV(show::Effect effect) {
    switch (effect) {
    case show::Effect::SphereStorm:
        return 0.5f;
    case show::Effect::RaytraceInfinity:
        return 0.f;
    default:
        return -1.f;
    }
}

// --- The scene
// -------------------------------------------------------------------------------------------------

Spheres::Spheres(SceneDetail detail) : detail_(detail) {
    static_.meshes.resize(kMeshCount);
    static_.meshes[kFloorMesh] = quad(kFloorSize, kFloorSize);
    static_.meshes[kSoftboxMesh] = quad(kSoftboxWidth, kSoftboxHeight);
    static_.meshes[kRimMesh] = quad(kRimWidth, kRimHeight);
    static_.meshes[kTileMesh] = bevelBox({kTileHalf, kTileHalfHeight, kTileHalf}, 0.025f, 2);

    std::vector<rt::Material>& m = static_.materials;
    m.resize(kMaterialCount);
    // 1993's red and white board: bevelled lacquered tiles near the camera, each a little different (a deep
    // smooth coat, a thinner one, a worn one), over this checker plane 4 cm down (seen in the grooves and
    // beyond the tiles).
    m[kBoard] = plastic({0.5f, 0.018f, 0.014f}, 0.35f);
    setColor(m[kBoard].baseColor2, {0.8f, 0.79f, 0.76f});
    m[kBoard].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Checker);
    m[kBoard].patternScale = 1.f;
    m[kBoard].coat = 1.f;
    m[kBoard].coatRoughness = 0.f;
    constexpr Vec3 kRed[3] = {{0.5f, 0.02f, 0.016f}, {0.42f, 0.016f, 0.014f}, {0.56f, 0.032f, 0.022f}};
    constexpr Vec3 kWhite[3] = {{0.8f, 0.79f, 0.76f}, {0.74f, 0.72f, 0.68f}, {0.83f, 0.82f, 0.79f}};
    constexpr float kTileRoughness[3] = {0.16f, 0.3f, 0.48f}, kTileCoat[3] = {1.f, 0.8f, 0.45f},
                    kTileCoatRoughness[3] = {0.f, 0.03f, 0.1f};
    for (std::uint32_t v = 0; v < kTileVariants; ++v) {
        m[kTileRed + v] = plastic(kRed[v], kTileRoughness[v]);
        m[kTileWhite + v] = plastic(kWhite[v], kTileRoughness[v]);
        for (const std::uint32_t k : {kTileRed + v, kTileWhite + v}) {
            m[k].coat = kTileCoat[v];
            m[k].coatRoughness = kTileCoatRoughness[v];
        }
    }
    // The board and its tiles receive the balls' caustics (one receiver, RAYTRACE's).
    for (std::uint32_t k = kBoard; k < kTileWhite + kTileVariants; ++k) {
        m[k].flags |= rt::kMaterialCausticReceiver;
        m[k].receiver = 0;
    }
    m[kChrome] = metal({0.96f, 0.96f, 0.97f}, 0.f);
    m[kGold] = metal({1.f, 0.76f, 0.33f}, 0.1f); // satin
    // Satin copper under a smooth clear coat.
    m[kCopper] = metal({0.95f, 0.6f, 0.47f}, 0.3f);
    m[kCopper].coat = 1.f;
    m[kCopper].coatRoughness = 0.f;
    m[kBrushedSteel] = metal({0.72f, 0.72f, 0.74f}, 0.22f);
    m[kBrushedSteel].pattern = static_cast<std::uint32_t>(rt::Pattern::BrushedMetal);
    m[kLacquer] = plastic({0.012f, 0.012f, 0.014f}, 0.4f); // black, under a deep clear coat
    m[kLacquer].coat = 1.f;
    m[kLacquer].coatRoughness = 0.f;
    m[kPearl] = plastic({0.86f, 0.84f, 0.8f}, 0.45f);
    m[kPearl].coat = 1.f;
    m[kPearl].coatRoughness = 0.02f;
    m[kGlass] = plastic({1.f, 1.f, 1.f}, 0.f);
    m[kGlass].transmission = 1.f;
    m[kGlass].ior = 1.5f;
    m[kGlass].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Glass);
    m[kSoftbox] = plastic({0.8f, 0.8f, 0.8f}, 0.9f);
    m[kSoftbox].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m[kSoftbox].emission, kSoftboxColor);
    m[kSoftbox].emissionScale = kSoftboxIntensity;
    m[kRim] = m[kSoftbox];
    setColor(m[kRim].emission, kRimColor);
    m[kRim].emissionScale = kRimIntensity;
    // Polished black stone in two blacks, 2-unit squares.
    m[kObsidian] = plastic({0.006f, 0.006f, 0.007f}, 0.f);
    setColor(m[kObsidian].baseColor2, {0.03f, 0.03f, 0.034f});
    m[kObsidian].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Checker);
    m[kObsidian].patternScale = 2.f;
    m[kParticleChrome] = metal({0.95f, 0.95f, 0.96f}, 0.f);

    // RAYTRACE's balls, one range each (each bounces on its own), centred on the origin: the instance
    // places it.
    for (std::uint32_t i = 0; i < kRaytraceSphereCount; ++i) {
        const BallLook look = ballLook(i);
        rt::SphereRecord s{};
        s.radius = look.radius;
        setColor(s.color, look.colour);
        s.material = look.material;
        static_.spheres.push_back(s);
        static_.sphereRanges.push_back({i, 1});
    }
    // The colossal spheres beyond the board, one range each.
    for (std::uint32_t g = 0; g < kRaytraceGiants; ++g) {
        rt::SphereRecord s{};
        s.radius = kGiants[g].radius;
        setColor(s.color, {1, 1, 1});
        s.material = kGiants[g].material;
        static_.sphereRanges.push_back({std::uint32_t(static_.spheres.size()), 1});
        static_.spheres.push_back(s);
    }
    static_.capacity.instances =
        2 + kRaytraceSphereCount + kRaytraceGiants + kRaytraceTileSide * kRaytraceTileSide + 2;
    static_.capacity.spheres = std::uint32_t(static_.spheres.size());
    const std::uint32_t side = infinitySide(detail);
    static_.capacity.particles = std::max(stormCount(detail), side * side * side);
    static_.capacity.treeLights = stormEmissiveCount(detail);
    static_.capacity.causticResolution = kRaytraceReceiverResolution;
    static_.capacity.mediumGrid = kCirrusGrid;
    static_.particleShader = "modern_particles_spheres.comp.spv";
    static_.mediumShader = "modern_spheres_medium.comp.spv";
}

SceneFrame Spheres::frame(const show::TimelineFrame& frame) const {
    switch (frame.effect) {
    case show::Effect::SphereStorm:
        return stormFrame(frame);
    case show::Effect::RaytraceInfinity:
        return infinityFrame(frame);
    default:
        return raytraceFrame(frame);
    }
}

SceneFrame Spheres::raytraceFrame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    f.camera = spheresCamera(frame);
    f.exposureEV = spheresExposureEV(show::Effect::Raytrace);
    // The sunlit board is about 1.5: sun -> chrome -> board paths (caustics through 48 balls) would speckle
    // at the default.
    f.clampIndirect = 2.f;
    const SpheresDrop drop = spheresDrop(frame);
    const float widen = smooth(float((frame.localSeconds - drop.seconds) / (0.25 * drop.barSeconds)));
    f.environment = raytraceSky(kRaytraceSunRadius + kRaytraceSunWiden * widen);
    f.medium = raytraceMedium(frame);
    f.instances.push_back({kFloorMesh, kBoard, floorUnder(f.camera.eye, -0.04f)});
    const std::array<RaytraceBall, kRaytraceSphereCount> balls = raytraceBalls(frame);
    for (std::uint32_t i = 0; i < kRaytraceSphereCount; ++i)
        f.instances.push_back(sphereInstance(i, 1, static_.spheres[i].material, translate(balls[i].centre)));
    const auto t = float(frame.localSeconds);
    for (std::uint32_t g = 0; g < kRaytraceGiants; ++g) {
        const Vec3 at =
            kGiants[g].centre + Vec3{0.f, 30.f + 30.f * std::sin(0.23f * t + 2.1f * float(g)), 0.f};
        f.instances.push_back(
            sphereInstance(kRaytraceSphereCount + g, 1, kGiants[g].material, translate(at)));
    }
    // The tiles round a point 12 units ahead of the camera.
    const CameraBasis basis = cameraBasis(f.camera);
    const Vec3 ahead =
        f.camera.eye + normalize(Vec3{basis.forward.x, 0.f, basis.forward.z} + Vec3{0.f, 0.f, 1e-4f}) * 12.f;
    const int ax = int(std::floor(ahead.x)), az = int(std::floor(ahead.z));
    constexpr int kHalfSide = int(kRaytraceTileSide / 2);
    for (int j = az - kHalfSide; j < az + kHalfSide; ++j)
        for (int i = ax - kHalfSide; i < ax + kHalfSide; ++i) {
            const std::uint32_t h = hashU(std::uint32_t(i) * 0x8da6b343u ^ std::uint32_t(j) * 0xcb1ab31fu);
            const bool white = ((i + j) & 1) == 0;
            const std::uint32_t material = (white ? kTileWhite : kTileRed) + h % kTileVariants;
            const float axis = kTau * unitOf(hashU(h + 1u)), tilt = 0.006f * unitOf(hashU(h + 2u));
            const float sink = 0.003f * unitOf(hashU(h + 3u));
            const rt::Mat34 place =
                compose(translate({float(i) + 0.5f, -kTileHalfHeight - sink, float(j) + 0.5f}),
                        rotate({std::cos(axis), 0.f, std::sin(axis)}, tilt));
            f.instances.push_back({kTileMesh, material, place});
        }
    // The cool fill and the warm rim.
    const struct {
        std::uint32_t mesh, material;
        Vec3 at;
        float width, height;
    } lights[2] = {{kSoftboxMesh, kSoftbox, kSoftboxA, kSoftboxWidth, kSoftboxHeight},
                   {kRimMesh, kRim, kRimLight, kRimWidth, kRimHeight}};
    for (const auto& l : lights) {
        const rt::Mat34 place = facing(l.at, kSoftboxAim);
        SceneInstance box{l.mesh, l.material, place};
        box.light = std::uint32_t(f.lights.size());
        f.instances.push_back(box);
        const rt::Material& m = static_.materials[l.material];
        f.lights.push_back(rectLightForQuad(place, l.width, l.height, rt::LightKind::Emitter,
                                            colorOf(m.emission) * m.emissionScale, kSoftboxWeight));
    }
    // The sun's caustics through the balls (the glass ones focus it, the chrome ones throw its reflections)
    // as a photon map on the board round them: sharp, and none of the sun -> ball -> board grain.
    CausticFrame c;
    c.receivers = {receiverForQuad(compose(translate({0.f, 0.f, 0.f}), rotate({1.f, 0.f, 0.f}, -0.5f * kPi)),
                                   kRaytraceReceiverSide, kRaytraceReceiverSide, kRaytraceReceiverResolution)};
    c.photons = raytracePhotons(detail_);
    c.light = -1;
    c.casterInstanceFirst = 1;
    c.casterInstanceCount = kRaytraceSphereCount;
    f.caustics = c;
    return f;
}

SceneFrame Spheres::stormFrame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    f.camera = spheresCamera(frame);
    // Per shot: the first two look into the sun's sliver on the horizon, so they are exposed down.
    constexpr float kShotEV[4] = {-0.2f, -0.1f, 0.2f, 0.15f};
    f.exposureEV = kShotEV[std::min<std::uint32_t>(f.camera.shot, 3)];
    f.clampIndirect = 16.f;
    f.environment = stormSky();
    f.instances.push_back({kFloorMesh, kObsidian, floorUnder(f.camera.eye)});
    // High above every camera's frame: it fills the chrome from the cool side, unseen.
    const rt::Mat34 fill = compose(facing({-9.f, 30.f, 24.f}, {0.f, 4.f, 0.f}), scale({3.f, 3.f, 3.f}));
    SceneInstance box{kSoftboxMesh, kSoftbox, fill};
    box.light = 0;
    f.instances.push_back(box);
    const rt::Material& m = static_.materials[kSoftbox];
    f.lights.push_back(rectLightForQuad(fill, kSoftboxWidth, kSoftboxHeight, rt::LightKind::Emitter,
                                        colorOf(m.emission) * m.emissionScale, 1.f));
    ParticleFrame p;
    p.count = stormCount(detail_);
    p.params = stormParams(frame, detail_);
    p.material = kParticleChrome;
    f.particles = p;
    SceneInstance field = sphereInstance(0, p.count, kParticleChrome, identity());
    field.dynamicSpheres = true;
    // The glowing particles (the field's first) are the light tree's lights, from the shader's C++ mirror.
    rt::RectLight tree{};
    tree.kind = static_cast<std::uint32_t>(rt::LightKind::Tree);
    tree.selectWeight = 2.f;
    tree.gate = rt::kNoLight;
    field.light = std::uint32_t(f.lights.size());
    f.lights.push_back(tree);
    f.instances.push_back(field);
    const std::uint32_t glowing = stormEmissiveCount(detail_);
    f.treeLights.reserve(glowing);
    const auto seconds = float(frame.localSeconds);
    for (std::uint32_t k = 0; k < glowing; ++k) {
        const rt::SphereRecord s = spheresParticle(k, seconds, p.params);
        f.treeLights.push_back({colorOf(s.center), s.radius, colorOf(s.color) * s.emission, 1.f, k});
    }
    return f;
}

SceneFrame Spheres::infinityFrame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    f.camera = spheresCamera(frame);
    // Per shot: looking down (shots 0 and 2) the camera looks into the haze it falls towards; opened up.
    constexpr float kShotEV[3] = {0.3f, 0.f, 0.6f};
    f.exposureEV = kShotEV[std::min<std::uint32_t>(f.camera.shot, 2)];
    f.clampIndirect = 16.f;
    f.environment = infinitySky();
    // Depth: a dusk-lit haze through the lattice, so the halls recede in steps of air and the lanterns glow
    // in it. A height fog based at the camera, thickening below it and thinning above (an endless homogeneous
    // fog would let no light in from the sky at all): the depths the camera falls into are haze, the low sun
    // still reaches it.
    MediumFrame medium;
    FogDesc fog;
    fog.density = kInfinityFog;
    fog.baseHeight = f.camera.eye.y;
    fog.falloff = kInfinityFogFalloff;
    fog.albedo = {0.55f, 0.58f, 0.66f}; // a cool, dim haze: the dark channels between the spheres stay dark
    fog.phaseG = 0.4f;
    medium.fog = fog;
    f.medium = medium;
    // The lattice round the camera, recycled as it falls.
    ParticleFrame p;
    p.params = infinityParams(f.camera.eye, detail_, frame.localSeconds - spheresDrop(frame).seconds);
    p.count = std::uint32_t(p.params[0]);
    p.material = kParticleChrome;
    f.particles = p;
    SceneInstance field = sphereInstance(0, p.count, kParticleChrome, identity());
    field.dynamicSpheres = true;
    f.instances.push_back(field);
    return f;
}

// --- The epoch cards
// ----------------------------------------------------------------------------------------------

} // namespace evo::modern

#include "render/stage_plan.hpp" // kExportMaxBounces: the bounces an export traces

namespace evo::modern {

std::string Spheres::nowCard(show::Effect effect) const {
    switch (effect) {
    case show::Effect::Raytrace:
        return std::string("2026") + kCardDot + "REAL-TIME PATH TRACING" + kCardDot +
               groupThousands(kRaytraceSphereCount + kRaytraceGiants) + " SPHERES" + kCardDot +
               groupThousands(kShowFps) + " FPS";
    case show::Effect::SphereStorm:
        return std::string("2026") + kCardDot + groupThousands(kStormSpheres) + " SPHERES" + kCardDot +
               groupThousands(kStormEmissive) + " OF THEM LIGHTS" + kCardDot + "LIGHT TREE" + kCardDot + "MOTION BLUR";
    case show::Effect::RaytraceInfinity:
        return std::string("2026") + kCardDot + groupThousands(kInfinitySpheres) + " MIRROR SPHERES" + kCardDot +
               "INFINITE REFLECTIONS" + kCardDot + groupThousands(render::kExportMaxBounces) + " BOUNCES";
    default:
        return {};
    }
}

} // namespace evo::modern
