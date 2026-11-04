#include "modern/scenes/ship_asset.hpp"

#include "modern/mesh_builder.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace evo::modern {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float smooth(float x) {
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// --- A shape: points with uvs, triangles with a material slot
// -------------------------------------------------

struct Shape {
    std::vector<Vec3> positions;
    std::vector<Vec2> uvs;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::vector<std::uint32_t> slots; // per triangle: which material (and so which mesh) it belongs to
};

std::uint32_t addPoint(Shape& s, Vec3 p, Vec2 uv) {
    s.positions.push_back(p);
    s.uvs.push_back(uv);
    return static_cast<std::uint32_t>(s.positions.size() - 1);
}

void addTriangle(Shape& s, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t slot) {
    s.triangles.push_back({a, b, c});
    s.slots.push_back(slot);
}

// Rows of points (the same count on every row) joined by quads; `closed` joins each row's last point to its
// first. slotOf(row, column) names each quad's material. Returns the index of the first point.
template <typename SlotOf>
std::uint32_t addGrid(Shape& s, const std::vector<std::vector<Vec3>>& rows,
                      const std::vector<std::vector<Vec2>>& uvs, bool closed, SlotOf slotOf) {
    const auto first = static_cast<std::uint32_t>(s.positions.size());
    const auto columns = static_cast<std::uint32_t>(rows.front().size());
    for (std::size_t i = 0; i < rows.size(); ++i)
        for (std::size_t j = 0; j < columns; ++j)
            addPoint(s, rows[i][j], uvs[i][j]);
    const std::uint32_t quads = closed ? columns : columns - 1;
    for (std::uint32_t i = 0; i + 1 < rows.size(); ++i)
        for (std::uint32_t j = 0; j < quads; ++j) {
            const std::uint32_t jn = (j + 1) % columns;
            const std::uint32_t a = first + i * columns + j, b = first + i * columns + jn;
            const std::uint32_t c = first + (i + 1) * columns + jn, d = first + (i + 1) * columns + j;
            const std::uint32_t slot = slotOf(i, j);
            addTriangle(s, a, b, c, slot);
            addTriangle(s, a, c, d, slot);
        }
    return first;
}

// A fan from a new apex to a row of `count` points from `first` (closed: the last joins the first).
void addFan(Shape& s, Vec3 apex, Vec2 uv, std::uint32_t first, std::uint32_t count, bool closed,
            std::uint32_t slot) {
    const std::uint32_t centre = addPoint(s, apex, uv);
    const std::uint32_t n = closed ? count : count - 1;
    for (std::uint32_t j = 0; j < n; ++j)
        addTriangle(s, centre, first + (j + 1) % count, first + j, slot);
}

// Twice the signed volume of the triangles from `begin` on (positive: counter-clockwise seen from outside).
double signedVolume(const Shape& s, std::size_t begin) {
    double volume = 0;
    for (std::size_t t = begin; t < s.triangles.size(); ++t) {
        const Vec3 a = s.positions[s.triangles[t][0]], b = s.positions[s.triangles[t][1]],
                   c = s.positions[s.triangles[t][2]];
        volume += double(dot(a, cross(b, c)));
    }
    return volume;
}

// Turns the closed part made of the triangles from `begin` on to face outwards (inwards when asked: a glass
// shell's inner wall faces into the cavity, out of the glass).
void orient(Shape& s, std::size_t begin, bool inwards = false) {
    if ((signedVolume(s, begin) < 0) != inwards)
        for (std::size_t t = begin; t < s.triangles.size(); ++t)
            std::swap(s.triangles[t][1], s.triangles[t][2]);
}

// The shape's meshes, one per slot: smooth normals, area weighted over the triangles at the same position
// (so a duplicated seam stays smooth) whose faces lie within `creaseDegrees` of each other (a sharper edge
// stays sharp).
std::vector<rt::Mesh> toMeshes(const Shape& s, std::uint32_t slotCount, float creaseDegrees) {
    const std::size_t triangles = s.triangles.size();
    std::vector<Vec3> faceCross(triangles), faceUnit(triangles);
    std::map<std::tuple<float, float, float>, std::vector<std::uint32_t>> around;
    const auto key = [&](std::uint32_t p) {
        return std::make_tuple(s.positions[p].x, s.positions[p].y, s.positions[p].z);
    };
    for (std::size_t t = 0; t < triangles; ++t) {
        const auto& tri = s.triangles[t];
        const Vec3 n =
            cross(s.positions[tri[1]] - s.positions[tri[0]], s.positions[tri[2]] - s.positions[tri[0]]);
        faceCross[t] = n;
        const float len = length(n);
        faceUnit[t] = len > 0 ? n / len : Vec3{};
        for (const std::uint32_t p : tri)
            around[key(p)].push_back(static_cast<std::uint32_t>(t));
    }
    const float cosCrease = std::cos(creaseDegrees * kPi / 180.f);
    std::vector<rt::Mesh> meshes(slotCount);
    std::vector<std::map<std::tuple<std::uint32_t, float, float, float>, std::uint32_t>> seen(slotCount);
    for (std::size_t t = 0; t < triangles; ++t) {
        const std::uint32_t slot = s.slots[t];
        rt::Mesh& mesh = meshes[slot];
        std::array<std::uint32_t, 3> out{};
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t p = s.triangles[t][k];
            Vec3 sum{};
            for (const std::uint32_t o : around[key(p)])
                if (dot(faceUnit[o], faceUnit[t]) >= cosCrease)
                    sum = sum + faceCross[o];
            const float len = length(sum);
            const Vec3 n = len > 0 ? sum / len : faceUnit[t];
            const auto vertexKey = std::make_tuple(p, n.x, n.y, n.z);
            const auto found = seen[slot].find(vertexKey);
            if (found != seen[slot].end()) {
                out[k] = found->second;
                continue;
            }
            const Vec3 q = s.positions[p];
            const Vec2 uv = s.uvs[p];
            mesh.vertices.push_back({{q.x, q.y, q.z}, {n.x, n.y, n.z}, {uv.x, uv.y}});
            out[k] = static_cast<std::uint32_t>(mesh.vertices.size() - 1);
            seen[slot].emplace(vertexKey, out[k]);
        }
        mesh.indices.insert(mesh.indices.end(), out.begin(), out.end());
    }
    return meshes;
}

// --- The design (1993 model units; kScale metres each) ----------------------------------------------------
//
// The 1993 fighter (retro/flyby.cpp): a fuselage from a needle nose at z = 2.5 through its widest section at
// z = -0.3 to the tail at z = -1.9, the canopy on its back ahead of the widest section, two wings swept back
// hard with a little dihedral, and an engine pod hung under each wing with its nozzle at z = -2.05.

