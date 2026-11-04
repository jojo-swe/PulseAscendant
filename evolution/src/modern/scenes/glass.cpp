// The Glass scene: GLENZ, GLASS CATHEDRAL and CRYSTAL CITY (see glass.hpp). The glenz star is Future Crew's
// mesh (Second Reality, GLENZ/MAIN.C, Unlicense; licenses/FUTURE-CREW-NOTICE.md).

#include "modern/scenes/glass.hpp"

#include "modern/mesh_builder.hpp"
#include "retro/city.hpp"
#include "show/music_curves.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>

namespace evo::modern {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegrees = kPi / 180.f;

// --- Small maths ---------------------------------------------------------------------------------------------

float clamp01(float x) {
    return std::clamp(x, 0.f, 1.f);
}

float smooth(float x) {
    x = clamp01(x);
    return x * x * (3.f - 2.f * x);
}

Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

Vec3 catmullRom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.f * p1) + (p2 - p0) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                   (3.f * p1 - p0 - 3.f * p2 + p3) * t3);
}

struct Key {
    Vec3 eye, target;
};

// The keys at s in [0, 1] along a Catmull-Rom spline through them (the ends repeated).
template <std::size_t N> Key alongKeys(const std::array<Key, N>& keys, float s) {
    static_assert(N >= 2);
    const float x = clamp01(s) * float(N - 1);
    const std::size_t i = std::min(static_cast<std::size_t>(x), N - 2);
    const float t = x - float(i);
    auto at = [&](std::ptrdiff_t k) {
        return keys[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(k, 0, std::ptrdiff_t(N) - 1))];
    };
    const auto j = static_cast<std::ptrdiff_t>(i);
    return {catmullRom(at(j - 1).eye, at(j).eye, at(j + 1).eye, at(j + 2).eye, t),
            catmullRom(at(j - 1).target, at(j).target, at(j + 1).target, at(j + 2).target, t)};
}

Camera makeCamera(Key k, float fov, float aperture, float focus, std::uint32_t shot) {
    Camera c;
    c.eye = k.eye;
    c.target = k.target;
    c.up = {0, 1, 0};
    c.fovYDegrees = fov;
    c.aperture = aperture;
    c.focusDistance = focus > 0 ? focus : length(k.target - k.eye);
    c.shot = shot;
    return c;
}

std::uint32_t hashU(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

float hash01(std::uint32_t x) {
    return float(hashU(x) >> 8) * (1.f / 16777216.f);
}

rt::Mat34 place(Vec3 position, const rt::Mat34& rotation, Vec3 size) {
    return compose(translate(position), compose(rotation, scale(size)));
}

rt::Mat34 place(Vec3 position, Vec3 size) {
    return compose(translate(position), scale(size));
}

// The rotation that turns unit vector a onto unit vector b.
rt::Mat34 rotationFromTo(Vec3 a, Vec3 b) {
    const Vec3 axis = cross(a, b);
    const float c = std::clamp(dot(a, b), -1.f, 1.f);
    if (length(axis) < 1e-6f) {
        if (c > 0)
            return identity();
        const Vec3 other = std::abs(a.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        return rotate(cross(a, other), kPi);
    }
    return rotate(axis, std::acos(c));
}

// --- The beat grid: shots cut on bar lines --------------------------------------------------------------------

// The bar line (a downbeat of the chapter's track) nearest `local` seconds into the chapter, in seconds into
// the chapter; `local` itself where the grid has no downbeat within 16 beats. A fixed fact of the chapter.
float barLineNear(const show::TimelineFrame& f, double local) {
    const double start = show::chapterStart(f.chapter);
    const int track = show::trackAt(start);
    const show::BeatGridView grid = show::beatGrid(track);
    const double offset = start - show::trackStart(track);
    const int at = std::max(grid.beatAt(offset + local), 0);
    double best = local, distance = 1e30;
    for (int i = std::max(at - 16, 0); i <= std::min(at + 16, grid.count() - 1); ++i) {
        if (grid.bar(i) != 0)
            continue;
        const double t = grid.beatTime(i) - offset;
        if (std::abs(t - local) < distance) {
            distance = std::abs(t - local);
            best = t;
        }
    }
    return static_cast<float>(best);
}

// The length of the bar that starts at the bar line nearest `local` (to the next downbeat), in seconds; two
// seconds where the grid has none.
float barLength(const show::TimelineFrame& f, double local) {
    const double start = show::chapterStart(f.chapter);
    const int track = show::trackAt(start);
    const show::BeatGridView grid = show::beatGrid(track);
    const double offset = start - show::trackStart(track);
    const double line = offset + barLineNear(f, local);
    const int at = std::max(grid.beatAt(line + 1e-6), 0);
    for (int i = at + 1; i < std::min(at + 17, grid.count()); ++i)
        if (grid.bar(i) == 0)
            return static_cast<float>(grid.beatTime(i) - line);
    return 2.f;
}

// The downbeat at or before `local` seconds into the chapter (the chapter's D for its accent), in seconds into
// the chapter; `local` where there is none within 16 beats.
float downbeatBefore(const show::TimelineFrame& f, double local) {
    const double start = show::chapterStart(f.chapter);
    const int track = show::trackAt(start);
    const show::BeatGridView grid = show::beatGrid(track);
    const double offset = start - show::trackStart(track);
    const int at = grid.beatAt(offset + local + 1e-6);
    for (int i = at; i >= std::max(at - 16, 0); --i)
        if (grid.bar(i) == 0)
            return static_cast<float>(grid.beatTime(i) - offset);
    return static_cast<float>(local);
}

// The bar lines passed since `local` seconds into the chapter, with the last one eased in over a beat: a
// smooth staircase of the bar count.
float barsSince(const show::TimelineFrame& f, double local) {
    const double start = show::chapterStart(f.chapter);
    const int track = show::trackAt(start);
    const show::BeatGridView grid = show::beatGrid(track);
    const double offset = start - show::trackStart(track);
    const int from = grid.beatAt(offset + local + 1e-6), to = grid.beatAt(offset + f.localSeconds + 1e-6);
    float bars = 0.f;
    for (int i = std::max(from + 1, 0); i <= to; ++i)
        if (grid.bar(i) == 0) {
            const double since = offset + f.localSeconds - grid.beatTime(i);
            bars += smooth(float(since / std::max(0.05, double(f.beatSeconds))));
        }
    return bars;
}

// Whole beats from `local` seconds into the chapter to the frame's time (negative before it).
int beatsSince(const show::TimelineFrame& f, double local) {
    const double start = show::chapterStart(f.chapter);
    const int track = show::trackAt(start);
    const show::BeatGridView grid = show::beatGrid(track);
    const double offset = start - show::trackStart(track);
    if (f.localSeconds < local)
        return -1;
    return grid.beatAt(offset + f.localSeconds + 1e-6) - grid.beatAt(offset + local + 1e-6);
}

// The local time of the j-th beat at or after `local` seconds into the chapter (j = 0: the next beat, or
// `local` itself on a beat).
float beatAfter(const show::TimelineFrame& f, double local, int j) {
    const double start = show::chapterStart(f.chapter);
    const int track = show::trackAt(start);
    const show::BeatGridView grid = show::beatGrid(track);
    const double offset = start - show::trackStart(track);
    int at = grid.beatAt(offset + local - 1e-6) + 1; // the first beat at or after local
    at = std::clamp(at + j, 0, grid.count() - 1);
    return static_cast<float>(grid.beatTime(at) - offset);
}

// The show's four lenses (36 mm equivalents) as vertical fields of view at 16:9.
constexpr float kLens24 = 45.4f, kLens35 = 31.7f, kLens50 = 23.1f, kLens85 = 13.6f;

// --- Materials ----------------------------------------------------------------------------------------------

void setColor(float out[3], Vec3 c) {
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
}

rt::Material pbr(Vec3 color, float roughness) {
    rt::Material m;
    setColor(m.baseColor, color);
    m.roughness = roughness;
    return m;
}

rt::Material stone(Vec3 a, Vec3 b, float roughness, float joints) {
    rt::Material m = pbr(a, roughness);
    setColor(m.baseColor2, b);
    m.pattern = static_cast<std::uint32_t>(rt::Pattern::Concrete);
    m.patternScale = joints;
    return m;
}

// A smooth dielectric: absorption per metre (Beer-Lambert), IOR, Abbe number (0: no dispersion).
rt::Material glass(Vec3 absorption, float ior, float abbe) {
    rt::Material m;
    setColor(m.baseColor, {1, 1, 1});
    setColor(m.baseColor2, absorption);
    m.roughness = 0.f;
    m.ior = ior;
    m.transmission = 1.f;
    m.abbe = abbe;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Glass);
    return m;
}

// Stained glass: a thin-walled pane of a colour.
rt::Material pane(Vec3 color) {
    rt::Material m = glass({0, 0, 0}, 1.52f, 0.f);
    setColor(m.baseColor, color);
    setColor(m.baseColor2, {0, 0, 0});
    m.flags = rt::kMaterialThinWalled;
    return m;
}

rt::Material emitter(Vec3 color, float scale) {
    rt::Material m = pbr({0.f, 0.f, 0.f}, 1.f);
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m.emission, color);
    m.emissionScale = scale;
    return m;
}

Vec3 emitted(const rt::Material& m) {
    return Vec3{m.emission[0], m.emission[1], m.emission[2]} * m.emissionScale;
}

rt::Material receiverOf(rt::Material m) {
    m.flags |= rt::kMaterialCausticReceiver;
    m.receiver = 0;
    return m;
}

enum Material : std::uint32_t {
    // GLENZ
    kFlint,
    kCobalt,
    kCycFloor, // the receiver
    kCyc,
    kKeyEmitter,
    kKeySoftEmitter,
    kSoftbox,
    kScrim,
    kTubeRed,
    kTubeGold,
    kTubeGreen,
    kFixture,
    kWash,
    // GLASS CATHEDRAL
    kFlagstones, // receiver 0
    kLimestone,
    kLimestoneWest, // receiver 1
    kShutterWood,
    kPillarGlass,
    kLead,
    kLampEmitter,
    kIron,
    kPaneFirst, // kPaneColours stained-glass colours
    // CRYSTAL CITY
    kQuartz = kPaneFirst + 6,
    kAmberCore,
    kPaving, // the receiver
    kPavingOutside,
    kMaterialCount
};
constexpr std::uint32_t kPaneColours = 6;
// The sun's family only (amber, red, white): the sky's blue is the cool side.
constexpr std::array<Vec3, kPaneColours> kPaneTint = {{
    {0.95f, 0.55f, 0.08f}, // amber
    {0.80f, 0.06f, 0.05f}, // ruby
    {0.90f, 0.87f, 0.78f}, // pale (the grisaille)
    {0.98f, 0.78f, 0.25f}, // gold
    {0.55f, 0.03f, 0.06f}, // crimson
    {0.96f, 0.96f, 0.95f}, // clear
}};

// --- GLENZ's set ----------------------------------------------------------------------------------------------

