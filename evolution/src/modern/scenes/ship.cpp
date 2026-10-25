#include "modern/scenes/ship.hpp"

#include "modern/environment.hpp"
#include "modern/mesh_builder.hpp"
#include "retro/flyby_flight.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <span>
#include <tuple>
#include <utility>

namespace evo::modern {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr double kPiD = std::numbers::pi;

float smooth(float x) {
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// --- A shape: shared positions, triangles with a material slot, smooth normals up to a crease angle ----------

struct Shape {
    std::vector<Vec3> positions;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::vector<std::uint32_t> slots; // per triangle: which material (and so which mesh) it belongs to
};

std::uint32_t addPoint(Shape& s, Vec3 p) {
    s.positions.push_back(p);
    return static_cast<std::uint32_t>(s.positions.size() - 1);
}

void addTriangle(Shape& s, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t slot) {
    s.triangles.push_back({a, b, c});
    s.slots.push_back(slot);
}

// Rows of closed loops (each `columns` points, the same count on every row), joined by quads; slot(row, column)
// names each quad's material (row: the quad between row and row + 1). Returns the index of the first point.
template <typename SlotOf>
std::uint32_t addLoops(Shape& s, const std::vector<std::vector<Vec3>>& rows, SlotOf slotOf) {
    const auto first = static_cast<std::uint32_t>(s.positions.size());
    const auto columns = static_cast<std::uint32_t>(rows.front().size());
    for (const auto& row : rows)
        for (const Vec3& p : row)
            addPoint(s, p);
    for (std::uint32_t i = 0; i + 1 < rows.size(); ++i)
        for (std::uint32_t j = 0; j < columns; ++j) {
            const std::uint32_t jn = (j + 1) % columns;
            const std::uint32_t a = first + i * columns + j, b = first + i * columns + jn;
            const std::uint32_t c = first + (i + 1) * columns + jn, d = first + (i + 1) * columns + j;
            const std::uint32_t slot = slotOf(i, j);
            addTriangle(s, a, b, c, slot);
            addTriangle(s, a, c, d, slot);
        }
    return first;
}

// A fan from a new apex point to a loop of `columns` points starting at `first`.
void addFan(Shape& s, Vec3 apex, std::uint32_t first, std::uint32_t columns, std::uint32_t slot) {
    const std::uint32_t centre = addPoint(s, apex);
    for (std::uint32_t j = 0; j < columns; ++j)
        addTriangle(s, centre, first + (j + 1) % columns, first + j, slot);
}

// Twice the signed volume of the triangles from `begin` on (positive: they wind counter-clockwise seen from
// outside, the convention of every mesh here).
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

// The shape's meshes, one per slot (slotCount of them; an empty slot gives an empty mesh): smooth normals,
// area-weighted over the triangles around a point whose faces lie within `creaseDegrees` of each other (a
// sharper edge stays sharp). uvs are 0: no material here reads them.
std::vector<rt::Mesh> toMeshes(const Shape& s, std::uint32_t slotCount, float creaseDegrees) {
    const std::size_t triangles = s.triangles.size();
    std::vector<Vec3> faceCross(triangles), faceUnit(triangles);
    std::vector<std::vector<std::uint32_t>> around(s.positions.size());
    for (std::size_t t = 0; t < triangles; ++t) {
        const auto& tri = s.triangles[t];
        const Vec3 n = cross(s.positions[tri[1]] - s.positions[tri[0]], s.positions[tri[2]] - s.positions[tri[0]]);
        faceCross[t] = n;
        const float len = length(n);
        faceUnit[t] = len > 0 ? n / len : Vec3{};
        for (const std::uint32_t p : tri)
            around[p].push_back(static_cast<std::uint32_t>(t));
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
            for (const std::uint32_t o : around[p])
                if (dot(faceUnit[o], faceUnit[t]) >= cosCrease)
                    sum = sum + faceCross[o];
            const float len = length(sum);
            const Vec3 n = len > 0 ? sum / len : faceUnit[t];
            const auto key = std::make_tuple(p, n.x, n.y, n.z);
            const auto found = seen[slot].find(key);
            if (found != seen[slot].end()) {
                out[k] = found->second;
                continue;
            }
            const Vec3 q = s.positions[p];
            mesh.vertices.push_back({{q.x, q.y, q.z}, {n.x, n.y, n.z}, {0.f, 0.f}});
            out[k] = static_cast<std::uint32_t>(mesh.vertices.size() - 1);
            seen[slot].emplace(key, out[k]);
        }
        mesh.indices.insert(mesh.indices.end(), out.begin(), out.end());
    }
    return meshes;
}

// --- The ship's design (1993 model units; kShipScale metres each) -------------------------------------------
//
// The 1993 fighter's layout (retro/flyby.cpp): a fuselage from a needle nose at z = 2.5 through its widest
// section at z = -0.3 to the tail at z = -1.9, a canopy on its back ahead of the widest section, two wings
// swept back hard with a little dihedral, and an engine pod hung under each wing with its nozzle at z = -2.05.

constexpr float S = kShipScale;

// The fuselage's scale along z (0 at the nose; 1 at the widest section), a Catmull-Rom curve through the 1993
// sections (nose 0 at 2.5, 0.58 at 1.0, 1 at -0.3, 0.85 at -1.9), smoothed into an ogive.
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
    // Knots run from the nose (high z) to the tail.
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

// A point of the fuselage's section at z, angle theta (0 to starboard, pi / 2 on top): a superellipse, flatter
// on its back than its keel, like the 1993 hexagon rounded off.
constexpr float kFuselageHalfWidth = 0.34f, kFuselageTop = 0.30f, kFuselageKeel = 0.24f;
constexpr float kFuselageExponent = 2.5f;

Vec3 fuselagePoint(float z, float theta, float inflate = 1.f) {
    const float k = std::max(fuselageScale(z), 0.012f) * inflate;
    const float c = std::cos(theta), s = std::sin(theta);
    const float e = 2.f / kFuselageExponent;
    const float x = kFuselageHalfWidth * k * std::copysign(std::pow(std::abs(c), e), c);
    const float y = (s >= 0 ? kFuselageTop : kFuselageKeel) * k * std::copysign(std::pow(std::abs(s), e), s);
    return Vec3{x, y, z} * S;
}

// The canopy: an ellipsoid on the fuselage's back (its lower half sunk into the hull), longer behind its
// widest point than ahead of it; a shell kCanopyWall thick.
constexpr Vec3 kCanopyCentre{0.f, 0.245f, 0.38f};
constexpr float kCanopyHalfWidth = 0.15f, kCanopyHeight = 0.135f, kCanopyFront = 0.55f, kCanopyBack = 0.75f;
constexpr float kCanopyWall = 0.008f;

// The wings: swept back hard, with dihedral; the 1993 root and tip edges.
constexpr float kWingRootX = 0.16f, kWingTipX = 1.86f;

float wingLeadingZ(float x) {
    return 0.60f + (x - 0.225f) * (-1.25f - 0.60f) / (1.80f - 0.225f);
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

// The engine pods: under each wing, the 1993 pod's centre line, a revolved nacelle.
constexpr float kPodX = 0.74f, kPodY = -0.13f;
constexpr float kNozzleExitZ = -2.07f;
// The nacelle's profile (radius, z) from the intake spinner's tip to the nozzle throat's centre, and the
// material of each segment (the segment after point i).
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

// --- Meshes and materials -----------------------------------------------------------------------------------

enum Material : std::uint32_t {
    kTitanium,
    kCeramic,
    kChrome,
    kDuct,
    kNozzle,
    kNozzleInner,
    kThroat,
    kPanelLines,
    kCanopyGlass,
    kCockpit,
    kDialAmber,
    kDialCyan,
    kNavRed,
    kNavGreen,
    kStrobe,
    kOcean,
    kMaterialCount
};

enum Mesh : std::uint32_t {
    kFuselageTitanium,
    kFuselageCeramic,
    kWingTitanium,
    kWingCeramic,
    kPodChrome,
    kPodDuct,
    kPodTitanium,
    kPodNozzle,
    kPodNozzleInner,
    kPodThroat,
    kCanopy,
    kPanelLineMesh,
    kCockpitTub,
    kSeat,
    kHeadrest,
    kConsole,
    kDial,
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

rt::Material emitter(Vec3 color, float scale) {
    rt::Material m = plastic({0.02f, 0.02f, 0.02f}, 0.4f);
    m.emission[0] = color.x, m.emission[1] = color.y, m.emission[2] = color.z;
    m.emissionScale = scale;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    m.flags = rt::kMaterialCameraOnlyEmission; // seen, never sampled as a light (Task 6)
    return m;
}

// Base emission scales the per-frame table modulates.
constexpr float kPanelEmission = 2.2f, kStrobeEmission = 60.f, kNavEmission = 14.f;

std::vector<rt::Material> buildMaterials() {
    std::vector<rt::Material> m(kMaterialCount);
    m[kTitanium] = metal({0.56f, 0.53f, 0.50f}, 0.3f); // titanium's F0, brushed across the span
    m[kTitanium].pattern = static_cast<std::uint32_t>(rt::Pattern::BrushedMetal);
    m[kTitanium].patternScale = 2.f;
    m[kCeramic] = plastic({0.035f, 0.038f, 0.045f}, 0.55f); // dark ceramic tiles under a satin coat
    m[kCeramic].coat = 0.35f;
    m[kCeramic].coatRoughness = 0.18f;
    m[kChrome] = metal({0.95f, 0.95f, 0.96f}, 0.06f);
    m[kDuct] = metal({0.12f, 0.12f, 0.13f}, 0.45f);
    m[kNozzle] = metal({0.33f, 0.26f, 0.24f}, 0.32f); // heat-tinted
    m[kNozzleInner] = emitter({1.f, 0.38f, 0.1f}, 2.5f); // the bell's inner wall, glowing orange
    m[kThroat] = emitter({0.85f, 0.9f, 1.f}, 40.f);      // white-hot
    m[kPanelLines] = emitter({0.25f, 0.8f, 1.f}, kPanelEmission);
    m[kCanopyGlass] = plastic({0.93f, 0.96f, 1.f}, 0.f);
    m[kCanopyGlass].transmission = 1.f;
    m[kCanopyGlass].ior = 1.5f;
    m[kCanopyGlass].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Glass);
    m[kCockpit] = plastic({0.025f, 0.027f, 0.03f}, 0.6f);
    m[kDialAmber] = emitter({1.f, 0.55f, 0.12f}, 3.f);
    m[kDialCyan] = emitter({0.2f, 0.85f, 1.f}, 3.f);
    m[kNavRed] = emitter({1.f, 0.06f, 0.03f}, kNavEmission);
    m[kNavGreen] = emitter({0.05f, 1.f, 0.2f}, kNavEmission);
    m[kStrobe] = emitter({1.f, 1.f, 1.f}, kStrobeEmission);
    // The planet's ocean: deep blue under a glossy coat (the sun's glint).
    m[kOcean] = plastic({0.012f, 0.05f, 0.11f}, 0.5f);
    m[kOcean].coat = 1.f;
    m[kOcean].coatRoughness = 0.07f;
    return m;
}

// The fuselage, its panel lines and the canopy's opening. Slots: titanium 0, ceramic belly 1.
constexpr int kFuselageRows = 120, kFuselageColumns = 112;
constexpr float kFuselageNoseZ = 2.6f, kFuselageTailZ = -2.0f;
// The belly's ceramic lies below sin(theta) = kBellyLine; the upper panel line along the side.
constexpr float kBellyLine = -0.32f, kSideLine = 0.2f;

float rowZ(int i) {
    // Denser towards the nose, where the curvature is.
    const float u = static_cast<float>(i) / static_cast<float>(kFuselageRows - 1);
    const float w = 1.f - (1.f - u) * (1.f - u) * 0.6f - 0.4f * (1.f - u);
    return lerp(kFuselageNoseZ - 0.02f, kFuselageTailZ, w);
}

float columnTheta(int j) {
    return 2.f * kPi * static_cast<float>(j) / static_cast<float>(kFuselageColumns);
}

void buildFuselage(Shape& s) {
    const std::size_t begin = s.triangles.size();
    std::vector<std::vector<Vec3>> rows;
    for (int i = 0; i < kFuselageRows; ++i) {
        std::vector<Vec3> row;
        for (int j = 0; j < kFuselageColumns; ++j)
            row.push_back(fuselagePoint(rowZ(i), columnTheta(j)));
        rows.push_back(std::move(row));
    }
    // A tail ring inset for the tail cap's bevel.
    {
        std::vector<Vec3> row;
        for (int j = 0; j < kFuselageColumns; ++j)
            row.push_back(fuselagePoint(kFuselageTailZ, columnTheta(j), 0.78f) + Vec3{0, 0, -0.06f * S});
        rows.push_back(std::move(row));
    }
    const auto columns = static_cast<std::uint32_t>(kFuselageColumns);
    const std::uint32_t first = addLoops(s, rows, [](std::uint32_t, std::uint32_t j) {
        const float mid = columnTheta(int(j)) + kPi / static_cast<float>(kFuselageColumns);
        return std::sin(mid) < kBellyLine ? 1u : 0u;
    });
    addFan(s, Vec3{0, 0, kFuselageNoseZ} * S, first, columns, 0);
    const std::uint32_t last = first + columns * static_cast<std::uint32_t>(rows.size() - 1);
    const Vec3 tail = fuselagePoint(kFuselageTailZ, 0.f, 0.f) + Vec3{0, 0, -0.06f * S};
    addFan(s, Vec3{0.f, tail.y, tail.z}, last, columns, 1);
    orient(s, begin);
}

// A wing (side +1 starboard, -1 port). Slots: titanium 0 (leading edge and underside), ceramic 1 (the top).
constexpr int kWingRows = 40, kWingColumns = 56;

Vec3 wingPoint(float x, int k) {
    const float le = wingLeadingZ(x), te = wingTrailingZ(x), chord = le - te;
    const float phi = 2.f * kPi * static_cast<float>(k) / static_cast<float>(kWingColumns);
    const float u = 0.5f * (1.f + std::cos(phi)); // 1 at the trailing edge (k = 0), 0 at the leading edge
    const float half = nacaHalfThickness(u, wingThickness(x));
    const float y = wingDihedralY(x) + (std::sin(phi) >= 0 ? half : -half);
    return Vec3{x, y, le - u * chord};
}

bool wingTop(int k) {
    const float phi = 2.f * kPi * (static_cast<float>(k) + 0.5f) / static_cast<float>(kWingColumns);
    return std::sin(phi) > 0 && std::cos(phi) > -0.86f; // the top, but for the leading edge's band
}

void buildWing(Shape& s, float side) {
    const std::size_t begin = s.triangles.size();
    std::vector<std::vector<Vec3>> rows;
    for (int i = 0; i < kWingRows; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kWingRows - 1);
        const float x = lerp(kWingRootX, kWingTipX, 1.f - (1.f - t) * (1.f - t) * 0.3f - 0.7f * (1.f - t));
        std::vector<Vec3> row;
        for (int k = 0; k < kWingColumns; ++k) {
            const Vec3 p = wingPoint(x, k);
            row.push_back(Vec3{side * p.x, p.y, p.z} * S);
        }
        rows.push_back(std::move(row));
    }
    const auto columns = static_cast<std::uint32_t>(kWingColumns);
    const std::uint32_t first =
        addLoops(s, rows, [](std::uint32_t, std::uint32_t k) { return wingTop(int(k)) ? 1u : 0u; });
    // Caps: the root (inside the fuselage) and the tip.
    const auto centre = [&](const std::vector<Vec3>& row) {
        Vec3 c{};
        for (const Vec3& p : row)
            c = c + p;
        return c / static_cast<float>(row.size());
    };
    addFan(s, centre(rows.front()), first, columns, 0);
    addFan(s, centre(rows.back()), first + columns * static_cast<std::uint32_t>(kWingRows - 1), columns, 0);
    orient(s, begin);
}

// An engine pod (side +1 starboard, -1 port) revolved about its axis. Slots: chrome 0, duct 1, titanium 2,
// nozzle 3, nozzle inner 4, throat 5.
constexpr int kPodSegments = 96;

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
    const auto columns = static_cast<std::uint32_t>(kPodSegments);
    const auto ring = [&](const ProfilePoint& p) {
        std::vector<Vec3> row;
        for (int j = 0; j < kPodSegments; ++j) {
            const float a = 2.f * kPi * static_cast<float>(j) / static_cast<float>(kPodSegments);
            row.push_back((axis + Vec3{p.r * std::cos(a), p.r * std::sin(a), p.z}) * S);
        }
        return row;
    };
    // The rings of the profile points off the axis; the two ends are poles.
    std::vector<std::vector<Vec3>> rows;
    for (std::size_t i = 1; i + 1 < kPodProfile.size(); ++i)
        rows.push_back(ring(kPodProfile[i]));
    const std::uint32_t first =
        addLoops(s, rows, [](std::uint32_t row, std::uint32_t) { return podSlot(row + 1); });
    addFan(s, (axis + Vec3{0, 0, kPodProfile.front().z}) * S, first, columns, podSlot(0));
    addFan(s, (axis + Vec3{0, 0, kPodProfile.back().z}) * S,
           first + columns * static_cast<std::uint32_t>(rows.size() - 1), columns,
           podSlot(kPodProfile.size() - 2));
    orient(s, begin);
}