constexpr float S = 4.f; // metres per unit

// The fuselage's scale along z (0 at the nose, 1 at the widest section): a Catmull-Rom curve through the 1993
// sections, smoothed into an ogive.
struct Knot {
    float z, scale;
};
constexpr std::array<Knot, 10> kFuselageKnots = {{
    {2.62f, 0.f},
    {2.35f, 0.17f},
    {1.9f, 0.34f},
    {1.0f, 0.62f},
    {0.3f, 0.87f},
    {-0.3f, 1.f},
    {-0.9f, 0.99f},
    {-1.5f, 0.93f},
    {-1.9f, 0.86f},
    {-2.0f, 0.84f},
}};

float fuselageScale(float z) {
    const std::size_t n = kFuselageKnots.size();
    if (z >= kFuselageKnots.front().z)
        return 0.f;
    if (z <= kFuselageKnots.back().z)
        return kFuselageKnots.back().scale;
    std::size_t i = 0;
    while (i + 2 < n && z < kFuselageKnots[i + 1].z)
        ++i;
    const float z0 = kFuselageKnots[i].z, z1 = kFuselageKnots[i + 1].z;
    const float t = (z0 - z) / (z0 - z1);
    const float p0 = kFuselageKnots[i == 0 ? 0 : i - 1].scale, p1 = kFuselageKnots[i].scale,
                p2 = kFuselageKnots[i + 1].scale, p3 = kFuselageKnots[std::min(i + 2, n - 1)].scale;
    const float t2 = t * t, t3 = t2 * t;
    return std::max(0.f, 0.5f * ((2.f * p1) + (-p0 + p2) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                                 (-p0 + 3.f * p1 - 3.f * p2 + p3) * t3));
}

// A point of the fuselage's section at z and angle phi (0 on top, pi / 2 towards +x, +-pi under the keel):
// a superellipse, flatter on the back than the keel, with a chine (a ridge along the sides, sharpest where
// top and keel meet) and, behind the canopy, a dorsal spine.
constexpr float kFuselageHalfWidth = 0.34f, kFuselageTop = 0.30f, kFuselageKeel = 0.24f;
constexpr float kFuselageNoseZ = 2.6f, kFuselageTailZ = -2.0f;

float spineHeight(float z, float phi) {
    const float along = smooth((0.05f - z) / 0.6f) * smooth((z + 2.0f) / 0.25f);
    return 0.075f * along * std::exp(-(phi / 0.2f) * (phi / 0.2f));
}

Vec3 fuselagePoint(float z, float phi, float inflate = 1.f) {
    const float k = std::max(fuselageScale(z), 0.012f) * inflate;
    const float up = std::cos(phi), side = std::sin(phi);
    const float e = 2.f / 2.5f;
    float x = kFuselageHalfWidth * k * std::copysign(std::pow(std::abs(side), e), side);
    float y = (up >= 0 ? kFuselageTop : kFuselageKeel) * k * std::copysign(std::pow(std::abs(up), e), up);
    const float chine = 0.16f * std::pow(1.f - std::abs(up), 8.f) * smooth((2.45f - z) / 0.8f);
    x *= 1.f + chine;
    y += spineHeight(z, phi) * k;
    return Vec3{x, y, z} * S;
}

// The canopy: an ellipsoid on the fuselage's back (its lower half sunk into the hull), longer behind its
// widest point than ahead of it; a glass shell kCanopyWall thick.
constexpr Vec3 kCanopyCentre{0.f, 0.245f, 0.38f};
constexpr float kCanopyHalfWidth = 0.15f, kCanopyHeight = 0.135f, kCanopyFront = 0.55f, kCanopyBack = 0.75f;
constexpr float kCanopyWall = 0.008f;

// The wings: the 1993 wing (root edge z 0.60 .. -1.70, tip 1.80 .. 1.95 at z -1.25 .. -1.85) with a curved
// root extension running forward along the chine, a NACA section thinning to the tip, and dihedral.
constexpr float kWingRootX = 0.16f, kWingTipX = 1.86f;

float wingLeadingZ(float x) {
    const float lerx = 0.75f * std::exp(-((x - 0.16f) / 0.2f) * ((x - 0.16f) / 0.2f));
    return 0.60f + (x - 0.225f) * (-1.25f - 0.60f) / (1.80f - 0.225f) + lerx;
}
float wingTrailingZ(float x) {
    return -1.70f + (x - 0.28f) * (-1.85f + 1.70f) / (1.95f - 0.28f);
}
float wingDihedralY(float x) {
    return 0.10f * (x - kWingRootX) / (kWingTipX - kWingRootX);
}
float wingThickness(float x) {
    return lerp(0.11f, 0.026f, (x - kWingRootX) / (kWingTipX - kWingRootX));
}

// NACA 4-digit half thickness at chord fraction u (closed trailing edge) for a maximum thickness `t`.
float nacaHalfThickness(float u, float t) {
    return 5.f * t *
           (0.2969f * std::sqrt(u) - 0.1260f * u - 0.3516f * u * u + 0.2843f * u * u * u -
            0.1036f * u * u * u * u);
}

// The engine nacelles: under each wing on the 1993 pod's centre line, revolved: a chrome spinner and intake
// lip, the duct, the titanium body, the nozzle's outer bell, its glowing inner wall and the white-hot throat.
constexpr float kPodX = 0.74f, kPodY = -0.13f;
constexpr float kNozzleExitZ = -2.07f;
struct ProfilePoint {
    float r, z;
};
constexpr std::array<ProfilePoint, 24> kPodProfile = {{
    {0.f, -0.32f},     // 0 spinner tip
    {0.022f, -0.35f},  // 1
    {0.04f, -0.40f},   // 2
    {0.05f, -0.46f},   // 3 spinner base
    {0.074f, -0.47f},  // 4 duct floor
    {0.084f, -0.41f},  // 5 duct wall
    {0.094f, -0.36f},  // 6 lip inner
    {0.104f, -0.338f}, // 7 lip front
    {0.114f, -0.342f}, // 8
    {0.121f, -0.365f}, // 9 lip outer
    {0.124f, -0.42f},  // 10 body
    {0.126f, -0.8f},   // 11
    {0.126f, -1.2f},   // 12
    {0.123f, -1.55f},  // 13
    {0.117f, -1.74f},  // 14 collar
    {0.111f, -1.80f},  // 15 nozzle start
    {0.113f, -1.92f},  // 16
    {0.119f, -2.07f},  // 17 nozzle exit
    {0.110f, -2.075f}, // 18 exit lip inner
    {0.097f, -2.0f},   // 19 inner wall
    {0.08f, -1.93f},   // 20
    {0.064f, -1.885f}, // 21 throat
    {0.03f, -1.88f},   // 22
    {0.f, -1.879f},    // 23 throat plate centre
}};