// The key light: a small strong square out to the right of the star and a little in front, 25 degrees up from
// it (its photons make the caustics). Low enough that the star throws its caustic a long way across the floor
// to its left, where the locked camera sees it fan out in spectra beside the star, and the floor shot looks
// along the fan. The hard spot is small (0.3 degrees across from the star), so the caustic keeps the flint's
// spectra apart; the soft box around it (before the accent) blurs them.
constexpr float kKeySize = 0.025f, kKeySoftSize = 0.32f;
constexpr Vec3 kKeyAt{4.0f, 2.9f, -1.4f}, kKeyAim{0.f, 0.9f, 0.f};
constexpr Vec3 kCausticAt{-1.7f, 0.f, 0.8f}; // the middle of the caustic's fan with the star at rest
// The key's colour temperature on the beats after D: about 3200 K and 7500 K.
constexpr Vec3 kKeyWarm{1.f, 0.78f, 0.56f}, kKeyCool{0.78f, 0.88f, 1.f};
constexpr float kKeyRadiance = 150000.f;
constexpr float kKeySoftRadiance = kKeyRadiance * (kKeySize * kKeySize) / (kKeySoftSize * kKeySoftSize);
constexpr Vec3 kKeyColor{1.f, 0.96f, 0.9f};
// The key's snoot: an octagonal black tube, the inradius of its opening, its walls' thickness and its length
// (a pool of light about 4 m across round the star; the floor beyond it dark).
constexpr float kSnootOpening = 0.42f, kSnootWall = 0.02f, kSnootLength = 0.9f;
// The strip softboxes behind the star on either side (the bright edges on the facets), and the scrim over the
// stage (what the star's upper facets mirror: without it they show the black above the set).
constexpr float kSoftboxWidth = 0.6f, kSoftboxHeight = 2.4f;
constexpr float kSoftboxRadiance = 1.6f;
constexpr Vec3 kScrimAt{0.f, 5.2f, 0.6f};
constexpr float kScrimSize = 4.f, kScrimRadiance = 0.6f;
// The copper drum: six tubes round an axis along x, at the 1993 copper's angles (copper.comp).
constexpr std::uint32_t kTubes = 6;
constexpr float kTubeWidth = 7.f, kTubeHeight = 0.045f;
constexpr float kTubeRadiance = 5.f;
// The cyc wash: a cool strip on the floor at the foot of the cove, hidden behind a low ground row, lighting the
// back wall from below (the backdrop the star refracts).
constexpr float kWashWidth = 14.f, kWashHeight = 0.25f, kWashRadiance = 40.f;
constexpr int kWashUnits = 7;
constexpr float kDrumY = 1.3f, kDrumZ = 2.7f, kDrumRadiusY = 0.95f, kDrumRadiusZ = 0.55f;
// The cyclorama: the receiver square of the floor, the rest of the floor, the cove up to the back wall.
// The receiver: the stage floor round the star and behind it, as far as the facets throw their spectra (7 m
// square: 6.8 mm texels in the window, 3.4 mm in an export).
constexpr float kReceiverHalf = 3.5f;
constexpr Vec3 kReceiverAt{0.f, 0.f, 1.6f};
constexpr float kFloorHalf = 14.f, kFloorFront = -14.f, kCoveStart = 6.f,
                kCoveRadius = 2.f, kWallTop = 10.f;
constexpr std::uint32_t kGlenzResolution = 1024; // x kExportMapFactor in an export

enum GlenzLight : std::uint32_t {
    kKeyLight,
    kKeySoftLight,
    kSoftLeft,
    kSoftRight,
    kScrimLight,
    kFlatLight,
    kWashLight, // kWashUnits of them
    kTubeLight0 = kWashLight + kWashUnits
};

// --- GLASS CATHEDRAL's set ------------------------------------------------------------------------------------

// A hall of four rows of 16 glass pillars under a pointed vault, 21 m wide and 60 m long, the west door at
// z = 0 and the rose window at z = kNaveLength; a lancet in every bay of both walls.
constexpr float kNaveHalf = 10.5f, kNaveLength = 60.f, kWallThickness = 0.8f, kSpringHeight = 12.6f;
constexpr float kFirstPillarZ = 5.f, kBay = 3.4f;
constexpr int kBays = 16; // pillars per row; windows between them
constexpr int kWindows = kBays - 1;
constexpr std::array<float, 4> kPillarRows = {-8.f, -3.6f, 3.6f, 8.f};
// A pillar: a clustered gothic pier of light flint glass, a faceted octagonal core with four shafts on its axes
// and four slender ones between them, from its moulded stone base (kPlinth) to its capital (kPillarTop). Every
// facet is a prism: the sun through a pier breaks into spectra.
constexpr float kPlinth = 0.6f;
constexpr float kPillarTop = 7.23f; // the capital's underside
constexpr float kPierCore = 0.27f;  // the octagon's inradius
constexpr float kPierShaft = 0.15f, kPierShaftAt = 0.29f; // the four shafts: radius, distance from the axis
constexpr float kPierFillet = 0.075f, kPierFilletAt = 0.33f; // the four between them
constexpr int kPierFacets = 32;
constexpr float kWindowWidth = 2.2f, kWindowBottom = 3.f, kWindowTop = 9.6f;
constexpr int kWindowCols = 4, kWindowRows = 12;
constexpr float kRoseRadius = 4.f, kRoseY = 16.f, kGableTop = 26.f;
constexpr std::uint32_t kCathedralResolution = 512; // x kExportMapFactor in an export
// Sodium lamps hanging over the nave, one per bay.
constexpr Vec3 kLampColor{1.f, 0.52f, 0.18f};
constexpr float kLampRadiance = 25.f, kLampSize = 0.36f, kLampHeight = 9.5f;
constexpr int kWindowVariants = 4;
// The east windows' shutters (outside the glass): bays 0 and 1 are open from the start, 2 and 3 stay shut,
// kHeroBay opens on the accent's downbeat (the sun's disc appears in it), and bays kHeroBay + 1 .. 14 open
// one per beat from the accent: the sun walks down the nave and reaches pillar after pillar.
constexpr int kHeroBay = 4;
constexpr float kShutterTravel = 7.4f, kShutterSeconds = 0.35f;
// The haze: sigma_t per metre at the floor, halving every 14 m up.
constexpr float kHazeDensity = 0.018f, kHazeFalloff = 0.05f;
// Towards the sun: from the east (+x), 29 degrees up, a little towards the door. Its ray through the middle of
// the hero window passes between the piers of both eastern rows, so the disc reads through the glass from
// the nave.
constexpr Vec3 kCathedralSun{0.80f, 0.47f, -0.29f};

float pillarZ(int k) {
    return kFirstPillarZ + kBay * float(k);
}
float windowZ(int k) { // between pillars k and k + 1
    return pillarZ(k) + 0.5f * kBay;
}

// --- CRYSTAL CITY's set --------------------------------------------------------------------------------------

constexpr float kCityScale = 0.3f; // metres per retro city unit
constexpr std::int32_t kLotI0 = -5, kLotI1 = 4, kLotM0 = 0, kLotM1 = 17;
constexpr std::uint32_t kCrystalResolution = 512; // x kExportMapFactor in an export
// The plaza: the four lots round the main avenue's fifth crossing are open paving, a 26 m square (the
// receiver, 5 cm texels in the window) with the glenz sculpture at its centre.
constexpr Vec3 kPlaza{0.f, 0.f, 63.f};
constexpr float kPlazaHalf = 13.f;
constexpr float kSculptureScale = 2.f, kSculptureLift = 3.65f;
constexpr std::uint32_t kCrystalVariants = 3;

// --- Meshes ---------------------------------------------------------------------------------------------------

enum Mesh : std::uint32_t {
    kGlenzStar,
    kQuad,
    kUnitBox,
    kCove,
    kPillarShaft,
    kPier,
    kPierBase,
    kPierCapital,
    kArcadeArch,
    kVault,
    kVaultRib,
    kCame,
    kTracery,
    kRose,         // the rose window's panes: kPaneColours meshes from here
    kRoseLead = kRose + kPaneColours,
    kRoseStone,
    kWindowPanes, // kWindowVariants x kPaneColours meshes from here
    kCrystal = kWindowPanes + kWindowVariants * kPaneColours, // kCrystalVariants meshes
    kLampHousing = kCrystal + kCrystalVariants,
    kMeshCount
};

void vertex(rt::Mesh& m, Vec3 p, Vec3 n, Vec2 uv) {
    rt::Vertex v{};
    v.position[0] = p.x;
    v.position[1] = p.y;
    v.position[2] = p.z;
    v.normal[0] = n.x;
    v.normal[1] = n.y;
    v.normal[2] = n.z;
    v.uv[0] = uv.x;
    v.uv[1] = uv.y;
    m.vertices.push_back(v);
}

// A flat triangle wound counter-clockwise seen from the side `outward` points to.
void flatTriangle(rt::Mesh& m, Vec3 a, Vec3 b, Vec3 c, Vec3 outward) {
    Vec3 n = cross(b - a, c - a);
    const float len = length(n);
    if (!(len > 1e-12f))
        return;
    n = n / len;
    if (dot(n, outward) < 0) {
        std::swap(b, c);
        n = -n;
    }
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    vertex(m, a, n, {0, 0});
    vertex(m, b, n, {1, 0});
    vertex(m, c, n, {0, 1});
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2});
}

void flatQuad(rt::Mesh& m, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 outward) {
    flatTriangle(m, a, b, c, outward);
    flatTriangle(m, a, c, d, outward);
}