// One wall of the canopy: an ellipsoid shrunk by `inset` on every axis.
void buildCanopyWall(Shape& s, float inset, bool inner) {
    const std::size_t begin = s.triangles.size();
    constexpr int kRings = 44, kColumns = 72;
    const float a = kCanopyHalfWidth - inset, b = kCanopyHeight - inset;
    const float front = kCanopyFront - inset, back = kCanopyBack - inset;
    std::vector<std::vector<Vec3>> rows;
    for (int i = 1; i < kRings; ++i) {
        const float zeta = std::cos(kPi * static_cast<float>(i) / static_cast<float>(kRings)); // 1 front .. -1
        const float rho = std::sqrt(std::max(0.f, 1.f - zeta * zeta));
        const float z = kCanopyCentre.z + (zeta > 0 ? zeta * front : zeta * back);
        std::vector<Vec3> row;
        for (int j = 0; j < kColumns; ++j) {
            const float theta = 2.f * kPi * static_cast<float>(j) / static_cast<float>(kColumns);
            row.push_back(Vec3{a * rho * std::cos(theta), kCanopyCentre.y + b * rho * std::sin(theta), z} * S);
        }
        rows.push_back(std::move(row));
    }
    const std::uint32_t first = addLoops(s, rows, [](std::uint32_t, std::uint32_t) { return 0u; });
    const auto columns = static_cast<std::uint32_t>(kColumns);
    addFan(s, Vec3{0, kCanopyCentre.y, kCanopyCentre.z + front} * S, first, columns, 0);
    addFan(s, Vec3{0, kCanopyCentre.y, kCanopyCentre.z - back} * S,
           first + columns * static_cast<std::uint32_t>(rows.size() - 1), columns, 0);
    orient(s, begin, inner);
}