// --- Materials and meshes
// -----------------------------------------------------------------------------------

enum Material : std::uint32_t {
    kTitanium,
    kTitaniumHot,
    kCeramic,
    kChrome,
    kMirror,
    kDuct,
    kNozzle,
    kNozzleInner,
    kThroat,
    kCanopyGlass,
    kCockpit,
    kSeat,
    kScreenAmber,
    kScreenWhite,
    kDashLight,
    kNavRed,
    kNavGreen,
    kStrobe,
    kMaterialCount
};

enum Mesh : std::uint32_t {
    kFuselageTitanium,
    kFuselageCeramic,
    kTailFairing,
    kWingTitanium,
    kWingCeramic,
    kFinTitanium,
    kFinCeramic,
    kCanopyFrame,
    kPodChrome,
    kPodDuct,
    kPodBody,
    kPodNozzle,
    kPodNozzleInner,
    kPodThroat,
    kCanopy,
    kCockpitTub,
    kSeatMesh,
    kHeadrest,
    kConsole,
    kGlareShield,
    kScreen,
    kDashQuad,
    kNavLight,
    kMeshCount
};

rt::Material plastic(Vec3 color, float roughness) {
    rt::Material m;
    m.baseColor[0] = color.x, m.baseColor[1] = color.y, m.baseColor[2] = color.z;
    m.roughness = roughness;
    return m;
}

rt::Material metal(Vec3 color, float roughness) {
    rt::Material m = plastic(color, roughness);
    m.metallic = 1.f;
    return m;
}

// Seen, never sampled as a light (the integrator's camera-only emission).
rt::Material emitter(Vec3 color, float scale) {
    rt::Material m = plastic({0.02f, 0.02f, 0.02f}, 0.4f);
    m.emission[0] = color.x, m.emission[1] = color.y, m.emission[2] = color.z;
    m.emissionScale = scale;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    m.flags = rt::kMaterialCameraOnlyEmission;
    return m;
}

// Emission scales the per-frame table modulates.
constexpr float kGlowIdle = 1.4f, kGlowKick = 7.f, kStrobeEmission = 60.f, kNavEmission = 14.f;
constexpr float kThroatEmission = 34.f, kNozzleInnerEmission = 4.f;
constexpr Vec3 kDashEmission{8.f, 6.4f, 4.4f};
constexpr float kDashWidth = 0.6f, kDashHeight = 0.12f;

std::vector<rt::Material> buildMaterials() {
    std::vector<rt::Material> m(kMaterialCount);
    const auto hull = [](rt::Material& r, float panel) {
        r.pattern = static_cast<std::uint32_t>(rt::Pattern::Hull);
        r.patternScale = panel;
    };
    // Titanium's F0, skin panels 2.2 m long, brushed along the flow.
    m[kTitanium] = metal({0.62f, 0.58f, 0.53f}, 0.38f);
    hull(m[kTitanium], 2.2f);
    m[kTitaniumHot] = m[kTitanium]; // the nacelles: heat tint from the nozzle forward over 2.4 m
    m[kTitaniumHot].baseColor2[0] = 0.95f;
    m[kTitaniumHot].baseColor2[1] = 2.4f;
    // Black ceramic tiles 0.62 m square under a satin coat, scuffed paler; their seams glow amber.
    m[kCeramic] = plastic({0.03f, 0.032f, 0.036f}, 0.55f);
    m[kCeramic].coat = 0.3f;
    m[kCeramic].coatRoughness = 0.25f;
    hull(m[kCeramic], 0.62f);
    m[kCeramic].baseColor2[0] = 0.075f, m[kCeramic].baseColor2[1] = 0.07f, m[kCeramic].baseColor2[2] = 0.065f;
    m[kCeramic].emission[0] = 1.f, m[kCeramic].emission[1] = 0.55f, m[kCeramic].emission[2] = 0.16f;
    m[kCeramic].emissionScale = kGlowIdle;
    m[kCeramic].flags = rt::kMaterialCameraOnlyEmission;
    m[kChrome] = metal({0.96f, 0.96f, 0.97f}, 0.04f);
    // The tail's exhaust fairing between the nozzles, mirror polished: it shows the plumes as a reflection
    // (a delta lobe) instead of catching them by chance, which would sparkle.
    m[kMirror] = metal({0.93f, 0.9f, 0.86f}, 0.01f);
    m[kDuct] = metal({0.09f, 0.09f, 0.1f}, 0.5f);
    m[kNozzle] = metal({0.44f, 0.38f, 0.34f}, 0.34f); // the petals and bell: hottest titanium
    hull(m[kNozzle], 0.5f);
    m[kNozzle].baseColor2[0] = 1.f;
    m[kNozzle].baseColor2[1] = 1.4f;
    m[kNozzleInner] = emitter({1.f, 0.42f, 0.12f}, kNozzleInnerEmission);
    m[kThroat] = emitter({0.78f, 0.86f, 1.f}, kThroatEmission);
    // A gold-tinted canopy (the 1993 canopy's gold).
    m[kCanopyGlass] = plastic({1.f, 0.88f, 0.62f}, 0.f);
    m[kCanopyGlass].transmission = 1.f;
    m[kCanopyGlass].ior = 1.5f;
    m[kCanopyGlass].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Glass);
    m[kCockpit] = plastic({0.045f, 0.047f, 0.05f}, 0.6f);
    m[kSeat] = plastic({0.11f, 0.06f, 0.035f}, 0.7f);
    m[kScreenAmber] = emitter({1.f, 0.62f, 0.22f}, 3.f);
    m[kScreenWhite] = emitter({0.86f, 0.9f, 1.f}, 2.5f);
    m[kDashLight] = plastic({0.02f, 0.02f, 0.02f}, 0.4f); // a light: emits as the rect light does
    m[kDashLight].emission[0] = kDashEmission.x, m[kDashLight].emission[1] = kDashEmission.y;
    m[kDashLight].emission[2] = kDashEmission.z;
    m[kDashLight].emissionScale = 1.f;
    m[kDashLight].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    m[kNavRed] = emitter({1.f, 0.06f, 0.03f}, kNavEmission);
    m[kNavGreen] = emitter({0.05f, 1.f, 0.2f}, kNavEmission);
    m[kStrobe] = emitter({1.f, 1.f, 1.f}, kStrobeEmission);
    return m;
}