// A box between two corners as flat quads, appended (for meshes made of many boxes).
void addBox(rt::Mesh& m, Vec3 lo, Vec3 hi) {
    const Vec3 c = (lo + hi) * 0.5f;
    const std::array<Vec3, 8> p = {{{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                                    {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}}};
    const int faces[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    for (const auto& f : faces) {
        const Vec3 centre = (p[f[0]] + p[f[1]] + p[f[2]] + p[f[3]]) * 0.25f;
        flatQuad(m, p[f[0]], p[f[1]], p[f[2]], p[f[3]], centre - c);
    }
}

// The glenz star in its model space (retro units), flat-faceted, wound outwards (the 1993 winding points the
// right-handed normal inwards: each face is turned to face away from the centre).
rt::Mesh glenzStar() {
    const retro::GlenzMesh& g = retro::glenzMesh();
    rt::Mesh m;
    for (const auto& t : g.triangles) {
        auto at = [&](int i) {
            const auto& v = g.vertices[static_cast<std::size_t>(i)];
            return Vec3{float(v[0]), float(v[1]), float(v[2])};
        };
        const Vec3 a = at(t[0]), b = at(t[1]), c = at(t[2]);
        flatTriangle(m, a, b, c, (a + b + c) * (1.f / 3.f));
    }
    return m;
}

// A surface of revolution about y with `sides` flat facets around: profile points (radius, y), bottom to
// top. Facets face away from the axis (or down and up at caps).
rt::Mesh facetedLathe(std::span<const Vec2> profile, int sides, float phase) {
    rt::Mesh m;
    for (std::size_t k = 0; k + 1 < profile.size(); ++k) {
        const Vec2 p0 = profile[k], p1 = profile[k + 1];
        for (int s = 0; s < sides; ++s) {
            const float a0 = phase + 2.f * kPi * float(s) / float(sides);
            const float a1 = phase + 2.f * kPi * float(s + 1) / float(sides);
            const float am = 0.5f * (a0 + a1);
            auto at = [](Vec2 p, float a) { return Vec3{p.x * std::cos(a), p.y, p.x * std::sin(a)}; };
            const Vec3 radial{std::cos(am), 0, std::sin(am)};
            // The profile's outward normal in (r, y): (dy, -dr).
            const Vec3 outward = radial * (p1.y - p0.y) + Vec3{0, 1, 0} * (p0.x - p1.x);
            const Vec3 a = at(p0, a0), b = at(p0, a1), c = at(p1, a1), d = at(p1, a0);
            if (p0.x > 0 && p1.x > 0)
                flatQuad(m, a, b, c, d, outward);
            else if (p0.x > 0)
                flatTriangle(m, a, b, c, outward); // c == d on the axis
            else if (p1.x > 0)
                flatTriangle(m, a, c, d, outward);
        }
    }
    return m;
}

// A profile in the (z, y) plane swept along x from -halfWidth to +halfWidth, with smooth normals from the
// profile (towards `inside`, a point of the (z, y) plane the surface faces).
rt::Mesh extrudeAlongX(std::span<const Vec2> profile, float halfWidth, Vec2 inside) {
    rt::Mesh m;
    for (std::size_t k = 0; k + 1 < profile.size(); ++k) {
        const Vec2 p0 = profile[k], p1 = profile[k + 1];
        const Vec2 mid{0.5f * (p0.x + p1.x), 0.5f * (p0.y + p1.y)};
        const Vec3 towards{0, inside.y - mid.y, inside.x - mid.x};
        flatQuad(m, {-halfWidth, p0.y, p0.x}, {halfWidth, p0.y, p0.x}, {halfWidth, p1.y, p1.x},
                 {-halfWidth, p1.y, p1.x}, towards);
    }
    return m;
}

// A pointed (gothic) arch's centre line in the (x, y) plane: span from x = -span / 2 to +span / 2, springing
// at y = 0, each half an arc of radius sharpness * span centred on the other half's side; `segments` per half.
std::vector<Vec2> pointedArch(float span, float sharpness, int segments) {
    const float r = sharpness * span;
    const float cx = -0.5f * span + r; // the left half's centre (on the right)
    const float apexAngle = std::acos(std::clamp((0.f - cx) / r, -1.f, 1.f));
    std::vector<Vec2> left;
    for (int i = 0; i <= segments; ++i) {
        const float a = kPi + (apexAngle - kPi) * float(i) / float(segments);
        left.push_back({cx + r * std::cos(a), r * std::sin(a)});
    }
    std::vector<Vec2> points = left;
    for (int i = segments - 1; i >= 0; --i)
        points.push_back({-left[static_cast<std::size_t>(i)].x, left[static_cast<std::size_t>(i)].y});
    return points;
}

// A rib of rectangular section swept along a centre line in the (x, y) plane: `width` across the line in the
// plane, `depth` along z, flat facets, closed at both ends.
rt::Mesh rib(std::span<const Vec2> line, float width, float depth) {
    rt::Mesh m;
    const float hw = 0.5f * width, hd = 0.5f * depth;
    std::vector<Vec3> inner, outer;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const Vec2 a = line[i == 0 ? 0 : i - 1], b = line[std::min(i + 1, line.size() - 1)];
        Vec2 t{b.x - a.x, b.y - a.y};
        const float len = std::hypot(t.x, t.y);
        t = {t.x / len, t.y / len};
        const Vec3 n{-t.y, t.x, 0}; // left of the direction of travel: outward (up) for a left-to-right arch
        const Vec3 p{line[i].x, line[i].y, 0};
        outer.push_back(p + n * hw);
        inner.push_back(p - n * hw);
    }
    const Vec3 front{0, 0, -hd}, back{0, 0, hd};
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const Vec3 o0 = outer[i], o1 = outer[i + 1], i0 = inner[i], i1 = inner[i + 1];
        const Vec3 up = (o0 + o1) * 0.5f - (i0 + i1) * 0.5f;
        flatQuad(m, o0 + front, o1 + front, o1 + back, o0 + back, up);
        flatQuad(m, i0 + front, i0 + back, i1 + back, i1 + front, -up);
        flatQuad(m, i0 + front, i1 + front, o1 + front, o0 + front, {0, 0, -1});
        flatQuad(m, i0 + back, o0 + back, o1 + back, i1 + back, {0, 0, 1});
    }
    return m;
}

// A hexagonal quartz crystal: radius 1 across the corners, from y = 0 to the tip at y = 1, the pyramidal
// termination the top `tip` of it.
rt::Mesh crystal(float tip) {
    const std::array<Vec2, 4> profile = {{{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f - tip}, {0.f, 1.f}}};
    return facetedLathe(profile, 6, 0.f);
}

// --- The cathedral's windows -----------------------------------------------------------------------------------

// Whether a cell centre (u across in [-0.5, 0.5] of the width, y up in metres from the window's bottom) lies
// inside a lancet: the rectangle up to the head, then a pointed arch of the window's width.
bool insideLancet(float u, float y, float width, float height) {
    const float headRise = 0.5f * width * 1.35f;
    const float spring = height - headRise;
    if (y <= spring)
        return true;
    // The head: each half an arc of radius 0.85 * width centred on the other side.
    const float r = 0.85f * width;
    const float x = std::abs(u * width);
    const float cx = -0.5f * width + r; // centre of the left arc, mirrored by |x|
    const float dx = -x - cx, dy = (y - spring) * (std::sqrt(r * r - cx * cx) / headRise);
    return dx * dx + dy * dy <= r * r;
}

// The colour of a lancet's cell (col, row) in variant v: a ruby and amber border, gold and crimson
// medallions, and between them clear and pale glass, so that most of the sun comes in white.
std::uint32_t lancetColour(int col, int row, int variant) {
    const bool border = col == 0 || col == kWindowCols - 1;
    if (border)
        return (row + variant) % 2 == 0 ? 1u : 0u;
    const int band = (row + variant) % 5;
    if (band == 1) // a medallion
        return (col + row) % 2 == 0 ? 3u : 4u;
    return hash01(std::uint32_t(col * 131 + row * 7 + variant * 977)) < 0.6f ? 5u : 2u;
}