// A ribbon (a thin strip of quads, both sides drawn by the tracer) through `centre` points, `halfWidth` to
// either side along `across`.
void addRibbon(Shape& s, const std::vector<Vec3>& centre, const std::vector<Vec3>& across, float halfWidth) {
    const auto first = static_cast<std::uint32_t>(s.positions.size());
    for (std::size_t i = 0; i < centre.size(); ++i) {
        addPoint(s, centre[i] - across[i] * halfWidth);
        addPoint(s, centre[i] + across[i] * halfWidth);
    }
    for (std::uint32_t i = 0; i + 1 < centre.size(); ++i) {
        const std::uint32_t a = first + 2 * i;
        addTriangle(s, a, a + 1, a + 3, 0);
        addTriangle(s, a, a + 3, a + 2, 0);
    }
}

// The glowing panel lines: along the fuselage's sides (at the belly's edge and higher up), around the canopy's
// base, and spanwise over each wing's top.
void buildPanelLines(Shape& s) {
    const float lift = 1.012f; // off the hull, so they never fight it
    const float halfWidth = 0.006f * S;
    for (const float side : {1.f, -1.f}) {
        for (const auto& [line, from, to] :
             {std::tuple{kBellyLine, 1.25f, -1.85f}, std::tuple{kSideLine, -0.45f, -1.8f}}) {
            const float theta = side > 0 ? std::asin(line) : kPi - std::asin(line);
            std::vector<Vec3> centre, across;
            for (int i = 0; i <= 80; ++i) {
                const float z = lerp(from, to, static_cast<float>(i) / 80.f);
                const Vec3 a = fuselagePoint(z, theta - 0.01f, lift), b = fuselagePoint(z, theta + 0.01f, lift);
                centre.push_back((a + b) * 0.5f);
                across.push_back(normalize(b - a));
            }
            addRibbon(s, centre, across, halfWidth);
        }
        // Two lines along each wing's top, at a quarter and two thirds of the chord.
        for (const float u : {0.22f, 0.68f}) {
            std::vector<Vec3> centre, across;
            for (int i = 0; i <= 40; ++i) {
                const float x = lerp(0.42f, 1.72f, static_cast<float>(i) / 40.f);
                const float le = wingLeadingZ(x), te = wingTrailingZ(x);
                const float y = wingDihedralY(x) + nacaHalfThickness(u, wingThickness(x)) + 0.004f;
                centre.push_back(Vec3{side * x, y, le - u * (le - te)} * S);
                across.push_back(Vec3{0.f, 0.f, 1.f});
            }
            addRibbon(s, centre, across, halfWidth);
        }
    }
    // A ring at the canopy's base, a little above where the canopy meets the hull, and two across the back.
    for (const float z : {-0.62f, -1.15f}) {
        std::vector<Vec3> centre, across;
        for (int j = 0; j <= 48; ++j) {
            const float theta = lerp(std::asin(kSideLine), kPi - std::asin(kSideLine), static_cast<float>(j) / 48.f);
            centre.push_back(fuselagePoint(z, theta, lift));
            across.push_back(Vec3{0.f, 0.f, 1.f});
        }
        addRibbon(s, centre, across, halfWidth);
    }
}