// The fuselage. Slots: titanium 0, ceramic 1 (the belly and a strip along the spine), the tail fairing 2. uv
// in metres: u along z, v around the section (about its arc length, 0 on top).
constexpr int kFuselageRows = 96, kFuselageColumns = 96;

float rowZ(int i) {
    const float u = static_cast<float>(i) / static_cast<float>(kFuselageRows - 1);
    const float w = 1.f - (1.f - u) * (1.f - u) * 0.6f - 0.4f * (1.f - u); // denser towards the nose
    return lerp(kFuselageNoseZ - 0.02f, kFuselageTailZ, w);
}

float columnPhi(int j) {
    return -kPi + 2.f * kPi * static_cast<float>(j) / static_cast<float>(kFuselageColumns);
}

Vec2 fuselageUv(float z, float phi) {
    return {z * S, phi * S * 0.3f * std::max(fuselageScale(z), 0.05f)};
}

void buildFuselage(Shape& s) {
    const std::size_t begin = s.triangles.size();
    std::vector<std::vector<Vec3>> rows;
    std::vector<std::vector<Vec2>> uvs;
    for (int i = 0; i <= kFuselageRows; ++i) {
        const bool tailRing = i == kFuselageRows; // inset for the tail cap's bevel
        const float z = tailRing ? kFuselageTailZ : rowZ(i);
        std::vector<Vec3> row;
        std::vector<Vec2> uv;
        for (int j = 0; j <= kFuselageColumns; ++j) { // the keel's seam duplicated: uvs do not wrap
            const float phi = columnPhi(j);
            row.push_back(tailRing ? fuselagePoint(z, phi, 0.78f) + Vec3{0, 0, -0.06f * S}
                                   : fuselagePoint(z, phi));
            uv.push_back(fuselageUv(z, phi));
        }
        rows.push_back(std::move(row));
        uvs.push_back(std::move(uv));
    }
    const auto columns = static_cast<std::uint32_t>(kFuselageColumns + 1);
    const std::uint32_t first = addGrid(s, rows, uvs, false, [](std::uint32_t i, std::uint32_t j) {
        const float phi = columnPhi(int(j)) + kPi / static_cast<float>(kFuselageColumns);
        const float z = 0.5f * (rowZ(int(i)) + rowZ(std::min(int(i) + 1, kFuselageRows - 1)));
        if (int(i) == kFuselageRows - 1)
            return 2u; // the tail's bevel
        const bool belly = std::cos(phi) < -0.46f;
        const bool spine = std::abs(phi) < 0.17f && z < -0.35f && z > -1.95f;
        return belly || spine ? 1u : 0u;
    });
    addFan(s, Vec3{0, 0, kFuselageNoseZ} * S, {kFuselageNoseZ * S, 0.f}, first, columns, false, 0);
    const std::uint32_t last = first + columns * static_cast<std::uint32_t>(rows.size() - 1);
    const Vec3 tail = fuselagePoint(kFuselageTailZ, 0.5f * kPi, 0.f) + Vec3{0, 0, -0.06f * S};
    addFan(s, Vec3{0.f, tail.y, tail.z}, {tail.z, 0.f}, last, columns, false, 2);
    orient(s, begin);
}

// A wing (side +1 on +x, port; -1 on -x, starboard). Slots: titanium 0 (the top), ceramic 1 (the underside
// and the leading edge's band on both sides). uv: u along z, v along the span.
constexpr int kWingRows = 36, kWingColumns = 64;

// The point at span x and section column k (0 the trailing edge, round the top to the leading edge and back
// under); `top` reports which side it is on and `chord` its chord fraction from the leading edge.
Vec3 wingPoint(float x, int k, bool* top = nullptr, float* chord = nullptr) {
    const float le = wingLeadingZ(x), te = wingTrailingZ(x), c = le - te;
    const float phi = 2.f * kPi * static_cast<float>(k) / static_cast<float>(kWingColumns);
    const float u = 0.5f * (1.f + std::cos(phi)); // 1 at the trailing edge, 0 at the leading edge
    const float half = nacaHalfThickness(u, wingThickness(x));
    if (top)
        *top = std::sin(phi) >= 0;
    if (chord)
        *chord = u;
    const float y = wingDihedralY(x) + (std::sin(phi) >= 0 ? half : -half);
    return Vec3{x, y, le - u * c};
}

float wingSpan(int i) {
    const float t = static_cast<float>(i) / static_cast<float>(kWingRows - 1);
    return lerp(kWingRootX, kWingTipX, 1.f - (1.f - t) * (1.f - t) * 0.3f - 0.7f * (1.f - t));
}

void buildWing(Shape& s, float side) {
    const std::size_t begin = s.triangles.size();
    std::vector<std::vector<Vec3>> rows;
    std::vector<std::vector<Vec2>> uvs;
    for (int i = 0; i < kWingRows; ++i) {
        const float x = wingSpan(i);
        std::vector<Vec3> row;
        std::vector<Vec2> uv;
        for (int k = 0; k < kWingColumns; ++k) {
            const Vec3 p = wingPoint(x, k);
            row.push_back(Vec3{side * p.x, p.y, p.z} * S);
            uv.push_back({p.z * S, p.x * S});
        }
        rows.push_back(std::move(row));
        uvs.push_back(std::move(uv));
    }
    const auto columns = static_cast<std::uint32_t>(kWingColumns);
    const std::uint32_t first = addGrid(s, rows, uvs, true, [](std::uint32_t, std::uint32_t k) {
        const float phi = 2.f * kPi * (static_cast<float>(k) + 0.5f) / static_cast<float>(kWingColumns);
        const float u = 0.5f * (1.f + std::cos(phi));
        return std::sin(phi) > 0 && u > 0.1f ? 0u : 1u;
    });
    const auto centre = [&](const std::vector<Vec3>& row) {
        Vec3 c{};
        for (const Vec3& p : row)
            c = c + p;
        return c / static_cast<float>(row.size());
    };
    const Vec3 root = centre(rows.front()), tip = centre(rows.back());
    addFan(s, root, {root.z, std::abs(root.x)}, first, columns, true, 1);
    addFan(s, tip, {tip.z, std::abs(tip.x)}, first + columns * static_cast<std::uint32_t>(kWingRows - 1),
           columns, true, 1);
    orient(s, begin);
}

// Twin fins on the rear fuselage, canted outwards. Slots: titanium 0, ceramic 1 (the leading edge).
constexpr float kFinCant = 0.38f, kFinHeight = 0.5f, kFinRootX = 0.25f, kFinRootY = 0.16f;
constexpr int kFinRows = 16, kFinColumns = 40;