// One variant's panes of one colour, in the window's own frame: x across, y up from its bottom, at z = 0.
rt::Mesh lancetPanes(int variant, std::uint32_t colour) {
    rt::Mesh m;
    const float cell = kWindowWidth / kWindowCols;
    const float height = kWindowTop - kWindowBottom;
    const float rowHeight = height / kWindowRows;
    const float gap = 0.022f;
    for (int row = 0; row < kWindowRows; ++row)
        for (int col = 0; col < kWindowCols; ++col) {
            const float u = (float(col) + 0.5f) / kWindowCols - 0.5f;
            const float y = (float(row) + 0.5f) * rowHeight;
            if (!insideLancet(u, y, kWindowWidth, height) || lancetColour(col, row, variant) != colour)
                continue;
            const float x0 = -0.5f * kWindowWidth + col * cell + gap, x1 = x0 + cell - 2 * gap;
            const float y0 = row * rowHeight + gap, y1 = y0 + rowHeight - 2 * gap;
            flatQuad(m, {x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, {0, 0, -1});
        }
    return m;
}

// The leading between the panes, and the stone filling the cells outside the lancet's head.
rt::Mesh lancetLead() {
    rt::Mesh m;
    const float cell = kWindowWidth / kWindowCols;
    const float height = kWindowTop - kWindowBottom;
    const float rowHeight = height / kWindowRows;
    const float w = 0.03f, d = 0.03f;
    for (int col = 0; col <= kWindowCols; ++col) {
        const float x = -0.5f * kWindowWidth + col * cell;
        addBox(m, {x - w, 0, -d}, {x + w, height, d});
    }
    for (int row = 0; row <= kWindowRows; ++row) {
        const float y = row * rowHeight;
        addBox(m, {-0.5f * kWindowWidth, y - w, -d}, {0.5f * kWindowWidth, y + w, d});
    }
    return m;
}

rt::Mesh lancetTracery() {
    rt::Mesh m;
    const float cell = kWindowWidth / kWindowCols;
    const float height = kWindowTop - kWindowBottom;
    const float rowHeight = height / kWindowRows;
    for (int row = 0; row < kWindowRows; ++row)
        for (int col = 0; col < kWindowCols; ++col) {
            const float u = (float(col) + 0.5f) / kWindowCols - 0.5f;
            const float y = (float(row) + 0.5f) * rowHeight;
            if (insideLancet(u, y, kWindowWidth, height))
                continue;
            const float x0 = -0.5f * kWindowWidth + col * cell;
            addBox(m, {x0, row * rowHeight, -0.12f}, {x0 + cell, (row + 1) * rowHeight, 0.12f});
        }
    return m;
}

// The rose window in its own frame (centre at the origin, facing -z): rings of cells by colour.
struct RoseCell {
    float r0, r1, a0, a1;
    std::uint32_t colour;
};
std::vector<RoseCell> roseCells() {
    std::vector<RoseCell> cells;
    cells.push_back({0.f, 0.75f, 0.f, 2 * kPi, 2});
    const struct {
        float r0, r1;
        int count;
        std::array<std::uint32_t, 3> colours;
    } rings[] = {{0.75f, 1.9f, 8, {1, 0, 1}}, {1.9f, 3.1f, 16, {0, 3, 4}}, {3.1f, 3.85f, 32, {1, 2, 0}}};
    for (const auto& ring : rings)
        for (int i = 0; i < ring.count; ++i) {
            const float a0 = 2 * kPi * float(i) / float(ring.count), a1 = 2 * kPi * float(i + 1) / float(ring.count);
            cells.push_back({ring.r0, ring.r1, a0, a1, ring.colours[static_cast<std::size_t>(i % 3)]});
        }
    return cells;
}

void annularCell(rt::Mesh& m, float r0, float r1, float a0, float a1, float z, float inset) {
    const int steps = std::max(2, int((a1 - a0) / (6 * kDegrees)));
    const float ri = r0 > 0 ? r0 + inset : 0.f, ro = r1 - inset;
    const float da = r0 > 0 ? inset / std::max(ri, 0.1f) : 0.f;
    for (int s = 0; s < steps; ++s) {
        const float b0 = a0 + da + (a1 - a0 - 2 * da) * float(s) / steps;
        const float b1 = a0 + da + (a1 - a0 - 2 * da) * float(s + 1) / steps;
        auto at = [&](float r, float a) { return Vec3{r * std::cos(a), r * std::sin(a), z}; };
        if (ri > 0)
            flatQuad(m, at(ri, b0), at(ro, b0), at(ro, b1), at(ri, b1), {0, 0, -1});
        else
            flatTriangle(m, {0, 0, z}, at(ro, b0), at(ro, b1), {0, 0, -1});
    }
}

rt::Mesh rosePanes(std::uint32_t colour) {
    rt::Mesh m;
    for (const RoseCell& c : roseCells())
        if (c.colour == colour)
            annularCell(m, c.r0, c.r1, c.a0, c.a1, 0.f, 0.025f);
    return m;
}

rt::Mesh roseLead() {
    rt::Mesh m;
    for (const RoseCell& c : roseCells()) {
        // The cell's outline as thin strips just in front of the glass.
        annularCell(m, std::max(c.r1 - 0.05f, 0.01f), c.r1 + 0.02f, c.a0, c.a1, -0.03f, 0.f);
        if (c.r0 > 0) {
            const float a = c.a0;
            const Vec3 d{std::cos(a), std::sin(a), 0}, s{-std::sin(a) * 0.035f, std::cos(a) * 0.035f, 0};
            const Vec3 p0 = d * c.r0 + Vec3{0, 0, -0.03f}, p1 = d * c.r1 + Vec3{0, 0, -0.03f};
            flatQuad(m, p0 - s, p1 - s, p1 + s, p0 + s, {0, 0, -1});
        }
    }
    return m;
}

// The stone around the rose: the square of the wall's opening minus the circle.
rt::Mesh roseStone(float half) {
    rt::Mesh m;
    const int steps = 64;
    for (int s = 0; s < steps; ++s) {
        const float a0 = 2 * kPi * float(s) / steps, a1 = 2 * kPi * float(s + 1) / steps;
        auto onSquare = [&](float a) {
            const float c = std::cos(a), si = std::sin(a);
            const float k = half / std::max(std::abs(c), std::abs(si));
            return Vec3{c * k, si * k, 0};
        };
        const Vec3 i0{kRoseRadius * std::cos(a0), kRoseRadius * std::sin(a0), 0};
        const Vec3 i1{kRoseRadius * std::cos(a1), kRoseRadius * std::sin(a1), 0};
        for (const float z : {-0.15f, 0.15f}) {
            const Vec3 dz{0, 0, z};
            flatQuad(m, i0 + dz, onSquare(a0) + dz, onSquare(a1) + dz, i1 + dz, {0, 0, z});
        }
        // The circle's reveal.
        flatQuad(m, i0 + Vec3{0, 0, -0.15f}, i1 + Vec3{0, 0, -0.15f}, i1 + Vec3{0, 0, 0.15f},
                 i0 + Vec3{0, 0, 0.15f}, -(i0 + i1));
    }
    return m;
}

rt::Mesh pillarShaft() {
    // An octagonal glass shaft with chamfered ends.
    const std::array<Vec2, 6> profile = {{{0.f, 0.f}, {0.36f, 0.f}, {0.44f, 0.12f}, {0.44f, 5.4f}, {0.36f, 5.55f},
                                          {0.f, 5.55f}}};
    return facetedLathe(profile, 8, kPi / 8);
}

// The pier's outline at angle a (radians from +x): the farthest of the octagonal core and the round shafts
// along that direction (every shaft overlaps the core, so the outline is star-shaped about the axis).
float pierRadius(float a) {
    const float sector = kPi / 4.f;
    const float off = a - sector * std::floor(a / sector) - 0.5f * sector;
    float r = kPierCore / std::cos(off);
    const Vec2 u{std::cos(a), std::sin(a)};
    for (int k = 0; k < 8; ++k) {
        const float at = k % 2 == 0 ? kPierShaftAt : kPierFilletAt, radius = k % 2 == 0 ? kPierShaft : kPierFillet;
        const Vec2 c{at * std::cos(sector * float(k)), at * std::sin(sector * float(k))};
        const float b = u.x * c.x + u.y * c.y;
        const float disc = b * b - (c.x * c.x + c.y * c.y - radius * radius);
        if (disc >= 0.f)
            r = std::max(r, b + std::sqrt(disc));
    }
    return r;
}

// The glass pier from y = 0 to `height`, flat-faceted (kPierFacets round), its ends chamfered.
rt::Mesh pier(float height) {
    rt::Mesh m;
    const float chamfer = 0.05f;
    std::vector<Vec3> outline;
    for (int i = 0; i < kPierFacets; ++i) {
        const float a = 2.f * kPi * (float(i) + 0.5f) / float(kPierFacets);
        const float r = pierRadius(a);
        outline.push_back({r * std::cos(a), 0.f, r * std::sin(a)});
    }
    const std::array<std::pair<float, float>, 4> rings = {{{0.f, 0.86f}, {chamfer, 1.f}, {height - chamfer, 1.f},
                                                           {height, 0.86f}}};
    auto at = [&](int i, std::size_t ring) {
        const Vec3 o = outline[static_cast<std::size_t>((i + kPierFacets) % kPierFacets)];
        return Vec3{o.x * rings[ring].second, rings[ring].first, o.z * rings[ring].second};
    };
    for (int i = 0; i < kPierFacets; ++i)
        for (std::size_t ring = 0; ring + 1 < rings.size(); ++ring) {
            const Vec3 a = at(i, ring), b = at(i + 1, ring), c = at(i + 1, ring + 1), d = at(i, ring + 1);
            const Vec3 mid = (a + b + c + d) * 0.25f;
            flatQuad(m, a, b, c, d, Vec3{mid.x, 0.f, mid.z});
        }
    for (int i = 0; i < kPierFacets; ++i) {
        flatTriangle(m, {0, 0, 0}, at(i, 0), at(i + 1, 0), {0, -1, 0});
        flatTriangle(m, {0, height, 0}, at(i, 3), at(i + 1, 3), {0, 1, 0});
    }
    return m;
}

std::vector<rt::Mesh> buildMeshes() {
    std::vector<rt::Mesh> meshes(kMeshCount);
    meshes[kGlenzStar] = glenzStar();
    meshes[kQuad] = quad(1.f, 1.f);
    meshes[kUnitBox] = box({0.5f, 0.5f, 0.5f});
    {
        // The cove: a quarter circle from the floor at z = kCoveStart up to the back wall, then the wall.
        std::vector<Vec2> profile;
        for (int i = 0; i <= 12; ++i) {
            const float a = 0.5f * kPi * float(i) / 12.f;
            profile.push_back({kCoveStart + kCoveRadius * std::sin(a), kCoveRadius * (1.f - std::cos(a))});
        }
        profile.push_back({kCoveStart + kCoveRadius, kWallTop});
        meshes[kCove] = extrudeAlongX(profile, kFloorHalf, {kCoveStart - 4.f, 4.f});
    }
    meshes[kPillarShaft] = pillarShaft();
    meshes[kPier] = pier(kPillarTop - kPlinth);
    {
        // The pier's base: a square plinth under a torus and a scotia; its capital: a bell flaring to a
        // square abacus (the boxes are added with the instances).
        const std::array<Vec2, 7> base = {
            {{0.f, 0.f}, {0.62f, 0.f}, {0.62f, 0.05f}, {0.55f, 0.1f}, {0.5f, 0.12f}, {0.47f, 0.15f}, {0.f, 0.15f}}};
        meshes[kPierBase] = facetedLathe(base, 24, 0.f);
        const std::array<Vec2, 7> capital = {
            {{0.f, 0.f}, {0.43f, 0.f}, {0.45f, 0.06f}, {0.5f, 0.16f}, {0.6f, 0.24f}, {0.66f, 0.25f}, {0.f, 0.25f}}};
        meshes[kPierCapital] = facetedLathe(capital, 24, 0.f);
    }
    {
        const std::vector<Vec2> line = pointedArch(kBay, 0.72f, 10);
        meshes[kArcadeArch] = rib(line, 0.4f, 0.6f);
    }
    {
        // The vault: a pointed barrel over the nave, its section in the (x, y) plane extruded along z.
        const std::vector<Vec2> line = pointedArch(2 * kNaveHalf, 0.6f, 14);
        rt::Mesh m;
        for (std::size_t i = 0; i + 1 < line.size(); ++i) {
            const Vec2 a = line[i], b = line[i + 1];
            const Vec3 towards{-0.5f * (a.x + b.x), -0.5f * (a.y + b.y) + 2.f, 0};
            flatQuad(m, {a.x, a.y, 0}, {b.x, b.y, 0}, {b.x, b.y, kNaveLength}, {a.x, a.y, kNaveLength}, towards);
        }
        meshes[kVault] = m;
        std::vector<Vec2> inner;
        for (const Vec2 p : line) // the rib runs just under the vault
            inner.push_back({p.x * 0.985f, p.y - 0.12f});
        meshes[kVaultRib] = rib(inner, 0.32f, 0.36f);
    }
    meshes[kCame] = lancetLead();
    meshes[kTracery] = lancetTracery();
    for (std::uint32_t c = 0; c < kPaneColours; ++c)
        meshes[kRose + c] = rosePanes(c);
    meshes[kRoseLead] = roseLead();
    meshes[kRoseStone] = roseStone(4.2f);
    for (int v = 0; v < kWindowVariants; ++v)
        for (std::uint32_t c = 0; c < kPaneColours; ++c) {
            meshes[kWindowPanes + std::uint32_t(v) * kPaneColours + c] = lancetPanes(v, c);
        }
    for (std::uint32_t v = 0; v < kCrystalVariants; ++v)
        meshes[kCrystal + v] = crystal(0.1f + 0.06f * float(v));
    {
        // A hanging lamp: a short closed iron drum (the emitter just under its floor, per frame) and the rod up
        // to the vault.
        const std::array<Vec2, 6> profile = {
            {{0.f, 0.f}, {0.26f, 0.f}, {0.3f, 0.06f}, {0.3f, 0.3f}, {0.05f, 0.36f}, {0.f, 0.36f}}};
        rt::Mesh m = facetedLathe(profile, 12, 0.f);
        addBox(m, {-0.015f, 0.36f, -0.015f}, {0.015f, 9.f, 0.015f});
        meshes[kLampHousing] = m;
    }
    for (rt::Mesh& m : meshes)
        if (m.indices.empty()) // a colour a window does not use: every mesh needs a triangle, a sliver out of sight
            flatTriangle(m, {0, -1.f, 0}, {0.001f, -1.f, 0}, {0, -0.999f, 0}, {0, 0, -1});
    return meshes;
}

std::vector<rt::Material> buildMaterials(const GlassOptions& options) {
    const float abbe = options.abbe;
    std::vector<rt::Material> m(kMaterialCount);
    // GLENZ: clear flint around a cobalt core. The core is index-matched to the flint (IOR 1: no interface
    // where they meet, as in cased glass) and shows only by Beer-Lambert: blue-violet, pale through the
    // star's tips, deep through its heart.
    m[kFlint] = glass({0.f, 0.f, 0.f}, kFlintIor, abbe);
    // The core is a denser glass fused inside the flint: its surface is a flint-to-core interface, of the
    // relative index n_core / n_flint and the relative dispersion (relativeAbbe), and it is cobalt.
    m[kCobalt] = glass(options.clearCore ? Vec3{} : Vec3{0.32f, 0.42f, 0.04f}, kCoreIor / kFlintIor,
                       abbe > 0.f ? relativeAbbe(kFlintIor, abbe, kCoreIor, kCoreAbbe) : 0.f);
    // The cyclorama: warm white paint, a light polish worn into it.
    m[kCyc] = stone({0.80f, 0.78f, 0.74f}, {0.71f, 0.69f, 0.66f}, 0.7f, 0.f);
    m[kCycFloor] = stone({0.80f, 0.78f, 0.74f}, {0.70f, 0.68f, 0.64f}, 0.55f, 0.f);
    m[kCycFloor].coat = 0.15f;
    m[kCycFloor].coatRoughness = 0.06f;
    m[kCycFloor] = receiverOf(m[kCycFloor]);
    m[kKeyEmitter] = emitter(kKeyColor, kKeyRadiance);
    m[kKeySoftEmitter] = emitter(kKeyColor, kKeySoftRadiance);
    m[kSoftbox] = emitter({0.85f, 0.92f, 1.f}, kSoftboxRadiance);
    m[kScrim] = emitter({0.92f, 0.95f, 1.f}, kScrimRadiance);
    m[kTubeRed] = emitter({1.f, 0.16f, 0.09f}, kTubeRadiance);
    m[kTubeGold] = emitter({1.f, 0.62f, 0.12f}, kTubeRadiance);
    m[kTubeGreen] = emitter({0.2f, 0.95f, 0.28f}, kTubeRadiance);
    for (const Material tube : {kTubeRed, kTubeGold, kTubeGreen}) { // glass tubes when they are dark
        setColor(m[tube].baseColor, {0.16f, 0.16f, 0.17f});
        m[tube].roughness = 0.25f;
    }
    m[kWash] = emitter({0.55f, 0.72f, 1.f}, kWashRadiance);
    m[kFixture] = pbr({0.03f, 0.03f, 0.035f}, 0.35f);
    m[kFixture].metallic = 1.f;
    // GLASS CATHEDRAL.
    m[kFlagstones] = receiverOf(stone({0.70f, 0.66f, 0.58f}, {0.58f, 0.54f, 0.47f}, 0.5f, 1.1f));
    m[kFlagstones].coat = 0.25f;
    m[kFlagstones].coatRoughness = 0.12f;
    m[kLimestone] = stone({0.66f, 0.62f, 0.55f}, {0.48f, 0.45f, 0.40f}, 0.85f, 0.f);
    m[kLimestoneWest] = receiverOf(m[kLimestone]);
    m[kLimestoneWest].receiver = 1;
    m[kShutterWood] = pbr({0.16f, 0.09f, 0.05f}, 0.7f);
    m[kShutterWood].pattern = static_cast<std::uint32_t>(rt::Pattern::WoodPlanks);
    m[kShutterWood].patternScale = 0.18f;
    setColor(m[kShutterWood].baseColor2, {0.08f, 0.045f, 0.025f});
    // The piers: a light flint, clear, less dispersive than the glenz's (its fire would turn every column the
    // windows shine through into rainbow static), still splitting the sun on the floor.
    m[kPillarGlass] = glass({0.f, 0.f, 0.f}, 1.58f, abbe > 0.f ? 40.f : 0.f);
    m[kLead] = pbr({0.06f, 0.06f, 0.065f}, 0.5f);
    m[kLead].metallic = 1.f;
    m[kLampEmitter] = emitter(kLampColor, kLampRadiance);
    m[kIron] = pbr({0.05f, 0.045f, 0.04f}, 0.55f);
    m[kIron].metallic = 1.f;
    for (std::uint32_t c = 0; c < kPaneColours; ++c)
        m[kPaneFirst + c] = pane(kPaneTint[c]);
    // CRYSTAL CITY: thick quartz-like glass, sea-green by absorption over metres.
    m[kQuartz] = glass({0.035f, 0.012f, 0.006f}, 1.6f, abbe); // glacial: blue-green through metres
    // The plaza sculpture's core: the cobalt's place taken by amber, the sun's colour (Act III's gold).
    m[kAmberCore] = m[kCobalt];
    setColor(m[kAmberCore].baseColor2, options.clearCore ? Vec3{} : Vec3{0.f, 0.02f, 0.09f});
    m[kPavingOutside] = stone({0.46f, 0.45f, 0.43f}, {0.34f, 0.33f, 0.32f}, 0.6f, 0.9f);
    m[kPavingOutside].coat = 0.15f;
    m[kPavingOutside].coatRoughness = 0.1f;
    m[kPaving] = receiverOf(m[kPavingOutside]);
    return m;
}

// --- GLENZ ------------------------------------------------------------------------------------------------------

// The glenz's rotation, x then y then z (retro/glenz.cpp's turn), at `seconds`.
rt::Mat34 glenzRotation(double seconds) {
    double a[3];
    for (int i = 0; i < 3; ++i)
        a[i] = retro::kGlenzStartAngle[i] + retro::kGlenzSpin[i] * seconds;
    const rt::Mat34 rx = rotate({1, 0, 0}, float(a[0]));
    const rt::Mat34 ry = rotate({0, 1, 0}, float(a[1]));
    const rt::Mat34 rz = rotate({0, 0, 1}, float(a[2]));
    return compose(rz, compose(ry, rx));
}

Vec3 glenzCentre(const show::TimelineFrame& f, double seconds) {
    const double lift = retro::kGlenzFloor + retro::kGlenzLift * retro::glenzBounce(f, seconds);
    return {0, float(kGlenzWorldScale * lift) + glenzWorldLift(), 0};
}

// Where the copper drum's tube i is (centre of the tube), at seconds into the chapter: the angle and the
// swaying amplitude of copper.comp (pc.retroSeconds), on a drum round an axis along x behind the star.
Vec3 tubeCentre(std::uint32_t i, double seconds) {
    const double t = seconds;
    const double amplitude = (72.0 + 14.0 * std::sin(t * 0.37)) / 86.0;
    const double angle = t * 1.3 + double(i) * 2.0 * std::numbers::pi / double(kTubes);
    return {0.f, kDrumY - float(kDrumRadiusY * amplitude * std::sin(angle)),
            kDrumZ - float(kDrumRadiusZ * std::cos(angle))};
}

} // namespace