// The scene's meshes; `triangles` gets the ship's triangle count as instanced (both sides, every part).
std::vector<rt::Mesh> buildMeshes() {
    std::vector<rt::Mesh> meshes(kMeshCount);
    {
        Shape s;
        buildFuselage(s);
        auto parts = toMeshes(s, 2, 34.f);
        meshes[kFuselageTitanium] = std::move(parts[0]);
        meshes[kFuselageCeramic] = std::move(parts[1]);
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
        buildPod(s, 1.f);
        buildPod(s, -1.f);
        auto parts = toMeshes(s, 6, 38.f);
        meshes[kPodChrome] = std::move(parts[0]);
        meshes[kPodDuct] = std::move(parts[1]);
        meshes[kPodTitanium] = std::move(parts[2]);
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
    {
        Shape s;
        buildPanelLines(s);
        meshes[kPanelLineMesh] = std::move(toMeshes(s, 1, 80.f)[0]);
    }
    meshes[kCockpitTub] = bevelBox(Vec3{0.11f, 0.05f, 0.42f} * S, 0.02f * S, 2);
    meshes[kSeat] = bevelBox(Vec3{0.055f, 0.03f, 0.05f} * S, 0.012f * S, 2);
    meshes[kHeadrest] = bevelBox(Vec3{0.05f, 0.06f, 0.015f} * S, 0.01f * S, 2);
    meshes[kConsole] = bevelBox(Vec3{0.1f, 0.025f, 0.06f} * S, 0.01f * S, 2);
    meshes[kDial] = quad(0.026f * S, 0.018f * S);
    meshes[kNavLight] = uvSphere(0.018f * S, 12, 24);
    return meshes;
}

// The cockpit's parts relative to the canopy's centre (model units), and its dials.
struct Dial {
    float x, z;
    std::uint32_t material;
};
constexpr std::array<Dial, 6> kDials = {{
    {-0.06f, 0.0f, kDialCyan},
    {-0.03f, 0.f, kDialAmber},
    {0.f, 0.f, kDialCyan},
    {0.03f, 0.f, kDialAmber},
    {0.06f, 0.f, kDialCyan},
    {0.f, 0.03f, kDialAmber},
}};

rt::Mat34 inMetres(Vec3 unitsPosition) {
    return translate(unitsPosition * S);
}

std::vector<SceneInstance> buildShipInstances() {
    std::vector<SceneInstance> out;
    const auto add = [&](std::uint32_t mesh, std::uint32_t material, const rt::Mat34& m = identity()) {
        out.push_back({mesh, material, m});
    };
    add(kFuselageTitanium, kTitanium);
    add(kFuselageCeramic, kCeramic);
    add(kWingTitanium, kTitanium);
    add(kWingCeramic, kCeramic);
    add(kPodChrome, kChrome);
    add(kPodDuct, kDuct);
    add(kPodTitanium, kTitanium);
    add(kPodNozzle, kNozzle);
    add(kPodNozzleInner, kNozzleInner);
    add(kPodThroat, kThroat);
    add(kCanopy, kCanopyGlass);
    add(kPanelLineMesh, kPanelLines);
    // The cockpit under the canopy: a tub, the seat with its headrest, the console and its dials.
    const Vec3 c = kCanopyCentre;
    add(kCockpitTub, kCockpit, inMetres(c + Vec3{0.f, -0.045f, -0.02f}));
    add(kSeat, kCockpit, inMetres(c + Vec3{0.f, 0.02f, -0.2f}));
    add(kHeadrest, kCockpit,
        compose(inMetres(c + Vec3{0.f, 0.07f, -0.27f}), rotate({1, 0, 0}, 0.25f)));
    const rt::Mat34 console = compose(inMetres(c + Vec3{0.f, 0.03f, 0.2f}), rotate({1, 0, 0}, 0.45f));
    add(kConsole, kCockpit, console);
    for (const Dial& d : kDials)
        add(kDial, d.material,
            compose(console, compose(translate(Vec3{d.x, 0.0255f, d.z} * S), rotate({1, 0, 0}, -0.5f * kPi))));
    // Navigation lights: red to port, green to starboard at the wing tips, the strobe on the tail.
    const float tipX = kWingTipX - 0.01f;
    const float tipZ = 0.5f * (wingLeadingZ(tipX) + wingTrailingZ(tipX));
    add(kNavLight, kNavGreen, inMetres({tipX, wingDihedralY(tipX), tipZ}));
    add(kNavLight, kNavRed, inMetres({-tipX, wingDihedralY(tipX), tipZ}));
    add(kNavLight, kStrobe, inMetres({0.f, fuselagePoint(-1.85f, 0.5f * kPi).y / S + 0.01f, -1.85f}));
    return out;
}

// --- The flight -------------------------------------------------------------------------------------------

constexpr double kRetroRate = retro::kFlybyRetroRate;

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

struct DPose {
    DVec3 position;
    double r[3][3]; // model to world, rows
};

DPose pose(double t) {
    DPose p;
    p.position = {0, 0, double(kShipSpeed) * t};
    const retro::FlybyAttitude a = retro::flybyAttitude(t);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            p.r[i][j] = a.m[i][j];
    return p;
}

// R^T d.
DVec3 toModel(const DPose& p, DVec3 d) {
    return {p.r[0][0] * d.x + p.r[1][0] * d.y + p.r[2][0] * d.z, p.r[0][1] * d.x + p.r[1][1] * d.y + p.r[2][1] * d.z,
            p.r[0][2] * d.x + p.r[1][2] * d.y + p.r[2][2] * d.z};
}

Vec3 single(DVec3 v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

// The world's camera, positions relative to the ship (world axes), metres.
struct RelativeCamera {
    DVec3 eye, target;
    float fov;
    float aperture = 0, focus = 1;
    std::uint32_t shot;
    bool modelLocked = false; // eye and target in model space (riding with the ship) instead of world axes
};

DVec3 spherical(double azimuth, double elevation, double distance) {
    return {distance * std::cos(elevation) * std::sin(azimuth), distance * std::sin(elevation),
            -distance * std::cos(elevation) * std::cos(azimuth)};
}

double smoothD(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

struct ShotTimes {
    double accent, crossingEnd, duration;
};

ShotTimes shotTimes(const show::TimelineFrame& f) {
    const double duration = std::max(f.chapterDuration, 1e-3);
    const double accent = std::clamp(f.accentSeconds, 0.0, duration);
    const double crossingEnd = accent + std::min<double>(kShipCrossingSeconds, 0.5 * (duration - accent));
    return {accent, crossingEnd, duration};
}

std::uint32_t shotAt(const ShotTimes& s, double local) {
    return local < s.accent ? 0u : local < s.crossingEnd ? 1u : 2u;
}

RelativeCamera relativeCamera(const show::TimelineFrame& f) {
    const ShotTimes times = shotTimes(f);
    const double local = std::clamp(f.localSeconds, 0.0, times.duration);
    const std::uint32_t shot = shotAt(times, local);
    RelativeCamera c{};
    c.shot = shot;
    c.fov = static_cast<float>(retro::kFlybyFovY);
    if (shot == 0) {
        // The chase: behind and above, drifting a little round to the right; from 4.2 s before the accent it
        // swings round onto the 1993 camera, which it holds from 1.2 s before the accent (the portal opens a
        // second before it), so both eras show the same picture while they cross-fade.
        const retro::FlybyView v93 = retro::flybyView(times.duration, local * kRetroRate);
        const DVec3 eye93{v93.eye[0] * S, v93.eye[1] * S, v93.eye[2] * S};
        const DVec3 target93{v93.target[0] * S, v93.target[1] * S, v93.target[2] * S};
        const double w = smoothD((local - (times.accent - 4.2)) / 3.0);
        if (w >= 1.0) {
            c.eye = eye93;
            c.target = target93;
            return c;
        }
        const double drift = local / std::max(times.accent, 1e-3);
        const double chaseAzimuth = (-12.0 + 18.0 * smoothD(drift)) * kPiD / 180.0;
        const double chaseElevation = (15.0 - 3.0 * drift) * kPiD / 180.0;
        const double chaseDistance = 7.0 * S - 0.6 * S * drift;
        const DVec3 chaseTarget = DVec3{0.0, -0.15, 2.4} * S;
        const double azimuth93 = std::atan2(eye93.x, -eye93.z);
        const double distance93 = std::sqrt(eye93.x * eye93.x + eye93.y * eye93.y + eye93.z * eye93.z);
        const double elevation93 = std::asin(eye93.y / distance93);
        const auto mix = [w](double a, double b) { return a + (b - a) * w; };
        c.eye = spherical(mix(chaseAzimuth, azimuth93), mix(chaseElevation, elevation93),
                          mix(chaseDistance, distance93));
        c.target = chaseTarget * (1 - w) + target93 * w;
        return c;
    }
    if (shot == 1) {
        // The crossing: a camera standing in space beside the flight path, a little below it, that the ship
        // passes kShipPassSeconds after the accent. It turns after the ship more slowly than the ship flies
        // past (its bearing is the ship's seen from kLag times further off), so the ship sweeps across the
        // frame, streaked by the shutter, and then recedes into the distance as the camera catches up.
        constexpr double kSide = 13.0, kBelow = 3.4, kLag = 2.2;
        const double pass = times.accent + kShipPassSeconds;
        const DPose now = pose(local);
        const DVec3 camera = pose(pass).position + DVec3{kSide, -kBelow, 0.0};
        const double along = double(kShipSpeed) * (local - pass);
        c.eye = camera - now.position;
        c.target = c.eye + DVec3{-kSide * kLag, kBelow * kLag * 0.85, along};
        c.fov = 46.f;
        return c;
    }
    // The orbit past the canopy, riding with the ship: from ahead and to the right round to its left, above
    // the canopy, which reflects the sun and the planet.
    const double s = smoothD((local - times.crossingEnd) / std::max(times.duration - times.crossingEnd, 1e-3));
    const double azimuth = (38.0 - 92.0 * s) * kPiD / 180.0; // from +z (ahead), positive to starboard
    const double elevation = (17.0 + 9.0 * s) * kPiD / 180.0;
    const double distance = (3.6 - 0.5 * std::sin(kPiD * s)) * 1.0;
    const DVec3 centre{0.0, (kCanopyCentre.y + 0.02) * S, (kCanopyCentre.z - 0.05) * S};
    c.eye = centre + DVec3{distance * std::cos(elevation) * std::sin(azimuth), distance * std::sin(elevation),
                           distance * std::cos(elevation) * std::cos(azimuth)};
    c.target = centre + DVec3{0.0, -0.25, -0.9};
    c.fov = 40.f;
    c.modelLocked = true;
    return c;
}

Camera carry(const RelativeCamera& rc, const DPose& p) {
    Camera c;
    if (rc.modelLocked) {
        c.eye = single(rc.eye);
        c.target = single(rc.target);
    } else {
        c.eye = single(toModel(p, rc.eye));
        c.target = single(toModel(p, rc.target));
    }
    c.up = single(toModel(p, {0, 1, 0}));
    c.fovYDegrees = rc.fov;
    c.aperture = rc.aperture;
    c.focusDistance = rc.focus;
    c.shot = rc.shot;
    return c;
}

// --- The planet and the sky ----------------------------------------------------------------------------------

// The planet's centre in the world: below the ship's path, the ship kPlanetAltitude above the ocean half way
// through FLYBY (about 10 s in).
const DVec3 kPlanetCentre{0.0, -double(kPlanetRadius) - double(kPlanetAltitude), double(kShipSpeed) * 10.0};

// The sun: 0.6 degrees across, warm white, bright enough that sunlit titanium sits near white at EV 0.
constexpr float kSunAngularRadius = 0.3f * kPi / 180.f;
constexpr Vec3 kSunRadiance{52000.f, 46000.f, 38000.f};

// A quaternion (x, y, z, w) of the rotation R^T (world to model).
std::array<float, 4> quaternionOf(const DPose& p) {
    // m = R^T: m[i][j] = r[j][i].
    const auto m = [&](int i, int j) { return p.r[j][i]; };
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

std::uint32_t scaled(std::uint32_t count, SceneDetail detail) {
    const double n = std::round(double(count) * std::clamp(double(detail.scale), 0.0, 1.0));
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(n));
}

std::uint32_t triangleCount(const std::vector<rt::Mesh>& meshes, const std::vector<SceneInstance>& instances) {
    std::uint32_t total = 0;
    for (const SceneInstance& in : instances)
        if (in.kind == rt::GeometryKind::Triangles)
            total += static_cast<std::uint32_t>(meshes[in.mesh].indices.size() / 3);
    return total;
}

} // namespace

// --- Public functions ---------------------------------------------------------------------------------------

ShipStats shipStats(SceneDetail detail) {
    return Ship(detail).stats();
}

std::vector<ShipShot> shipShots(const show::TimelineFrame& frame) {
    const ShotTimes t = shotTimes(frame);
    return {{0.f, float(t.accent)}, {float(t.accent), float(t.crossingEnd)}, {float(t.crossingEnd), float(t.duration)}};
}

ShipPose shipPose(double localSeconds) {
    const DPose p = pose(localSeconds);
    ShipPose out;
    out.position = single(p.position);
    for (int i = 0; i < 3; ++i)
        out.rows[i] = {float(p.r[i][0]), float(p.r[i][1]), float(p.r[i][2])};
    return out;
}

Camera shipWorldCamera(const show::TimelineFrame& frame) {
    const RelativeCamera rc = relativeCamera(frame);
    const DPose p = pose(frame.localSeconds);
    Camera c;
    const auto world = [&](DVec3 v) {
        if (!rc.modelLocked)
            return single(p.position + v);
        const DVec3 w{p.r[0][0] * v.x + p.r[0][1] * v.y + p.r[0][2] * v.z,
                      p.r[1][0] * v.x + p.r[1][1] * v.y + p.r[1][2] * v.z,
                      p.r[2][0] * v.x + p.r[2][1] * v.y + p.r[2][2] * v.z};
        return single(p.position + w);
    };
    c.eye = world(rc.eye);
    c.target = world(rc.target);
    c.up = {0, 1, 0};
    c.fovYDegrees = rc.fov;
    c.aperture = rc.aperture;
    c.focusDistance = rc.focus;
    c.shot = rc.shot;
    return c;
}

Camera shipCamera(const show::TimelineFrame& frame) {
    return carry(relativeCamera(frame), pose(frame.localSeconds));
}

Vec3 shipRenderPoint(const ShipPose& pose, Vec3 world) {
    return shipRenderDirection(pose, world - pose.position);
}

Vec3 shipRenderDirection(const ShipPose& pose, Vec3 d) {
    // R^T d: the columns of R dotted with d.
    return {pose.rows[0].x * d.x + pose.rows[1].x * d.y + pose.rows[2].x * d.z,
            pose.rows[0].y * d.x + pose.rows[1].y * d.y + pose.rows[2].y * d.z,
            pose.rows[0].z * d.x + pose.rows[1].z * d.y + pose.rows[2].z * d.z};
}

float shipExposureEV(std::uint32_t shot) {
    switch (shot) {
    case 1:
        return 0.f;
    case 2:
        return -0.2f;
    default:
        return 0.f;
    }
}

std::array<Vec3, 2> shipNozzles() {
    return {Vec3{kPodX, kPodY, kNozzleExitZ} * S, Vec3{-kPodX, kPodY, kNozzleExitZ} * S};
}

Ship::Ship(SceneDetail detail) {
    static_.meshes = buildMeshes();
    static_.materials = buildMaterials();
    shipInstances_ = buildShipInstances();
    // The planet: one analytic sphere, placed every frame.
    rt::SphereRecord planet{};
    planet.radius = kPlanetRadius;
    planet.color[0] = planet.color[1] = planet.color[2] = 1.f;
    planet.material = kOcean;
    static_.spheres = {planet};
    stars_ = scaled(kShipStars, detail);
    limb_ = scaled(kShipLimbSplats, detail);
    static_.splatShader = "modern_ship_splats.comp.spv";
    static_.capacity.instances = static_cast<std::uint32_t>(shipInstances_.size() + 1);
    static_.capacity.lights = 0;
    static_.capacity.materials = kMaterialCount;
    static_.capacity.spheres = 1;
    static_.capacity.splats = stars_ + limb_ + 1;

    stats_.stars = stars_;
    stats_.limbSplats = limb_;
    stats_.splats = stars_ + limb_ + 1;
    stats_.plumeVoxels = kShipPlumeVoxels; // the grid's size does not scale (yet)
    stats_.triangles = triangleCount(static_.meshes, shipInstances_);
    stats_.meshes = kMeshCount;
    stats_.materials = kMaterialCount;
}

std::uint32_t Ship::panelMaterial() const {
    return kPanelLines;
}

std::uint32_t Ship::strobeMaterial() const {
    return kStrobe;
}

std::vector<rt::Material> Ship::materials(const show::TimelineFrame& f) const {
    std::vector<rt::Material> m = static_.materials;
    // The panel lines breathe with the kick; the strobe flashes on every beat; the nozzles flicker with the
    // bass.
    m[kPanelLines].emissionScale = kPanelEmission * (0.35f + 1.9f * std::clamp(f.kick, 0.f, 1.f));
    const float beat = std::clamp(f.beatPhase, 0.f, 1.f);
    m[kStrobe].emissionScale = beat < 0.1f ? kStrobeEmission : 0.f;
    const float throb = 0.85f + 0.3f * std::clamp(f.bass, 0.f, 1.f);
    m[kThroat].emissionScale *= throb;
    m[kNozzleInner].emissionScale *= throb;
    return m;
}

SceneFrame Ship::frame(const show::TimelineFrame& f) const {
    SceneFrame out;
    const DPose p = pose(f.localSeconds);
    out.camera = carry(relativeCamera(f), p);
    out.instances = shipInstances_;
    // The planet in the ship's frame: R^T (C - P), rotated with the world.
    {
        const DVec3 centre = toModel(p, kPlanetCentre - p.position);
        rt::Mat34 m{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                m.m[i][j] = static_cast<float>(p.r[j][i]);
        m.m[0][3] = static_cast<float>(centre.x);
        m.m[1][3] = static_cast<float>(centre.y);
        m.m[2][3] = static_cast<float>(centre.z);
        SceneInstance planet{0, kOcean, m};
        planet.kind = rt::GeometryKind::Spheres;
        planet.firstSphere = 0;
        planet.sphereCount = 1;
        out.instances.push_back(planet);
    }
    out.materials = materials(f);

    const Vec3 sun = normalize(single(toModel(p, {kShipSunDirection.x, kShipSunDirection.y, kShipSunDirection.z})));
    EnvironmentDesc env = environmentDefaults(EnvironmentKind::Space);
    env.discDirection = sun;
    env.discAngularRadius = kSunAngularRadius;
    env.discRadiance = kSunRadiance;
    env.starDensity = 0.f; // the stars are splats, fixed to the world as the ship turns
    env.select = 1.f;      // no rect lights: next-event estimation samples the sun
    out.environment = env;
    out.exposureEV = shipExposureEV(out.camera.shot);

    // The splats (shaders/modern/ship_splats.comp): stars, the planet's limb and the sun's spikes.
    SplatFrame splats;
    splats.count = stars_ + limb_ + 1;
    const std::array<float, 4> q = quaternionOf(p);
    const DVec3 planet = toModel(p, kPlanetCentre - p.position);
    splats.params = {out.camera.eye.x, out.camera.eye.y, out.camera.eye.z, float(stars_),
                     q[0], q[1], q[2], q[3],
                     float(planet.x), float(planet.y), float(planet.z), float(limb_),
                     sun.x, sun.y, sun.z, kPlanetRadius};
    splats.fixedPointScale = 16384.f;
    out.splats = splats;
    return out;
}

} // namespace evo::modern