void buildFin(Shape& s, float side) {
    const std::size_t begin = s.triangles.size();
    const Vec3 span{side * std::sin(kFinCant), std::cos(kFinCant), 0.f};
    const Vec3 across{std::cos(kFinCant) * side, -std::sin(kFinCant), 0.f};
    std::vector<std::vector<Vec3>> rows;
    std::vector<std::vector<Vec2>> uvs;
    for (int i = 0; i < kFinRows; ++i) {
        const float h = static_cast<float>(i) / static_cast<float>(kFinRows - 1);
        const float le = lerp(-0.95f, -1.55f, h), te = lerp(-1.93f, -1.98f, h), c = le - te;
        const Vec3 base = Vec3{side * kFinRootX, kFinRootY, 0.f} + span * (h * kFinHeight);
        std::vector<Vec3> row;
        std::vector<Vec2> uv;
        for (int k = 0; k < kFinColumns; ++k) {
            const float phi = 2.f * kPi * static_cast<float>(k) / static_cast<float>(kFinColumns);
            const float u = 0.5f * (1.f + std::cos(phi));
            const float half = nacaHalfThickness(u, 0.045f * c) * (std::sin(phi) >= 0 ? 1.f : -1.f);
            const Vec3 p = base + across * half + Vec3{0.f, 0.f, le - u * c};
            row.push_back(p * S);
            uv.push_back({p.z * S, h * kFinHeight * S});
        }
        rows.push_back(std::move(row));
        uvs.push_back(std::move(uv));
    }
    const auto columns = static_cast<std::uint32_t>(kFinColumns);
    const std::uint32_t first = addGrid(s, rows, uvs, true, [](std::uint32_t, std::uint32_t k) {
        const float phi = 2.f * kPi * (static_cast<float>(k) + 0.5f) / static_cast<float>(kFinColumns);
        return 0.5f * (1.f + std::cos(phi)) < 0.12f ? 1u : 0u;
    });
    const auto centre = [&](const std::vector<Vec3>& row) {
        Vec3 c{};
        for (const Vec3& p : row)
            c = c + p;
        return c / static_cast<float>(row.size());
    };
    const Vec3 root = centre(rows.front()), tip = centre(rows.back());
    addFan(s, root, {root.z, 0.f}, first, columns, true, 0);
    addFan(s, tip, {tip.z, kFinHeight * S}, first + columns * static_cast<std::uint32_t>(kFinRows - 1),
           columns, true, 0);
    orient(s, begin);
}

Vec3 finTip(float side) {
    return (Vec3{side * kFinRootX, kFinRootY, -1.82f} +
            Vec3{side * std::sin(kFinCant), std::cos(kFinCant), 0.f} * kFinHeight) *
           S;
}

// A nacelle (side +1 on +x, port; -1 on -x, starboard) revolved about its axis, with nozzle petals. Slots:
// chrome 0, duct 1, body 2, nozzle 3, nozzle inner 4, throat 5. uv: u metres forward of the nozzle's exit
// (the heat tint's distance), v around.
constexpr int kPodSegments = 64, kPetals = 18;

std::uint32_t podSlot(std::size_t segment) {
    if (segment < 3)
        return 0; // spinner
    if (segment < 6)
        return 1; // duct
    if (segment < 10)
        return 0; // intake lip
    if (segment < 15)
        return 2; // body
    if (segment < 18)
        return 3; // nozzle outside
    if (segment < 21)
        return 4; // nozzle inside
    return 5;     // throat
}

void buildPod(Shape& s, float side) {
    const std::size_t begin = s.triangles.size();
    const Vec3 axis{side * kPodX, kPodY, 0.f};
    const auto ring = [&](const ProfilePoint& p, std::vector<Vec2>& uv) {
        std::vector<Vec3> row;
        for (int j = 0; j <= kPodSegments; ++j) {
            const float a = 2.f * kPi * static_cast<float>(j) / static_cast<float>(kPodSegments);
            row.push_back((axis + Vec3{p.r * std::cos(a), p.r * std::sin(a), p.z}) * S);
            uv.push_back({(p.z - kNozzleExitZ) * S, a * 0.125f * S});
        }
        return row;
    };
    std::vector<std::vector<Vec3>> rows;
    std::vector<std::vector<Vec2>> uvs;
    for (std::size_t i = 1; i + 1 < kPodProfile.size(); ++i) {
        std::vector<Vec2> uv;
        rows.push_back(ring(kPodProfile[i], uv));
        uvs.push_back(std::move(uv));
    }
    const auto columns = static_cast<std::uint32_t>(kPodSegments + 1);
    const std::uint32_t first =
        addGrid(s, rows, uvs, false, [](std::uint32_t row, std::uint32_t) { return podSlot(row + 1); });
    addFan(s, (axis + Vec3{0, 0, kPodProfile.front().z}) * S,
           {(kPodProfile.front().z - kNozzleExitZ) * S, 0.f}, first, columns, false, podSlot(0));
    addFan(s, (axis + Vec3{0, 0, kPodProfile.back().z}) * S, {0.f, 0.f},
           first + columns * static_cast<std::uint32_t>(rows.size() - 1), columns, false,
           podSlot(kPodProfile.size() - 2));
    orient(s, begin);
    // The petals: thin plates over the outer bell, one per 20 degrees, a little apart.
    for (int k = 0; k < kPetals; ++k) {
        const float a = 2.f * kPi * (static_cast<float>(k) + 0.5f) / static_cast<float>(kPetals);
        const float half = 0.4f * 2.f * kPi / static_cast<float>(kPetals);
        const auto at = [&](float angle, float r, float z) {
            return addPoint(s, (axis + Vec3{r * std::cos(angle), r * std::sin(angle), z}) * S,
                            {(z - kNozzleExitZ) * S, angle * 0.125f * S});
        };
        const std::uint32_t p0 = at(a - half, 0.1175f, -1.84f), p1 = at(a + half, 0.1175f, -1.84f);
        const std::uint32_t p2 = at(a + half * 0.9f, 0.1235f, -2.08f),
                            p3 = at(a - half * 0.9f, 0.1235f, -2.08f);
        addTriangle(s, p0, p2, p1, 3);
        addTriangle(s, p0, p3, p2, 3);
    }
}