// --- Public helpers -------------------------------------------------------------------------------------------

std::vector<Glass::Tower> crystalCityTowers() {
    // One quartz crystal per building of the 1993 city's lots, its hexagon inside the building's footprint,
    // about as tall as the building (its set-back tower included).
    std::vector<Glass::Tower> towers;
    for (std::int32_t m = kLotM0; m <= kLotM1; ++m)
        for (std::int32_t i = kLotI0; i <= kLotI1; ++i)
            for (const retro::CityBuilding& b : retro::cityLot(i, m)) {
                if ((i == -1 || i == 0) && (m == 4 || m == 5))
                    continue; // the plaza
                const float x0 = float(b.base.x0) * kCityScale, x1 = float(b.base.x1) * kCityScale;
                const float z0 = float(b.base.z0) * kCityScale, z1 = float(b.base.z1) * kCityScale;
                const float top = float(b.tower ? b.top.y1 : b.base.y1) * kCityScale;
                const float across = std::min(x1 - x0, z1 - z0);
                const std::uint32_t h =
                    retro::cityHash(i, m, std::uint32_t(b.sx0 + 2 * b.sz0 + 4 * b.sx1 + 8 * b.sz1));
                Glass::Tower t;
                t.base = {0.5f * (x0 + x1), 0.f, 0.5f * (z0 + z1)};
                t.radius = 0.5f * across / std::cos(30.f * kDegrees) * 0.9f;
                t.height = top * (0.85f + 0.3f * hash01(h));
                t.twist = 2.f * kPi * hash01(h ^ 0x5bd1e995u);
                towers.push_back(t);
            }
    towers.resize(std::min<std::size_t>(towers.size(), kCrystalPrisms));
    return towers;
}

std::uint32_t glassPillarCount() {
    return static_cast<std::uint32_t>(kPillarRows.size()) * std::uint32_t(kBays);
}

std::uint32_t stainedPaneCount() {
    // The lancets' cells inside their outline, in every window of both walls, and the rose's cells.
    std::uint32_t perWindow = 0;
    const float height = kWindowTop - kWindowBottom;
    for (int row = 0; row < kWindowRows; ++row)
        for (int col = 0; col < kWindowCols; ++col)
            if (insideLancet((float(col) + 0.5f) / kWindowCols - 0.5f, (float(row) + 0.5f) * height / kWindowRows,
                             kWindowWidth, height))
                ++perWindow;
    return perWindow * 2u * std::uint32_t(kWindows) + std::uint32_t(roseCells().size());
}

float glenzWorldLift() {
    const float reach = float(retro::kGlenzScale) * std::sqrt(3.f); // a cube corner, the farthest vertex
    return kGlenzWorldScale * (reach - float(retro::kGlenzFloor)) + kGlenzFloorGap;
}

rt::Mat34 glenzTransform(const show::TimelineFrame& frame, double seconds) {
    return compose(translate(glenzCentre(frame, seconds)),
                   compose(scale(Vec3{1, 1, 1} * float(kGlenzWorldScale * retro::kGlenzScale)),
                           glenzRotation(seconds)));
}

retro::GlenzVertices glenzVerticesIn2026(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    retro::GlenzVertices v = retro::glenzWorldVertices(frame, retroFrame);
    for (auto& p : v) {
        for (auto& c : p)
            c *= kGlenzWorldScale;
        p[1] += glenzWorldLift();
    }
    return v;
}

namespace {

// A count with thousands separators: 2,097,152.
std::string grouped(std::uint32_t n) {
    std::string digits = std::to_string(n), out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0)
            out += ',';
        out += digits[i];
    }
    return out;
}

} // namespace

GlassCard glassCard(show::Effect effect) {
    switch (effect) {
    case show::Effect::Glenz: {
        const std::uint32_t photons = kGlenzPhotons * kExportPhotonFactor;
        return {photons, 2, kFlintAbbe, "2026 · DISPERSION · " + grouped(photons) + " PHOTONS · CAUSTICS"};
    }
    case show::Effect::Cathedral: {
        const std::uint32_t photons = kCathedralPhotons * kExportPhotonFactor;
        return {photons, glassPillarCount(), kFlintAbbe,
                "2026 · GLASS CATHEDRAL · " + grouped(glassPillarCount()) + " GLASS PILLARS · " + grouped(photons) +
                    " PHOTONS · SHAFTS"};
    }
    default: {
        const auto prisms = static_cast<std::uint32_t>(crystalCityTowers().size());
        return {kCrystalPhotons * kExportPhotonFactor, prisms, kFlintAbbe,
                "2026 · CRYSTAL CITY · " + grouped(prisms) + " PRISMS · DISPERSION · CAUSTICS"};
    }
    }
}

// --- Cameras ----------------------------------------------------------------------------------------------------

std::vector<float> glassShotStarts(const show::TimelineFrame& f) {
    const auto accent = static_cast<float>(f.accentSeconds);
    switch (f.effect) {
    case show::Effect::Glenz: // the 1993 camera to two bars past the accent's downbeat, then the floor
        return {0.f, barLineNear(f, downbeatBefore(f, f.accentSeconds) + 2.0 * barLength(f, f.accentSeconds))};
    case show::Effect::Cathedral: { // the door, the aisle, the sun on the downbeat, the vault
        const float d = downbeatBefore(f, f.accentSeconds), bar = barLength(f, f.accentSeconds);
        return {0.f, barLineNear(f, d - 2.f * bar), d, barLineNear(f, d + 5.5f * bar)};
    }
    default: { // CRYSTAL CITY: the sunrise, the descent, the avenue from the downbeat, the look back
        const float d = downbeatBefore(f, f.accentSeconds), bar = barLength(f, f.accentSeconds);
        return {0.f, barLineNear(f, 2.f * bar), d, barLineNear(f, d + 5.75f * bar)};
    }
    }
}