// One wall of the canopy: the ellipsoid shrunk by `inset` on every axis.
void buildCanopyWall(Shape& s, float inset, bool inner) {
    const std::size_t begin = s.triangles.size();
    constexpr int kRings = 32, kColumns = 56;
    const float a = kCanopyHalfWidth - inset, b = kCanopyHeight - inset;
    const float front = kCanopyFront - inset, back = kCanopyBack - inset;
    std::vector<std::vector<Vec3>> rows;
    std::vector<std::vector<Vec2>> uvs;
    for (int i = 1; i < kRings; ++i) {
        const float zeta =
            std::cos(kPi * static_cast<float>(i) / static_cast<float>(kRings)); // 1 front .. -1
        const float rho = std::sqrt(std::max(0.f, 1.f - zeta * zeta));
        const float z = kCanopyCentre.z + (zeta > 0 ? zeta * front : zeta * back);
        std::vector<Vec3> row;
        for (int j = 0; j < kColumns; ++j) {
            const float theta = 2.f * kPi * static_cast<float>(j) / static_cast<float>(kColumns);
            row.push_back(Vec3{a * rho * std::cos(theta), kCanopyCentre.y + b * rho * std::sin(theta), z} *
                          S);
        }
        uvs.emplace_back(row.size(), Vec2{});
        rows.push_back(std::move(row));
    }
    const std::uint32_t first = addGrid(s, rows, uvs, true, [](std::uint32_t, std::uint32_t) { return 0u; });
    const auto columns = static_cast<std::uint32_t>(kColumns);
    addFan(s, Vec3{0, kCanopyCentre.y, kCanopyCentre.z + front} * S, {}, first, columns, true, 0);
    addFan(s, Vec3{0, kCanopyCentre.y, kCanopyCentre.z - back} * S, {},
           first + columns * static_cast<std::uint32_t>(rows.size() - 1), columns, true, 0);
    orient(s, begin, inner);
}

// The canopy's frame: a titanium bow over the glass a little behind its widest section.
void buildCanopyFrame(Shape& s) {
    const std::size_t begin = s.triangles.size();
    constexpr float kZ = 0.25f, kTube = 0.011f;
    const float zeta = (kZ - kCanopyCentre.z) / kCanopyBack;
    const float rho = std::sqrt(std::max(0.f, 1.f - zeta * zeta));
    const float a = kCanopyHalfWidth * rho + 0.004f, b = kCanopyHeight * rho + 0.004f;
    constexpr int kAlong = 28, kAround = 10;
    std::vector<std::vector<Vec3>> rows;
    std::vector<std::vector<Vec2>> uvs;
    for (int i = 0; i < kAlong; ++i) {
        const float t =
            lerp(-0.08f * kPi, 1.08f * kPi, static_cast<float>(i) / static_cast<float>(kAlong - 1));
        const Vec3 centre{a * std::cos(t), kCanopyCentre.y + b * std::sin(t), kZ};
        const Vec3 out = normalize(Vec3{b * std::cos(t), a * std::sin(t), 0.f});
        std::vector<Vec3> row;
        std::vector<Vec2> uv;
        for (int j = 0; j < kAround; ++j) {
            const float c = 2.f * kPi * static_cast<float>(j) / static_cast<float>(kAround);
            row.push_back((centre + out * (kTube * std::cos(c)) + Vec3{0, 0, kTube * std::sin(c)}) * S);
            uv.push_back({kZ * S + kTube * S * std::sin(c), t * 0.5f});
        }
        rows.push_back(std::move(row));
        uvs.push_back(std::move(uv));
    }
    addGrid(s, rows, uvs, true, [](std::uint32_t, std::uint32_t) { return 0u; });
    orient(s, begin);
}

std::vector<rt::Mesh> buildMeshes() {
    std::vector<rt::Mesh> meshes(kMeshCount);
    {
        Shape s;
        buildFuselage(s);
        auto parts = toMeshes(s, 3, 34.f);
        meshes[kFuselageTitanium] = std::move(parts[0]);
        meshes[kFuselageCeramic] = std::move(parts[1]);
        meshes[kTailFairing] = std::move(parts[2]);
    }
    {
        Shape s;
        buildWing(s, 1.f);
        buildWing(s, -1.f);
        auto parts = toMeshes(s, 2, 40.f);
        meshes[kWingTitanium] = std::move(parts[0]);
        meshes[kWingCeramic] = std::move(parts[1]);
    }
    {
        Shape s;
        buildFin(s, 1.f);
        buildFin(s, -1.f);
        auto parts = toMeshes(s, 2, 40.f);
        meshes[kFinTitanium] = std::move(parts[0]);
        meshes[kFinCeramic] = std::move(parts[1]);
    }
    {
        Shape s;
        buildCanopyFrame(s);
        meshes[kCanopyFrame] = std::move(toMeshes(s, 1, 60.f)[0]);
    }
    {
        Shape s;
        buildPod(s, 1.f);
        buildPod(s, -1.f);
        auto parts = toMeshes(s, 6, 38.f);
        meshes[kPodChrome] = std::move(parts[0]);
        meshes[kPodDuct] = std::move(parts[1]);
        meshes[kPodBody] = std::move(parts[2]);
        meshes[kPodNozzle] = std::move(parts[3]);
        meshes[kPodNozzleInner] = std::move(parts[4]);
        meshes[kPodThroat] = std::move(parts[5]);
    }
    {
        Shape s;
        buildCanopyWall(s, 0.f, false);
        buildCanopyWall(s, kCanopyWall, true);
        meshes[kCanopy] = std::move(toMeshes(s, 1, 50.f)[0]);
    }
    meshes[kCockpitTub] = bevelBox(Vec3{0.11f, 0.05f, 0.42f} * S, 0.02f * S, 2);
    meshes[kSeatMesh] = bevelBox(Vec3{0.055f, 0.03f, 0.05f} * S, 0.012f * S, 2);
    meshes[kHeadrest] = bevelBox(Vec3{0.05f, 0.06f, 0.015f} * S, 0.01f * S, 2);
    meshes[kConsole] = bevelBox(Vec3{0.1f, 0.025f, 0.06f} * S, 0.01f * S, 2);
    meshes[kGlareShield] = bevelBox(Vec3{0.1f, 0.008f, 0.035f} * S, 0.005f * S, 2);
    meshes[kScreen] = quad(0.05f * S, 0.035f * S);
    meshes[kDashQuad] = quad(kDashWidth, kDashHeight);
    meshes[kNavLight] = uvSphere(0.018f * S, 12, 24);
    return meshes;
}

rt::Mat34 inMetres(Vec3 units) {
    return translate(units * S);
}