namespace {

struct ShotTime {
    std::uint32_t shot;
    float s;     // eased progress through the shot
    float local; // seconds into the shot
    float length;
};

ShotTime shotTime(const show::TimelineFrame& f) {
    const std::vector<float> starts = glassShotStarts(f);
    const auto local = static_cast<float>(f.localSeconds);
    std::uint32_t shot = 0;
    while (shot + 1 < starts.size() && local >= starts[shot + 1])
        ++shot;
    const float start = starts[shot];
    const float end = shot + 1 < starts.size() ? starts[shot + 1] : static_cast<float>(f.chapterDuration);
    const float length = std::max(end - start, 1e-3f);
    return {shot, smooth((local - start) / length), local - start, length};
}

Camera glenzCamera(const show::TimelineFrame& f) {
    const ShotTime st = shotTime(f);
    if (st.shot == 0) {
        // Locked to the 1993 camera (8 units in front of the star, level with it, the same 45 degrees), so
        // that the beat swaps compare like with like; it holds through the accent.
        const float lift = glenzWorldLift();
        const Key k{{0.f, lift, -float(retro::kGlenzCameraDistance) * kGlenzWorldScale}, {0.f, lift, 0.f}};
        return makeCamera(k, float(retro::glenzCamera().fovYDegrees), 0.f, 0.f, st.shot);
    }
    // Free of the 1993 camera: 85 mm, low beyond the end of the caustic's fan on the left, looking back along
    // it to the star: the spectra in the bottom of the frame, the glass (its lower half, however high the bounce
    // carries it) in the top third; a slow push in, focused on the middle of the fan.
    const std::array<Key, 2> keys = {{
        {{-6.4f, 1.08f, 2.4f}, {-1.5f, 0.38f, 0.75f}},
        {{-5.9f, 1.02f, 2.25f}, {-1.4f, 0.38f, 0.72f}},
    }};
    const Key k = alongKeys(keys, st.s);
    return makeCamera(k, kLens85, 0.012f, length(kCausticAt - k.eye), st.shot);
}

// The cathedral: the nave runs along +z from the west door (z = 0) to the rose window (z = kNaveLength).
Camera cathedralCamera(const show::TimelineFrame& f) {
    const ShotTime st = shotTime(f);
    switch (st.shot) {
    case 0: {
        // The nave from the west door, 24 mm: a slow push in, the rose window far ahead.
        const std::array<Key, 2> keys = {{
            {{-1.3f, 1.7f, 1.4f}, {0.4f, 6.5f, 60.f}},
            {{-1.1f, 1.75f, 3.6f}, {0.6f, 6.2f, 60.f}},
        }};
        return makeCamera(alongKeys(keys, st.s), kLens24, 0.f, 0.f, st.shot);
    }
    case 1: {
        // Down the west aisle between the glass pillars, towards the light.
        const std::array<Key, 2> keys = {{
            {{-5.8f, 1.6f, 5.f}, {-3.f, 3.2f, 40.f}},
            {{-5.8f, 1.6f, 11.f}, {-2.5f, 3.4f, 44.f}},
        }};
        return makeCamera(alongKeys(keys, st.s), kLens24, 0.f, 0.f, st.shot);
    }
    case 2: {
        // On the downbeat the hero window's shutter opens on the sun: the camera looks up into it, then turns
        // down the nave as the light walks from bay to bay; focus on the pillars.
        const Vec3 window = cathedralHeroWindow();
        const Vec3 eye0 = window - normalize(kCathedralSun) * ((window.y - 1.5f) / normalize(kCathedralSun).y);
        const std::array<Key, 3> keys = {{
            {eye0, window},
            {eye0 + Vec3{-0.4f, 0.15f, 0.4f}, {4.f, 2.6f, 32.f}},
            {eye0 + Vec3{-1.f, 0.4f, 0.9f}, {-2.f, 0.3f, 40.f}},
        }};
        const float s = clamp01((st.local - 0.15f * st.length) / (0.85f * st.length));
        return makeCamera(alongKeys(keys, smooth(s)), kLens24, 0.02f, 9.f, st.shot);
    }
    default: {
        // Rising towards the vault, looking down the nave at the floor's spectra.
        const std::array<Key, 3> keys = {{
            {{0.f, 2.4f, 21.f}, {0.f, 3.f, 50.f}},
            {{0.5f, 8.f, 25.f}, {0.f, 0.8f, 46.f}},
            {{1.f, 13.5f, 28.f}, {0.f, 0.f, 44.f}},
        }};
        return makeCamera(alongKeys(keys, st.s), kLens24, 0.f, 0.f, st.shot);
    }
    }
}

Camera crystalCamera(const show::TimelineFrame& f) {
    const ShotTime st = shotTime(f);
    switch (st.shot) {
    case 0: {
        // High over the city towards the sunrise, as the white resolves.
        const std::array<Key, 2> keys = {{
            {{-5.f, 40.f, -28.f}, {2.f, 3.f, 62.f}},
            {{-4.f, 37.f, -22.f}, {2.f, 3.f, 64.f}},
        }};
        return makeCamera(alongKeys(keys, st.s), kLens35, 0.f, 0.f, st.shot);
    }
    case 1: {
        // Descending between the crystals into the main street.
        const std::array<Key, 3> keys = {{
            {{-4.f, 34.f, -18.f}, {1.f, 4.f, 66.f}},
            {{-1.5f, 14.f, 6.f}, {0.5f, 4.f, 66.f}},
            {{0.f, 2.f, 20.f}, {0.f, 3.f, 70.f}},
        }};
        return makeCamera(alongKeys(keys, st.s), kLens35, 0.f, 0.f, st.shot);
    }
    case 2: {
        // At street level down the avenue, 24 mm, the plaza's first full spectrum ahead.
        const std::array<Key, 3> keys = {{
            {{0.f, 2.f, 20.f}, {0.f, 2.6f, 63.f}},
            {{0.3f, 1.8f, 36.f}, {0.f, 1.6f, 63.f}},
            {{-0.6f, 1.9f, 48.f}, {0.f, 1.2f, 63.f}},
        }};
        return makeCamera(alongKeys(keys, st.s), kLens24, 0.f, 0.f, st.shot);
    }
    default: {
        // Rising and looking back over the city: a field of spectra.
        const std::array<Key, 3> keys = {{
            {{-7.f, 3.f, 82.f}, {0.f, 2.f, 58.f}},
            {{-2.f, 14.f, 90.f}, {0.f, 0.f, 55.f}},
            {{5.f, 30.f, 98.f}, {-1.f, 0.f, 46.f}},
        }};
        return makeCamera(alongKeys(keys, st.s), kLens35, 0.f, 0.f, st.shot);
    }
    }
}

} // namespace

Camera glassCamera(const show::TimelineFrame& frame) {
    switch (frame.effect) {
    case show::Effect::Cathedral:
        return cathedralCamera(frame);
    case show::Effect::CrystalCity:
        return crystalCamera(frame);
    default:
        return glenzCamera(frame);
    }
}

float glassExposureEV(show::Effect effect) {
    switch (effect) {
    case show::Effect::Cathedral:
        return 2.f;
    case show::Effect::CrystalCity:
        return 0.f;
    default:
        return 0.f;
    }
}

// --- The scene --------------------------------------------------------------------------------------------------

Glass::Glass(SceneDetail detail, GlassOptions options) : detail_(detail), options_(options) {
    if (!(detail.scale > 0.f && detail.scale <= 1.f))
        throw std::invalid_argument("Glass: the detail must be in (0, 1]");
    static_.meshes = buildMeshes();
    static_.materials = buildMaterials(options);

    towers_ = crystalCityTowers();
    panes_ = stainedPaneCount();

    SceneCapacity& cap = static_.capacity;
    cap.instances = 1024;
    cap.lights = 32;
    cap.materials = kMaterialCount;
    cap.causticResolution = kGlenzResolution;

    // The cathedral: everything that does not change (the pillars, the shutters and the lamps' emitters are
    // per frame).
    auto add = [this](std::uint32_t mesh, std::uint32_t material, const rt::Mat34& t) {
        cathedral_.push_back({mesh, material, t});
    };
    for (int k = 0; k < kWindows; ++k) {
        const float z = windowZ(k);
        const rt::Mat34 east = place({kNaveHalf + 0.4f * kWallThickness, kWindowBottom, z},
                                     rotate({0, 1, 0}, -0.5f * kPi), {1, 1, 1});
        const rt::Mat34 west = place({-kNaveHalf - 0.4f * kWallThickness, kWindowBottom, z},
                                     rotate({0, 1, 0}, 0.5f * kPi), {1, 1, 1});
        for (std::uint32_t c = 0; c < kPaneColours; ++c) {
            add(kWindowPanes + std::uint32_t(k % kWindowVariants) * kPaneColours + c, kPaneFirst + c, east);
            add(kWindowPanes + std::uint32_t((k + 2) % kWindowVariants) * kPaneColours + c, kPaneFirst + c, west);
        }
        for (const rt::Mat34& w : {east, west}) {
            add(kCame, kLead, w);
            add(kTracery, kLimestone, w);
        }
    }
    // The floor (receiver 0), the vault and its ribs.
    add(kQuad, kFlagstones,
        place({0, 0, 0.5f * kNaveLength}, rotate({1, 0, 0}, -0.5f * kPi), {2 * kNaveHalf, kNaveLength, 1}));
    add(kVault, kLimestone, translate({0, kSpringHeight, 0}));
    for (int k = 0; k < kBays; ++k)
        add(kVaultRib, kLimestone, translate({0, kSpringHeight, pillarZ(k)}));
    // The side walls around their windows: a plinth, piers, the band above the windows. The west wall's
    // face is receiver 1 (the low sun throws the east windows' light across onto it).
    for (const float side : {-1.f, 1.f}) {
        const float x0 = side > 0 ? kNaveHalf : -kNaveHalf - kWallThickness;
        const float xc = x0 + 0.5f * kWallThickness;
        const std::uint32_t material = side > 0 ? kLimestone : kLimestoneWest;
        auto wallBox = [&](float y0, float y1, float z0, float z1) {
            add(kUnitBox, material, place({xc, 0.5f * (y0 + y1), 0.5f * (z0 + z1)}, {kWallThickness, y1 - y0, z1 - z0}));
        };
        wallBox(0.f, kWindowBottom, 0.f, kNaveLength);
        wallBox(kWindowTop, kSpringHeight + 0.6f, 0.f, kNaveLength);
        float z = 0.f;
        for (int k = 0; k < kWindows; ++k) {
            wallBox(kWindowBottom, kWindowTop, z, windowZ(k) - 0.5f * kWindowWidth);
            z = windowZ(k) + 0.5f * kWindowWidth;
        }
        wallBox(kWindowBottom, kWindowTop, z, kNaveLength);
    }
    // The end walls: the west door's wall, and the apse wall round the rose window's square opening.
    {
        const float half = 4.2f, t = kWallThickness;
        const float w = kNaveHalf + kWallThickness;
        add(kUnitBox, kLimestone, place({0, 0.5f * kGableTop, -0.5f * t}, {2 * w, kGableTop, t}));
        const float zc = kNaveLength + 0.5f * t;
        add(kUnitBox, kLimestone, place({0, 0.5f * (kRoseY - half), zc}, {2 * w, kRoseY - half, t}));
        add(kUnitBox, kLimestone,
            place({0, 0.5f * (kRoseY + half + kGableTop), zc}, {2 * w, kGableTop - kRoseY - half, t}));
        add(kUnitBox, kLimestone, place({-0.5f * (w + half), kRoseY, zc}, {w - half, 2 * half, t}));
        add(kUnitBox, kLimestone, place({0.5f * (w + half), kRoseY, zc}, {w - half, 2 * half, t}));
        const rt::Mat34 rose = translate({0, kRoseY, kNaveLength + 0.4f * t});
        for (std::uint32_t c = 0; c < kPaneColours; ++c)
            add(kRose + c, kPaneFirst + c, rose);
        add(kRoseLead, kLead, rose);
        add(kRoseStone, kLimestone, rose);
    }
    // The pillars' stone: plinths, capitals and the arcades from pillar to pillar along each row; the hanging
    // lamps' housings.
    for (const float x : kPillarRows)
        for (int k = 0; k < kBays; ++k) {
            const float z = pillarZ(k);
            add(kUnitBox, kLimestone, place({x, 0.225f, z}, {1.1f, 0.45f, 1.1f}));
            add(kPierBase, kLimestone, translate({x, 0.45f, z}));
            add(kPierCapital, kLimestone, translate({x, kPillarTop, z}));
            add(kUnitBox, kLimestone, place({x, kPillarTop + 0.325f, z}, {1.36f, 0.15f, 1.36f}));
            if (k + 1 < kBays)
                add(kArcadeArch, kLimestone,
                    place({x, kPillarTop + 0.4f, z + 0.5f * kBay}, rotate({0, 1, 0}, 0.5f * kPi), {1, 1, 1}));
        }
    for (int k = 0; k < kWindows; ++k)
        add(kLampHousing, kIron, translate({0, kLampHeight + 0.04f, windowZ(k)}));
    // The piers themselves, in bay order: flint glass on their bases.
    for (int k = 0; k < kBays; ++k)
        for (std::size_t r = 0; r < kPillarRows.size(); ++r)
            pillars_.push_back({kPier, kPillarGlass, translate({kPillarRows[r], kPlinth, pillarZ(k)})});
}