// The instances in model space; `dash` gets the dash light's index among them and its transform.
std::vector<SceneInstance> buildInstances(std::uint32_t& dash, rt::Mat34& dashTransform) {
    std::vector<SceneInstance> out;
    const auto add = [&](std::uint32_t mesh, std::uint32_t material, const rt::Mat34& m = identity()) {
        out.push_back({mesh, material, m});
    };
    add(kFuselageTitanium, kTitanium);
    add(kFuselageCeramic, kCeramic);
    add(kTailFairing, kMirror);
    add(kWingTitanium, kTitanium);
    add(kWingCeramic, kCeramic);
    add(kFinTitanium, kTitanium);
    add(kFinCeramic, kCeramic);
    add(kCanopyFrame, kTitanium);
    add(kPodChrome, kChrome);
    add(kPodDuct, kDuct);
    add(kPodBody, kTitaniumHot);
    add(kPodNozzle, kNozzle);
    add(kPodNozzleInner, kNozzleInner);
    add(kPodThroat, kThroat);
    add(kCanopy, kCanopyGlass);
    // The cockpit: a tub, the seat and headrest, the console with two screens under a glare shield, and the
    // dash light under the shield, shining back and down onto the seat.
    const Vec3 c = kCanopyCentre;
    add(kCockpitTub, kCockpit, inMetres(c + Vec3{0.f, -0.045f, -0.02f}));
    add(kSeatMesh, kSeat, inMetres(c + Vec3{0.f, 0.02f, -0.2f}));
    add(kHeadrest, kSeat, compose(inMetres(c + Vec3{0.f, 0.07f, -0.27f}), rotate({1, 0, 0}, 0.25f)));
    const rt::Mat34 console = compose(inMetres(c + Vec3{0.f, 0.03f, 0.2f}), rotate({1, 0, 0}, 0.45f));
    add(kConsole, kCockpit, console);
    for (const float x : {-0.05f, 0.05f})
        add(kScreen, x < 0 ? kScreenAmber : kScreenWhite,
            compose(console, compose(translate(Vec3{x, 0.0256f, 0.f} * S), rotate({1, 0, 0}, -0.5f * kPi))));
    add(kGlareShield, kCockpit, inMetres(c + Vec3{0.f, 0.085f, 0.23f}));
    dashTransform = compose(inMetres(c + Vec3{0.f, 0.075f, 0.2f}), rotate({1, 0, 0}, 150.f * kPi / 180.f));
    dash = static_cast<std::uint32_t>(out.size());
    add(kDashQuad, kDashLight, dashTransform);
    // Navigation lights at the wing tips: red to port (+x: with y up and z along the nose, +x is the pilot's
    // left), green to starboard; strobes on the fin tips.
    const float tipX = kWingTipX - 0.01f;
    const float tipZ = 0.5f * (wingLeadingZ(tipX) + wingTrailingZ(tipX));
    add(kNavLight, kNavRed, inMetres({tipX, wingDihedralY(tipX), tipZ}));
    add(kNavLight, kNavGreen, inMetres({-tipX, wingDihedralY(tipX), tipZ}));
    for (const float side : {1.f, -1.f})
        add(kNavLight, kStrobe, translate(finTip(side)));
    return out;
}

std::uint32_t triangleCount(const std::vector<rt::Mesh>& meshes,
                            const std::vector<SceneInstance>& instances) {
    std::uint32_t total = 0;
    for (const SceneInstance& in : instances)
        total += static_cast<std::uint32_t>(meshes[in.mesh].indices.size() / 3);
    return total;
}

// The inverse of a rigid transform (rotation and translation).
rt::Mat34 inverseRigid(const rt::Mat34& m) {
    rt::Mat34 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = m.m[j][i];
    for (int i = 0; i < 3; ++i)
        r.m[i][3] = -(r.m[i][0] * m.m[0][3] + r.m[i][1] * m.m[1][3] + r.m[i][2] * m.m[2][3]);
    return r;
}

// The plumes' box in model space (metres): both nozzles' exits and 18 m behind them.
constexpr float kPlumeReach = 18.f;
Vec3 plumeLow() {
    return {-4.7f, kPodY * S - 1.9f, kNozzleExitZ * S - kPlumeReach};
}
Vec3 plumeHigh() {
    return {4.7f, kPodY * S + 1.9f, kNozzleExitZ * S + 0.1f};
}

} // namespace

// --- Public functions
// ---------------------------------------------------------------------------------------

std::array<Vec3, 2> shipNozzles() {
    return {Vec3{kPodX, kPodY, kNozzleExitZ} * S, Vec3{-kPodX, kPodY, kNozzleExitZ} * S};
}

float shipPlumeLength(float throttle) {
    return 6.f + 10.f * throttle;
}

float shipPlumeTemperature(float behind, float radial, float throttle) {
    const float length = shipPlumeLength(throttle);
    const float exitKelvin = 5600.f + 3400.f * throttle;
    const float core =
        std::exp(-std::max(behind, 0.f) / (0.3f * length)) * (1.f - 0.45f * std::min(radial * radial, 1.f));
    // Shock diamonds under the afterburner: hot knots along the first metres of the axis.
    const float spacing = 0.75f + 0.45f * throttle;
    float diamonds = 0;
    for (int k = 1; k <= 5; ++k) {
        const float d = (behind - spacing * float(k)) / 0.14f;
        diamonds += std::exp(-d * d) * std::exp(-0.5f * float(k));
    }
    const float burner = 2.f * std::clamp(throttle - 0.5f, 0.f, 1.f);
    return 900.f + (exitKelvin - 900.f) * core +
           3200.f * burner * diamonds * std::exp(-4.f * radial * radial);
}

Vec3 shipBlackbody(float kelvin) {
    const auto planck = [](double lambdaMicrons, double t) {
        return 1.0 / (std::pow(lambdaMicrons, 5.0) * (std::exp(14387.77 / (lambdaMicrons * t)) - 1.0));
    };
    const double t = std::max(double(kelvin), 300.0);
    return {float(planck(0.610, t) / planck(0.610, 6500.0)), float(planck(0.550, t) / planck(0.550, 6500.0)),
            float(planck(0.465, t) / planck(0.465, 6500.0))};
}

Vec3 shipPlumeEmission(float kelvin) {
    const Vec3 b = shipBlackbody(kelvin);
    const float luminance = 0.2126f * b.x + 0.7152f * b.y + 0.0722f * b.z;
    if (!(luminance > 0.f))
        return {};
    return b * std::pow(luminance, -0.45f);
}