SceneFrame Glass::frame(const show::TimelineFrame& f) const {
    switch (f.effect) {
    case show::Effect::Cathedral:
        return cathedralFrame(f);
    case show::Effect::CrystalCity:
        return crystalFrame(f);
    default:
        return glenzFrame(f);
    }
}

namespace {

// The scene's one table serves three chapters: a receiver material another chapter's frame has no receiver
// for is an ordinary surface there.
void limitReceivers(std::vector<rt::Material>& materials, std::size_t receivers) {
    for (rt::Material& m : materials)
        if ((m.flags & rt::kMaterialCausticReceiver) != 0 && m.receiver >= receivers) {
            m.flags &= ~rt::kMaterialCausticReceiver;
            m.receiver = rt::kNoReceiver;
        }
}

std::uint32_t photonsAt(std::uint32_t full, SceneDetail detail) {
    return std::max<std::uint32_t>(1024u, std::uint32_t(double(full) * double(detail.scale)));
}

void addLight(SceneFrame& out, std::uint32_t mesh, std::uint32_t material, const rt::Mat34& transform, float w,
              float h, const std::vector<rt::Material>& materials) {
    const auto index = static_cast<std::uint32_t>(out.lights.size());
    out.lights.push_back(rectLightForQuad(transform, w, h, rt::LightKind::Emitter, emitted(materials[material]), 1.f));
    out.instances.push_back({mesh, material, compose(transform, scale({w, h, 1})), index});
}

} // namespace

SceneFrame Glass::glenzFrame(const show::TimelineFrame& f) const {
    SceneFrame out;
    out.camera = glenzCamera(f);
    out.exposureEV = glassExposureEV(show::Effect::Glenz);
    // The tubes', the scrim's and the softboxes' light through the glass is path traced (only the key's is in
    // the photon map), and so is the key's on the cyclorama's walls: a low clamp keeps those caustics from
    // sparkling (an export does not filter).
    out.clampIndirect = 0.3f;
    out.materials = static_.materials;
    std::vector<rt::Material>& mats = out.materials;

    // The music. Until the accent the key is a soft box and its caustic a blur; on the accent, as the bounce
    // lands, the hard spot takes over and the caustic snaps into focus with its spectrum. From the accent's
    // downbeat the key steps warm and cool on every beat, so the caustic breathes. The tubes breathe with the
    // kick and dim for the bar after the accent, so the floor's spectrum has the stage.
    const auto local = static_cast<float>(f.localSeconds);
    const auto accent = static_cast<float>(f.accentSeconds);
    const float bar = barLength(f, f.accentSeconds);
    const bool hard = local >= accent;
    float tubes = 1.f + 0.35f * f.kick;
    if (hard)
        tubes *= 0.3f + 0.7f * smooth((local - accent - bar) / (0.5f * bar));
    mats[kTubeRed].emissionScale *= tubes;
    mats[kTubeGold].emissionScale *= tubes;
    mats[kTubeGreen].emissionScale *= tubes;
    const int beats = beatsSince(f, downbeatBefore(f, f.accentSeconds));
    const Vec3 tint = beats < 0 ? kKeyColor : (beats % 2 == 0 ? kKeyWarm : kKeyCool);
    setColor(mats[kKeyEmitter].emission, tint);
    setColor(mats[kKeySoftEmitter].emission, tint);
    if (hard)
        mats[kKeySoftEmitter].emissionScale = 0.f;
    else
        mats[kKeyEmitter].emissionScale = 0.f;

    // The star and its core (the photons' casters), first. The core is turned in the glass (fixed, as cast),
    // so that its faces meet the star's at angles: prisms wherever light crosses it.
    const rt::Mat34 star = glenzTransform(f, f.localSeconds);
    out.instances.push_back({kGlenzStar, kFlint, star});
    out.instances.push_back(
        {kGlenzStar, kCobalt, compose(star, compose(rotate({0, 1, 0}, kGlenzCoreTurn), scale(Vec3{1, 1, 1} * kGlenzCoreScale)))});

    // The cyclorama.
    const rt::Mat34 flat = rotate({1, 0, 0}, -0.5f * kPi); // quad +z -> +y
    out.instances.push_back(
        {kQuad, kCycFloor, place(kReceiverAt, flat, {2 * kReceiverHalf, 2 * kReceiverHalf, 1})});
    auto floorPart = [&](float x0, float x1, float z0, float z1) {
        out.instances.push_back(
            {kQuad, kCyc, place({0.5f * (x0 + x1), 0, 0.5f * (z0 + z1)}, flat, {x1 - x0, z1 - z0, 1})});
    };
    const float rx0 = kReceiverAt.x - kReceiverHalf, rx1 = kReceiverAt.x + kReceiverHalf;
    const float rz0 = kReceiverAt.z - kReceiverHalf, rz1 = kReceiverAt.z + kReceiverHalf;
    floorPart(-kFloorHalf, kFloorHalf, kFloorFront, rz0);
    floorPart(-kFloorHalf, kFloorHalf, rz1, kCoveStart);
    floorPart(-kFloorHalf, rx0, rz0, rz1);
    floorPart(rx1, kFloorHalf, rz0, rz1);
    out.instances.push_back({kCove, kCyc, identity()});

    // The key: a hard spot inside a soft box, aimed at the star, in a black housing (one of the two is on).
    const Vec3 keyAxis = normalize(kKeyAim - kKeyAt);
    const rt::Mat34 keyTurn = rotationFromTo({0, 0, 1}, keyAxis);
    addLight(out, kQuad, kKeyEmitter, place(kKeyAt, keyTurn, {1, 1, 1}), kKeySize, kKeySize, mats);
    addLight(out, kQuad, kKeySoftEmitter, place(kKeyAt - keyAxis * 0.01f, keyTurn, {1, 1, 1}), kKeySoftSize,
             kKeySoftSize, mats);
    out.instances.push_back(
        {kUnitBox, kFixture, place(kKeyAt - keyAxis * 0.2f, keyTurn, {kKeySoftSize + 0.04f, kKeySoftSize + 0.04f, 0.36f})});
    // Its snoot: a black tube kSnootLength long round the light, so that it lights the star and the floor
    // under it and little else; the caustic then has the floor to itself.
    for (int side = 0; side < 8; ++side) {
        const float a = 0.25f * kPi * float(side), offset = kSnootOpening + 0.5f * kSnootWall;
        const float across = 2.f * (kSnootOpening + kSnootWall) * std::tan(kPi / 8.f) + 0.01f;
        const rt::Mat34 wall = compose(rotate({0, 0, 1}, a),
                                       place({offset, 0.f, 0.5f * kSnootLength}, {kSnootWall, across, kSnootLength}));
        out.instances.push_back({kUnitBox, kFixture, compose(translate(kKeyAt), compose(keyTurn, wall))});
    }
    // The strip softboxes, out to either side of the star and behind it, turned to it: the bright edges.
    const bool floorShot = out.camera.shot == 1;
    for (const float side : {-1.f, 1.f}) {
        // (Re-rigged for the floor shot, which would see one: they go under the stage.)
        const Vec3 at{side * 3.9f, floorShot ? -3.f : 1.2f, 1.2f};
        const Vec3 to = Vec3{0, 0.9f, 0} - at;
        const float yaw = std::atan2(to.x, to.z);
        addLight(out, kQuad, kSoftbox, place(at, rotate({0, 1, 0}, yaw), {1, 1, 1}), kSoftboxWidth,
                 kSoftboxHeight, mats);
    }
    // The scrim, facing down, and the white flat behind the 1993 camera, facing the star: what its upper and
    // its front facets mirror.
    addLight(out, kQuad, kScrim, place(kScrimAt, rotate({1, 0, 0}, 0.5f * kPi), {1, 1, 1}), kScrimSize, kScrimSize,
             mats);
    addLight(out, kQuad, kScrim, place({0.f, 1.6f, -6.f}, {1, 1, 1}), 6.f, 3.2f, mats);
    // The cyc wash behind its ground row, tilted up at the back wall: a row of kWashUnits fixtures (each wall
    // point is lit mostly by the nearest, which light selection then finds).
    for (int k = 0; k < kWashUnits; ++k) {
        const float width = kWashWidth / float(kWashUnits);
        const Vec3 at{-0.5f * kWashWidth + (float(k) + 0.5f) * width, 0.22f, kCoveStart - 0.45f};
        addLight(out, kQuad, kWash, place(at, rotate({1, 0, 0}, -0.3f * kPi), {1, 1, 1}), width, kWashHeight, mats);
    }
    {
        out.instances.push_back({kUnitBox, kCyc, place({0.f, 0.2f, kCoveStart - 0.75f}, {kWashWidth + 0.4f, 0.4f, 0.12f})});
    }
    // The copper drum's tubes, facing the camera's side (-z), at the copper's angles. (Re-rigged for the floor
    // shot, which looks along the caustic from the left: the drum stands beyond the star on the right, under
    // the key's light, its tubes across the frame as the 1993 bars are.)
    for (std::uint32_t i = 0; i < kTubes; ++i) {
        Vec3 at = tubeCentre(i, f.localSeconds);
        float facing = kPi;
        if (floorShot) {
            at = {at.z + 1.f, at.y, 0.4f};
            facing = -0.5f * kPi;
        }
        const std::uint32_t material = kTubeRed + i % 3;
        addLight(out, kQuad, material, place(at, rotate({0, 1, 0}, facing), {1, 1, 1}), kTubeWidth, kTubeHeight,
                 mats);
    }

    // Beyond the set, the dim grey of a studio's flats: what the star's facets mirror where the scrim does not
    // reach (black would read as holes in the glass). Never sampled: it lights nothing worth a shadow ray.
    out.environment = environmentDefaults(EnvironmentKind::Space);
    out.environment.starDensity = 0.f;
    out.environment.starBrightness = 0.f;
    out.environment.zenith = out.environment.horizon = {0.1f, 0.102f, 0.11f};
    out.environment.ground = {0.02f, 0.02f, 0.022f};
    out.environment.select = 0.f;

    CausticFrame c;
    CausticReceiver r = receiverForQuad(place(kReceiverAt, flat, {1, 1, 1}), 2 * kReceiverHalf, 2 * kReceiverHalf,
                                        kGlenzResolution);
    r.chromaRadius = 2;
    c.receivers.push_back(r);
    c.photons = photonsAt(kGlenzPhotons, detail_);
    c.light = static_cast<std::int32_t>(hard ? kKeyLight : kKeySoftLight);
    c.casterInstanceFirst = 0;
    c.casterInstanceCount = 2;
    out.caustics = c;
    limitReceivers(out.materials, c.receivers.size());
    return out;
}