std::array<float, 4> quaternionOfRows(const std::array<Vec3, 3>& rows) {
    const auto m = [&](int i, int j) {
        const Vec3& r = rows[i];
        return double(j == 0 ? r.x : j == 1 ? r.y : r.z);
    };
    const double trace = m(0, 0) + m(1, 1) + m(2, 2);
    double x, y, z, w;
    if (trace > 0) {
        const double s = 0.5 / std::sqrt(trace + 1.0);
        w = 0.25 / s;
        x = (m(2, 1) - m(1, 2)) * s;
        y = (m(0, 2) - m(2, 0)) * s;
        z = (m(1, 0) - m(0, 1)) * s;
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        const double s = 2.0 * std::sqrt(1.0 + m(0, 0) - m(1, 1) - m(2, 2));
        w = (m(2, 1) - m(1, 2)) / s;
        x = 0.25 * s;
        y = (m(0, 1) + m(1, 0)) / s;
        z = (m(0, 2) + m(2, 0)) / s;
    } else if (m(1, 1) > m(2, 2)) {
        const double s = 2.0 * std::sqrt(1.0 + m(1, 1) - m(0, 0) - m(2, 2));
        w = (m(0, 2) - m(2, 0)) / s;
        x = (m(0, 1) + m(1, 0)) / s;
        y = 0.25 * s;
        z = (m(1, 2) + m(2, 1)) / s;
    } else {
        const double s = 2.0 * std::sqrt(1.0 + m(2, 2) - m(0, 0) - m(1, 1));
        w = (m(1, 0) - m(0, 1)) / s;
        x = (m(0, 2) + m(2, 0)) / s;
        y = (m(1, 2) + m(2, 1)) / s;
        z = 0.25 * s;
    }
    return {float(x), float(y), float(z), float(w)};
}

SplatFrame shipSplats(const ShipSky& sky) {
    SplatFrame s;
    s.count = sky.stars + sky.air + 1;
    const Vec3 sun = normalize(sky.sun);
    s.params = {sky.eye.x,
                sky.eye.y,
                sky.eye.z,
                float(sky.stars),
                sky.worldToRender[0],
                sky.worldToRender[1],
                sky.worldToRender[2],
                sky.worldToRender[3],
                sky.planetCentre.x,
                sky.planetCentre.y,
                sky.planetCentre.z,
                float(sky.air),
                sun.x,
                sun.y,
                sun.z,
                sky.planetRadius};
    s.fixedPointScale = 16384.f;
    return s;
}

ShipAsset::ShipAsset(SceneStatic& scene, SceneDetail detail, std::uint32_t splats) {
    std::vector<rt::Mesh> meshes = buildMeshes();
    std::vector<rt::Material> materials = buildMaterials();
    instances_ = buildInstances(dashInstance_, dashTransform_);
    counts_.triangles = triangleCount(meshes, instances_);
    counts_.meshes = kMeshCount;
    counts_.materials = kMaterialCount;
    counts_.instances = static_cast<std::uint32_t>(instances_.size());
    firstMesh_ = static_cast<std::uint32_t>(scene.meshes.size());
    firstMaterial_ = static_cast<std::uint32_t>(scene.materials.size());
    for (rt::Mesh& m : meshes)
        scene.meshes.push_back(std::move(m));
    scene.materials.insert(scene.materials.end(), materials.begin(), materials.end());
    const double cube = std::cbrt(std::clamp(double(detail.scale), 1e-6, 1.0));
    for (int k = 0; k < 3; ++k)
        grid_[k] = std::max<std::uint32_t>(16, std::uint32_t(std::lround(double(kShipPlumeGrid[k]) * cube)));
    scene.mediumShader = kShipPlumeShader;
    scene.splatShader = kShipSplatShader;
    SceneCapacity& c = scene.capacity;
    c.instances = std::max(c.instances, counts_.instances + 32);
    c.lights = std::max<std::uint32_t>(c.lights, 1);
    c.materials = std::max(c.materials, static_cast<std::uint32_t>(scene.materials.size()));
    c.splats = std::max(c.splats, splats);
    for (int k = 0; k < 3; ++k)
        c.mediumGrid[k] = std::max(c.mediumGrid[k], grid_[k]);
}

void ShipAsset::place(SceneFrame& frame, const rt::Mat34& modelToRender) const {
    const auto light = static_cast<std::uint32_t>(frame.lights.size());
    frame.lights.push_back(rectLightForQuad(compose(modelToRender, dashTransform_), kDashWidth, kDashHeight,
                                            rt::LightKind::Emitter, kDashEmission, 1.f));
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        SceneInstance in = instances_[i];
        in.mesh += firstMesh_;
        in.material += firstMaterial_;
        in.transform = compose(modelToRender, in.transform);
        if (i == dashInstance_)
            in.light = light;
        frame.instances.push_back(in);
    }
}

void ShipAsset::animate(std::vector<rt::Material>& table, const show::TimelineFrame& f,
                        float throttle) const {
    rt::Material* m = table.data() + firstMaterial_;
    const float kick = std::clamp(f.kick, 0.f, 1.f);
    m[kCeramic].emissionScale = kGlowIdle + kGlowKick * kick * kick;
    m[kStrobe].emissionScale = std::clamp(f.beatPhase, 0.f, 1.f) < 0.08f ? kStrobeEmission : 0.f;
    const float t = std::clamp(throttle, 0.f, 2.f);
    m[kThroat].emissionScale = kThroatEmission * t;
    m[kNozzleInner].emissionScale = kNozzleInnerEmission * t * t;
}

GridMediumDesc ShipAsset::plumes(float throttle, const rt::Mat34& modelToRender) const {
    GridMediumDesc g;
    g.size = grid_;
    const Vec3 lo = plumeLow(), hi = plumeHigh();
    g.worldToMedium = compose(mediumBox(lo, hi), inverseRigid(modelToRender));
    g.densityScale = 1.f;
    g.albedo = {0.04f, 0.04f, 0.04f}; // fire emits and absorbs; it hardly scatters
    g.phaseG = 0.35f;
    g.emissionScale = 22.f;
    const std::array<Vec3, 2> nozzles = shipNozzles();
    const float exitRadius = 0.105f * S;
    g.params = {lo.x,         lo.y,         lo.z,         throttle,   hi.x, hi.y, hi.z, 3.2f,
                nozzles[0].x, nozzles[0].y, nozzles[0].z, exitRadius, 70.f, 0.f,  0.f,  0.f};
    return g;
}

std::uint32_t ShipAsset::glowMaterial() const {
    return firstMaterial_ + kCeramic;
}
std::uint32_t ShipAsset::strobeMaterial() const {
    return firstMaterial_ + kStrobe;
}
std::uint32_t ShipAsset::throatMaterial() const {
    return firstMaterial_ + kThroat;
}

} // namespace evo::modern