Vec3 cathedralSunDirection() {
    return normalize(kCathedralSun);
}

Vec3 cathedralHeroWindow() {
    return {kNaveHalf + 0.4f * kWallThickness, 0.5f * (kWindowBottom + kWindowTop), windowZ(kHeroBay)};
}

float distanceToPier(Vec3 p) {
    float best = 1e30f;
    for (const float x : kPillarRows)
        for (int k = 0; k < kBays; ++k)
            best = std::min(best, std::hypot(p.x - x, p.z - pillarZ(k)));
    return best;
}

float pierReach() {
    float r = 0.f;
    for (int i = 0; i < 720; ++i)
        r = std::max(r, pierRadius(2.f * kPi * float(i) / 720.f));
    return r;
}

float cathedralShutter(const show::TimelineFrame& f, int window) {
    if (window < 2)
        return 1.f;
    if (window < kHeroBay)
        return 0.f;
    const auto local = static_cast<float>(f.localSeconds);
    const float open = window == kHeroBay ? downbeatBefore(f, f.accentSeconds)
                                          : beatAfter(f, f.accentSeconds, window - kHeroBay - 1);
    return smooth((local - open) / kShutterSeconds);
}

SceneFrame Glass::cathedralFrame(const show::TimelineFrame& f) const {
    SceneFrame out;
    out.camera = cathedralCamera(f);
    out.exposureEV = glassExposureEV(show::Effect::Cathedral);
    // The glass piers refract the windows, the lamps and the sun into everything: a low clamp keeps their
    // path-traced light (only the open windows' sunlight through them is in the photon maps) from sparkling.
    out.clampIndirect = 1.5f;
    out.materials = static_.materials;

    // The pillars the sun reaches through the open windows throw the photons' caustics (casters first): the
    // two eastern rows of the bays from the first open window of the wave (or the door's two) to two bays
    // past the last.
    int first = 0, last = 1;
    if (cathedralShutter(f, kHeroBay) > 0.f) {
        first = kHeroBay;
        last = kHeroBay;
        while (last + 1 < kWindows && cathedralShutter(f, last + 1) > 0.f)
            ++last;
    }
    std::vector<SceneInstance> casters, others;
    for (int k = 0; k < kBays; ++k)
        for (std::size_t r = 0; r < kPillarRows.size(); ++r) {
            const bool lit = r >= 1 && k >= first && k <= last + 2;
            (lit ? casters : others).push_back(pillars_[std::size_t(k) * kPillarRows.size() + r]);
        }
    out.instances = casters;
    out.instances.insert(out.instances.end(), others.begin(), others.end());
    out.instances.insert(out.instances.end(), cathedral_.begin(), cathedral_.end());
    // The east shutters, outside the glass, sliding up as they open.
    for (int k = 0; k < kWindows; ++k) {
        const float lift = kShutterTravel * cathedralShutter(f, k);
        out.instances.push_back({kUnitBox, kShutterWood,
                                 place({kNaveHalf + kWallThickness + 0.07f, 0.5f * (kWindowBottom + kWindowTop) + lift,
                                        windowZ(k)},
                                       {0.1f, kWindowTop - kWindowBottom + 0.3f, kWindowWidth + 0.4f})});
    }
    for (int k = 0; k < kWindows; ++k)
        addLight(out, kQuad, kLampEmitter,
                 place({0, kLampHeight + 0.03f, windowZ(k)}, rotate({1, 0, 0}, 0.5f * kPi), {1, 1, 1}), kLampSize,
                 kLampSize, out.materials);

    EnvironmentDesc e = environmentDefaults(EnvironmentKind::Day);
    e.discDirection = normalize(kCathedralSun);
    e.glow = 0.6f;
    e.select = 0.5f; // the sun and the sky through the open windows' glass (shadow rays pass the panes)
    out.environment = e;
    // Incense haze, thinning towards the vault: the sun through the stained glass draws coloured shafts.
    FogDesc fog;
    fog.density = kHazeDensity;
    fog.falloff = kHazeFalloff;
    fog.albedo = {0.93f, 0.94f, 0.95f};
    fog.phaseG = 0.55f;
    out.medium = MediumFrame{fog, std::nullopt};

    CausticFrame c;
    CausticReceiver floor = receiverForQuad(
        place({0, 0, 0.5f * kNaveLength}, rotate({1, 0, 0}, -0.5f * kPi), {1, 1, 1}), 2 * kNaveHalf, kNaveLength,
        kCathedralResolution);
    floor.chromaRadius = 3;
    CausticReceiver wall = receiverForQuad(
        place({-kNaveHalf, 0.5f * (kSpringHeight + 0.6f), 0.5f * kNaveLength}, rotate({0, 1, 0}, 0.5f * kPi),
              {1, 1, 1}),
        kNaveLength, kSpringHeight + 0.6f, kCathedralResolution);
    wall.chromaRadius = 3;
    c.receivers = {floor, wall};
    c.photons = photonsAt(kCathedralPhotons, detail_);
    c.light = -1;
    c.casterInstanceFirst = 0;
    c.casterInstanceCount = static_cast<std::uint32_t>(casters.size());
    out.caustics = c;
    return out;
}

SceneFrame Glass::crystalFrame(const show::TimelineFrame& f) const {
    SceneFrame out;
    out.camera = crystalCamera(f);
    out.exposureEV = glassExposureEV(show::Effect::CrystalCity);
    // The towers' own light through their glass is path traced (only the sculpture's is in the photon map):
    // a low clamp keeps it from sparkling on the paving.
    out.clampIndirect = 1.f;
    out.materials = static_.materials;
    // The plaza's sculpture first (the photons' casters): the glenz star in flint around its turned core,
    // turning slowly on a stone drum.
    const auto local = static_cast<float>(f.localSeconds);
    const rt::Mat34 sculpture =
        place(kPlaza + Vec3{0.f, kSculptureLift, 0.f},
              compose(rotate({0, 1, 0}, 0.12f * local), rotate({1, 0, 1}, 0.6155f)), Vec3{1, 1, 1} * kSculptureScale);
    out.instances.push_back({kGlenzStar, kFlint, sculpture});
    out.instances.push_back(
        {kGlenzStar, kAmberCore,
         compose(sculpture, compose(rotate({0, 1, 0}, kGlenzCoreTurn), scale(Vec3{1, 1, 1} * kGlenzCoreScale)))});
    out.instances.push_back({kPillarShaft, kLimestone, place(kPlaza, {3.4f, 0.08f, 3.4f})});
    for (std::uint32_t i = 0; i < towers_.size(); ++i) {
        const Tower& t = towers_[i];
        const std::uint32_t variant = hashU(i * 7919u) % kCrystalVariants;
        out.instances.push_back({kCrystal + variant, kQuartz,
                                 place(t.base, rotate({0, 1, 0}, t.twist), {t.radius, t.height, t.radius})});
    }
    struct Patch {
        float x0, x1, z0, z1;
    };
    const Patch p{kPlaza.x - kPlazaHalf, kPlaza.x + kPlazaHalf, kPlaza.z - kPlazaHalf, kPlaza.z + kPlazaHalf};
    // The ground: the plaza (the receiver), and the plain paving around it out to the haze.
    const rt::Mat34 flat = rotate({1, 0, 0}, -0.5f * kPi);
    out.instances.push_back(
        {kQuad, kPaving, place({0.5f * (p.x0 + p.x1), 0, 0.5f * (p.z0 + p.z1)}, flat, {p.x1 - p.x0, p.z1 - p.z0, 1})});
    const float far = 3000.f; // to the horizon: the haze, not an edge, ends it
    auto ground = [&](float x0, float x1, float z0, float z1) {
        out.instances.push_back(
            {kQuad, kPavingOutside, place({0.5f * (x0 + x1), 0, 0.5f * (z0 + z1)}, flat, {x1 - x0, z1 - z0, 1})});
    };
    ground(-far, far, -far, p.z0);
    ground(-far, far, p.z1, far);
    ground(-far, p.x0, p.z0, p.z1);
    ground(p.x1, far, p.z0, p.z1);

    // Sunrise, bar by bar: each downbeat lifts the sun 1.4 degrees and shrinks its hazy disc (the caustics
    // sharpen); on the accent's downbeat it clears the haze.
    const float bars = barsSince(f, 0.0);
    const float d = downbeatBefore(f, f.accentSeconds);
    const float clear = smooth((local - d) / 0.4f);
    const float elevation = (5.f + 1.4f * bars) * kDegrees;
    const float azimuth = 16.f * kDegrees;
    EnvironmentDesc e = environmentDefaults(EnvironmentKind::Dusk);
    e.discDirection = {std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                       std::cos(elevation) * std::cos(azimuth)};
    const float radius = std::max(0.55f, 1.5f - 0.1f * bars) * kDegrees;
    e.discAngularRadius = radius;
    // The disc's flux grows as the haze thins: its radiance over its solid angle's growth, times the haze.
    const float haze = 0.35f + 0.65f * clear;
    e.discRadiance =
        Vec3{17000.f, 11500.f, 6800.f} * (haze * (0.6f * kDegrees * 0.6f * kDegrees) / (radius * radius));
    e.townDensity = 0.f;
    e.starDensity = 0.f;
    // A clear dawn: deep blue overhead, gold only low along the horizon.
    e.zenith = {0.05f, 0.1f, 0.25f};
    e.horizon = {0.42f, 0.27f, 0.12f};
    e.ground = {0.03f, 0.028f, 0.025f};
    e.haze = 0.15f;
    e.select = 0.5f;
    out.environment = e;

    CausticFrame c;
    CausticReceiver r = receiverForQuad(place(kPlaza, flat, {1, 1, 1}), 2 * kPlazaHalf, 2 * kPlazaHalf,
                                        kCrystalResolution);
    r.chromaRadius = 3;
    c.receivers.push_back(r);
    c.photons = photonsAt(kCrystalPhotons, detail_);
    c.light = -1;
    c.casterInstanceFirst = 0;
    c.casterInstanceCount = 2;
    out.caustics = c;
    limitReceivers(out.materials, c.receivers.size());
    return out;
}

// --- The epoch cards
// ----------------------------------------------------------------------------------------------

std::string Glass::nowCard(show::Effect effect) const {
    const bool shown = effect == show::Effect::Glenz || effect == show::Effect::Cathedral ||
                       effect == show::Effect::CrystalCity;
    return shown ? glassCard(effect).line : std::string();
}

} // namespace evo::modern
