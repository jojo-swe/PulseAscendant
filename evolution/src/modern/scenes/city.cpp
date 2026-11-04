#include "modern/scenes/city.hpp"

#include "modern/mesh_builder.hpp"
#include "retro/city_plan.hpp"
#include "rt/light_tree.hpp"
#include "rt/random.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numbers>
#include <span>
#include <stdexcept>

namespace evo::modern {

// modern/studio.hpp's (whose lens constants would clash with this file's): ASCENT starts from its camera.
Camera studioCamera(const show::TimelineFrame& frame);

namespace {

using show::Effect;

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kCell = static_cast<float>(retro::kCityCell);
constexpr float kStreet = static_cast<float>(retro::kCityStreet);
constexpr float kKerb = 0.5f * kStreet;   // a street's half width: the kerb's distance from its centre line
constexpr float kPavement = 0.15f;        // the lots' paving stands this high over the street
constexpr float kFarReach = 9000.f;       // the ground reaches this far from the origin
constexpr float kFarCity = 3800.f;        // and the far city's blocks this far
constexpr std::uint32_t kSalt = 0x23C17u; // this scene's own hash salts start here
constexpr std::array<std::uint32_t, 3> kCloudGrid = {192, 48, 192}; // ASCENT's cloud deck, voxels

// --- Small helpers -----------------------------------------------------------------------------------------

float smooth(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float unitHash(std::uint32_t h) {
    return static_cast<float>(h >> 8) * (1.f / 16777216.f);
}

// Draw k of hash h, independent of its other draws (shaders/rt/city_patterns.glsl's cityDraw).
float draw(std::uint32_t h, std::uint32_t k) {
    return unitHash(rt::pcgHash(h + k * 0x9E3779B9u));
}

std::uint32_t lotHash(int i, int m, std::uint32_t salt) {
    return retro::cityHash(i, m, kSalt + salt);
}

rt::Mat34 place(Vec3 position, Vec3 size) {
    return compose(translate(position), scale(size));
}

rt::Mat34 place(Vec3 position, float yaw, Vec3 size) {
    return compose(translate(position), compose(rotate({0, 1, 0}, yaw), scale(size)));
}

void setColor(float out[3], Vec3 c) {
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
}

Vec3 colorOf(const float c[3]) {
    return {c[0], c[1], c[2]};
}

Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

// Appends `in` to `out`, placed by `t` (positions and normals; a uniform or axis scale keeps normals right
// after normalising).
void append(rt::Mesh& out, const rt::Mesh& in, const rt::Mat34& t) {
    const auto first = static_cast<std::uint32_t>(out.vertices.size());
    for (rt::Vertex v : in.vertices) {
        const Vec3 p = transformPoint(t, {v.position[0], v.position[1], v.position[2]});
        const Vec3 n = transformNormal(t, {v.normal[0], v.normal[1], v.normal[2]});
        setColor(v.position, p);
        setColor(v.normal, n);
        out.vertices.push_back(v);
    }
    for (std::uint32_t i : in.indices)
        out.indices.push_back(first + i);
}

// --- Meshes and materials ----------------------------------------------------------------------------------

enum Mesh : std::uint32_t {
    kUnitBox,      // -0.5 .. 0.5
    kUnitCylinder, // radius 0.5, height 1, capped
    kGround,       // quad facing +y
    kLampKit,      // the eight lamp posts of a lot, in the lot's frame
    kCarBody,
    kCarCabin,
    kCarWheels,
    kTrail, // a box -0.5 .. 0.5 along z with uv.x = z + 0.5 (tail 0, head 1)
    kSignQuad,
    kRoundedBox, // a unit box with slightly rounded edges (podiums, penthouses)
    kMeshCount
};

enum Material : std::uint32_t {
    kAsphalt = City::kAsphaltMaterial,
    kDeckAsphalt,
    kPaving,
    kBrick,
    kStone,
    kConcreteFacade,
    kDarkConcrete,
    kWhiteBands,
    kGlassBlue,
    kGlassBronze,
    kGlassGreen,
    kRoundGlass,
    kRooftop,
    kDarkMetal,
    kLampGlass, // the light tree's spheres: radiance from each sphere's colour * emission
    kCarPaint0,
    kCarPaint1,
    kCarPaint2,
    kCarPaint3,
    kCarGlass,
    kTyre,
    kHeadTrail,
    kTailTrail,
    kSign0, // eight sign colours
    kSign7 = kSign0 + 7,
    kSignFar,
    kCrownLight,
    kViaduct,
    kPlaster,
    kWindowFrame,
    kMaterialCount
};
constexpr std::uint32_t kFacadeMaterials[] = {kBrick,        kStone,      kConcreteFacade,
                                              kDarkConcrete, kWhiteBands, kGlassBlue,
                                              kGlassBronze,  kGlassGreen, kRoundGlass};

// A car's pair of light trails, 1.3 m apart, each 0.16 m wide and 0.1 m high, one unit long along z with
// uv.x = z + 0.5 (the tail 0, the head 1).
rt::Mesh trailMesh() {
    rt::Mesh m;
    const rt::Mesh streak = box({0.08f, 0.05f, 0.5f});
    append(m, streak, translate({-0.65f, 0.f, 0.f}));
    append(m, streak, translate({0.65f, 0.f, 0.f}));
    for (rt::Vertex& v : m.vertices) {
        v.uv[0] = v.position[2] + 0.5f;
        v.uv[1] = v.position[1] + 0.5f;
    }
    return m;
}

// The eight lamp posts of lot (0, 0) (cityLampHead's layout), in world space of that lot: a post on the
// pavement, an arm over the street, a housing over the light's sphere.
rt::Mesh lampKitMesh() {
    rt::Mesh out;
    const rt::Mesh unit = box({0.5f, 0.5f, 0.5f});
    for (std::uint32_t k = 0; k < kCityLampsPerLot; ++k) {
        const Vec3 head = cityLampHead(0, 0, k);
        // The post stands 0.45 m inside the kerb, behind the head: the arm points from it across the street.
        Vec3 out2{0, 0, 0}; // from the post towards the street
        const std::uint32_t side = k / 2;
        if (side == 0)
            out2 = {0, 0, -1};
        else if (side == 1)
            out2 = {0, 0, 1};
        else if (side == 2)
            out2 = {-1, 0, 0};
        else
            out2 = {1, 0, 0};
        const float reach = 2.1f; // head beyond the post
        const Vec3 post{head.x - out2.x * reach, 0.f, head.z - out2.z * reach};
        const float top = kCityLampHeight + 0.45f;
        append(out, unit, place({post.x, 0.5f * top, post.z}, {0.16f, top, 0.16f}));
        const Vec3 armCentre{0.5f * (post.x + head.x), top - 0.08f, 0.5f * (post.z + head.z)};
        const Vec3 armSize = side < 2 ? Vec3{0.1f, 0.1f, reach + 0.3f} : Vec3{reach + 0.3f, 0.1f, 0.1f};
        append(out, unit, place(armCentre, armSize));
        // The housing: a flat box over the sphere, open below.
        append(out, unit, place({head.x, head.y + kCityLampRadius + 0.06f, head.z}, {0.62f, 0.1f, 0.62f}));
    }
    return out;
}

// A prism: the polygon `profile` (z, y; counter-clockwise with z to the right and y up, star-shaped about
// its centroid)
// extruded across x from -halfWidth to +halfWidth, flat-shaded.
rt::Mesh extrude(std::span<const Vec2> profile, float halfWidth) {
    rt::Mesh m;
    const auto n = static_cast<std::uint32_t>(profile.size());
    Vec2 c{};
    for (const Vec2& p : profile)
        c = {c.x + p.x / static_cast<float>(n), c.y + p.y / static_cast<float>(n)};
    auto vertex = [&](float x, Vec2 p, Vec3 normal) {
        rt::Vertex v{};
        setColor(v.position, {x, p.y, p.x});
        setColor(v.normal, normal);
        v.uv[0] = p.x;
        v.uv[1] = p.y;
        m.vertices.push_back(v);
        return static_cast<std::uint32_t>(m.vertices.size() - 1);
    };
    for (int side = -1; side <= 1; side += 2) { // the two caps, fans about the centroid
        const float x = static_cast<float>(side) * halfWidth;
        const std::uint32_t centre = vertex(x, c, {static_cast<float>(side), 0, 0});
        const std::uint32_t first = static_cast<std::uint32_t>(m.vertices.size());
        for (const Vec2& p : profile)
            vertex(x, p, {static_cast<float>(side), 0, 0});
        for (std::uint32_t k = 0; k < n; ++k) {
            const std::uint32_t a = first + k, b = first + (k + 1) % n;
            if (side > 0)
                m.indices.insert(m.indices.end(), {centre, b, a});
            else
                m.indices.insert(m.indices.end(), {centre, a, b});
        }
    }
    for (std::uint32_t k = 0; k < n; ++k) { // the sides around the profile
        const Vec2 a = profile[k], b = profile[(k + 1) % n];
        // The outward normal of edge a -> b of a counter-clockwise (z, y) polygon seen from +x.
        const Vec3 normal = normalize(Vec3{0.f, -(b.x - a.x), b.y - a.y});
        const std::uint32_t v0 = vertex(-halfWidth, a, normal), v1 = vertex(halfWidth, a, normal),
                            v2 = vertex(halfWidth, b, normal), v3 = vertex(-halfWidth, b, normal);
        m.indices.insert(m.indices.end(), {v0, v1, v2, v0, v2, v3});
    }
    return m;
}

// A car, 4.5 m long along z (its front at +z), in three meshes: the painted body to the beltline, the glass
// cabin over it, and the wheels.
rt::Mesh carBody() {
    const std::array<Vec2, 8> profile = {{{2.25f, 0.32f},
                                          {2.28f, 0.62f},
                                          {2.05f, 0.82f},
                                          {0.9f, 0.98f},
                                          {-1.7f, 1.0f},
                                          {-2.22f, 0.92f},
                                          {-2.25f, 0.55f},
                                          {-2.1f, 0.3f}}};
    return extrude(profile, 0.9f);
}

rt::Mesh carCabin() {
    const std::array<Vec2, 5> profile = {
        {{0.85f, 0.97f}, {0.15f, 1.42f}, {-1.05f, 1.44f}, {-1.75f, 0.99f}, {-0.4f, 0.95f}}};
    return extrude(profile, 0.8f);
}

rt::Mesh carWheels() {
    rt::Mesh m;
    const rt::Mesh wheel = cylinder(0.33f, 0.24f, 20, true);
    for (const float z : {1.4f, -1.45f})
        for (const float x : {-0.8f, 0.8f})
            append(m, wheel, compose(translate({x, 0.33f, z}), rotate({0, 0, 1}, 0.5f * kPi)));
    return m;
}

std::vector<rt::Mesh> buildMeshes() {
    std::vector<rt::Mesh> meshes(kMeshCount);
    meshes[kUnitBox] = box({0.5f, 0.5f, 0.5f});
    meshes[kUnitCylinder] = cylinder(0.5f, 1.f, 64, true);
    rt::Mesh ground = quad(2.f * kFarReach, 2.f * kFarReach);
    for (rt::Vertex& v : ground.vertices) { // turn it to face +y: (x, y, 0) -> (x, 0, -y)
        const float y = v.position[1];
        v.position[1] = 0.f;
        v.position[2] = -y;
        v.normal[0] = 0.f;
        v.normal[1] = 1.f;
        v.normal[2] = 0.f;
    }
    meshes[kGround] = ground;
    meshes[kLampKit] = lampKitMesh();
    meshes[kCarBody] = carBody();
    meshes[kCarCabin] = carCabin();
    meshes[kCarWheels] = carWheels();
    meshes[kTrail] = trailMesh();
    meshes[kSignQuad] = quad(1.f, 1.f);
    meshes[kRoundedBox] = bevelBox({0.5f, 0.5f, 0.5f}, 0.04f, 2);
    return meshes;
}

rt::Material pbr(Vec3 color, float roughness, float metallic = 0.f) {
    rt::Material m;
    setColor(m.baseColor, color);
    m.roughness = roughness;
    m.metallic = metallic;
    return m;
}

rt::Material facade(rt::Pattern style, Vec3 wall, float wallRoughness, Vec3 glass, Vec3 room, float floor,
                    float wallMetallic = 0.f) {
    rt::Material m = pbr(wall, wallRoughness, wallMetallic);
    m.pattern = static_cast<std::uint32_t>(style);
    setColor(m.baseColor2, glass);
    setColor(m.emission, room);
    m.patternScale = floor;
    return m;
}

rt::Material emitter(Vec3 color, float scale) {
    rt::Material m = pbr({0.05f, 0.05f, 0.05f}, 0.4f);
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m.emission, color);
    m.emissionScale = scale;
    return m;
}

// The chapter's look of the materials: how wet the streets are and how many rooms are lit (the rooms'
// radiance scale).
struct Weather {
    float wetness = 0.85f;
    float signs = 1.f;     // every sign's level
    float farSign = 1.f;   // CITY NIGHT's red sign
    float windowsOn = 0.f; // METROPOLIS's flip: the share of dark windows switched on
    float ripples = 0.f;   // the puddles' ripple roughness
};

// The sign colours (Act II's "sodium and ice", Act I's city taking its 1993 gold): sodium, warm white, 5600 K
// white, red, ember, gold, warm white, sodium. kSignFar is CITY NIGHT's one red sign at the far end of its
// street, dark until the accent.
constexpr std::array<Vec3, 8> kSignColors = {{{1.f, 0.5f, 0.14f},
                                              {1.f, 0.8f, 0.58f},
                                              {0.86f, 0.92f, 1.f},
                                              {1.f, 0.06f, 0.03f},
                                              {1.f, 0.32f, 0.05f},
                                              {1.f, 0.68f, 0.24f},
                                              {1.f, 0.85f, 0.66f},
                                              {1.f, 0.55f, 0.18f}}};
constexpr float kSignRadiance = 1.6f;

std::vector<rt::Material> buildMaterials(const Weather& w) {
    std::vector<rt::Material> m(kMaterialCount);
    // Asphalt: the street pattern; its coat is the wetness.
    m[kAsphalt] = pbr({0.06f, 0.058f, 0.056f}, 0.88f);
    m[kAsphalt].pattern = static_cast<std::uint32_t>(rt::Pattern::CityStreet);
    m[kAsphalt].coat = w.wetness;
    m[kDeckAsphalt] = m[kAsphalt];
    m[kDeckAsphalt].patternScale = 1.f; // an avenue without junctions (city_patterns.glsl)
    m[kPaving] = pbr({0.25f, 0.24f, 0.23f}, 0.8f);
    m[kPaving].pattern = static_cast<std::uint32_t>(rt::Pattern::Concrete);
    setColor(m[kPaving].baseColor2, {0.17f, 0.165f, 0.16f});
    m[kPaving].patternScale = 1.2f;
    m[kPaving].coat = 0.5f * w.wetness;
    m[kPaving].coatRoughness = 0.12f;

    // Facades: wall, its roughness, the glass, a lit room's radiance, the floor height.
    // A lit room's radiance (cityRoomTint warms or cools it per room): warm, and well under the lamps, so a
    // lit window reads as a room and not as a white card.
    const Vec3 warmRoom{0.95f, 0.88f, 0.78f}, officeRoom{0.62f, 0.64f, 0.62f};
    m[kBrick] = facade(rt::Pattern::CityPunched, {0.24f, 0.11f, 0.07f}, 0.85f, {0.02f, 0.022f, 0.025f},
                       warmRoom, 3.3f);
    m[kStone] = facade(rt::Pattern::CityPunched, {0.34f, 0.31f, 0.26f}, 0.8f, {0.02f, 0.022f, 0.025f},
                       warmRoom, 3.6f);
    m[kConcreteFacade] = facade(rt::Pattern::CityBands, {0.27f, 0.27f, 0.26f}, 0.85f, {0.015f, 0.02f, 0.025f},
                                officeRoom, 3.5f);
    m[kDarkConcrete] = facade(rt::Pattern::CityPunched, {0.13f, 0.13f, 0.135f}, 0.75f, {0.02f, 0.02f, 0.022f},
                              warmRoom, 3.2f);
    m[kWhiteBands] = facade(rt::Pattern::CityBands, {0.42f, 0.41f, 0.39f}, 0.6f, {0.012f, 0.018f, 0.024f},
                            officeRoom, 3.7f);
    m[kGlassBlue] = facade(rt::Pattern::CityCurtain, {0.16f, 0.17f, 0.19f}, 0.35f, {0.012f, 0.022f, 0.038f},
                           officeRoom, 3.9f, 1.f);
    m[kGlassBronze] = facade(rt::Pattern::CityCurtain, {0.28f, 0.2f, 0.13f}, 0.3f, {0.03f, 0.022f, 0.014f},
                             officeRoom, 4.f, 1.f);
    m[kGlassGreen] = facade(rt::Pattern::CityCurtain, {0.2f, 0.21f, 0.2f}, 0.35f, {0.012f, 0.03f, 0.026f},
                            officeRoom, 3.8f, 1.f);
    m[kRoundGlass] = facade(rt::Pattern::CityRound, {0.55f, 0.56f, 0.58f}, 0.3f, {0.014f, 0.02f, 0.03f},
                            officeRoom, 4.1f, 1.f);

    m[kRooftop] = pbr({0.2f, 0.2f, 0.2f}, 0.85f);
    m[kRooftop].pattern = static_cast<std::uint32_t>(rt::Pattern::Concrete);
    setColor(m[kRooftop].baseColor2, {0.12f, 0.12f, 0.125f});
    m[kRooftop].patternScale = 0.f;
    m[kDarkMetal] = pbr({0.05f, 0.05f, 0.055f}, 0.45f, 1.f);
    m[kLampGlass] = emitter({0.f, 0.f, 0.f}, 1.f);
    const std::array<Vec3, 4> paints = {
        {{0.02f, 0.02f, 0.022f}, {0.25f, 0.02f, 0.02f}, {0.35f, 0.36f, 0.38f}, {0.03f, 0.06f, 0.12f}}};
    for (std::uint32_t k = 0; k < 4; ++k) {
        m[kCarPaint0 + k] = pbr(paints[k], 0.45f, 0.4f);
        m[kCarPaint0 + k].coat = 1.f;
        m[kCarPaint0 + k].coatRoughness = 0.05f;
    }
    m[kTyre] = pbr({0.025f, 0.025f, 0.025f}, 0.7f);
    m[kCarGlass] = pbr({0.01f, 0.012f, 0.014f}, 0.1f);
    m[kCarGlass].coat = 1.f;
    m[kCarGlass].coatRoughness = 0.01f;
    m[kHeadTrail] = emitter({1.f, 0.93f, 0.82f}, 30.f);
    m[kHeadTrail].pattern = static_cast<std::uint32_t>(rt::Pattern::CityTrail);
    m[kTailTrail] = emitter({1.f, 0.06f, 0.03f}, 26.f);
    m[kTailTrail].pattern = static_cast<std::uint32_t>(rt::Pattern::CityTrail);
    for (std::uint32_t k = 0; k < 8; ++k) {
        m[kSign0 + k] = emitter(kSignColors[k], kSignRadiance);
        m[kSign0 + k].pattern = static_cast<std::uint32_t>(rt::Pattern::CitySign);
    }
    m[kSignFar] = emitter(kSignColors[3], kSignRadiance * 1.6f);
    m[kSignFar].pattern = static_cast<std::uint32_t>(rt::Pattern::CitySign);
    for (std::uint32_t k = kSign0; k <= kSignFar; ++k)
        m[k].emissionScale *= w.signs;
    for (std::uint32_t f : kFacadeMaterials)
        m[f].coat = w.windowsOn; // the share of dark windows switched on (city_patterns.glsl)
    m[kAsphalt].coatRoughness = m[kDeckAsphalt].coatRoughness = w.ripples;
    m[kSignFar].emissionScale *= w.farSign;
    m[kCrownLight] = emitter({1.f, 0.9f, 0.75f}, 6.f);
    m[kViaduct] = pbr({0.32f, 0.31f, 0.3f}, 0.8f);
    m[kViaduct].pattern = static_cast<std::uint32_t>(rt::Pattern::Concrete);
    setColor(m[kViaduct].baseColor2, {0.22f, 0.21f, 0.2f});
    m[kPlaster] = pbr({0.12f, 0.115f, 0.11f}, 0.9f);
    m[kWindowFrame] = pbr({0.02f, 0.02f, 0.02f}, 0.5f, 0.6f);
    return m;
}

// The chapter's weather and choreography of the materials (a pure function of the frame).
Weather cityWeather(const show::TimelineFrame& frame);

// --- The plan of the city ---------------------------------------------------------------------------------

// Distance from p to the segment a-b on the ground.
float segmentDistance(float px, float pz, float ax, float az, float bx, float bz) {
    const float dx = bx - ax, dz = bz - az;
    const float len2 = dx * dx + dz * dz;
    const float t = len2 > 0.f ? std::clamp(((px - ax) * dx + (pz - az) * dz) / len2, 0.f, 1.f) : 0.f;
    const float ex = px - ax - t * dx, ez = pz - az - t * dz;
    return std::sqrt(ex * ex + ez * ez);
}

// The chapters' camera paths on the ground, as segments (what a lower detail keeps the city around).
constexpr std::array<std::array<float, 4>, 7> kPaths = {{
    {0.f, -60.f, 0.f, 160.f},                     // VECTOR CITY down the main street
    {0.f, 160.f, 330.f, 470.f},                   // and its turn
    {525.f, 525.f, 760.f, 820.f},                 // ASCENT
    {kNightStreetX, 180.f, kNightStreetX, 560.f}, // CITY NIGHT
    {kAvenueX, 200.f, kAvenueX, 1500.f},          // METROPOLIS
    {kAvenueX, 1500.f, kAvenueX, 1900.f},
    {kDowntownX - 200.f, kDowntownZ - 200.f, kDowntownX + 200.f, kDowntownZ + 200.f},
}};

// The Studio's building in ASCENT: a lot whose tower holds the room the camera leaves through its window.
constexpr int kStudioLotI = 12, kStudioLotM = 12;
// The room's window on the tower's +z face: the Studio's window (1.2 x 1.0 m).
constexpr float kRoomWindowX = 525.f, kRoomWindowY = 43.5f, kRoomWindowW = 1.2f, kRoomWindowH = 1.0f;
constexpr float kStudioTowerHeight = 66.f;

// How far above its 1993 height a building of the downtown rises: 1 outside it, up to about 7 at its centre.
float downtownLift(float x, float z) {
    const float dx = x - kDowntownX, dz = z - kDowntownZ;
    const float d = std::sqrt(dx * dx + dz * dz) / kDowntownRadius;
    return 1.f + 6.f * smooth(1.f - d) * smooth(1.f - d) + 0.6f * smooth(1.35f - d);
}

} // namespace

float cityAvenueHeight(float z) {
    if (z < 900.f)
        return kAvenueDeck + (kAvenueCrest - kAvenueDeck) * smooth((z - 330.f) / 370.f);
    return 60.f + (kAvenueCrest - 60.f) * (1.f - smooth((z - 900.f) / 350.f));
}

bool cityLotKept(int i, int m, SceneDetail detail) {
    if (detail.scale >= 1.f)
        return true;
    const float radius = std::max(260.f, 2200.f * std::sqrt(std::max(detail.scale, 0.f)));
    const float x = kCell * (static_cast<float>(i) + 0.5f), z = kCell * (static_cast<float>(m) + 0.5f);
    for (const auto& s : kPaths)
        if (segmentDistance(x, z, s[0], s[1], s[2], s[3]) < radius)
            return true;
    return false;
}

Vec3 cityLampHead(int i, int m, std::uint32_t k) {
    const float x0 = kCell * static_cast<float>(i) + kKerb, z0 = kCell * static_cast<float>(m) + kKerb;
    const float span = kCell - kStreet; // the lot's paving, kerb to kerb
    const std::uint32_t side = (k / 2) % 4;
    const float along = (k % 2 == 0 ? 0.25f : 0.75f) * span + (side % 2 == 1 ? 3.5f : 0.f);
    const float over = 1.65f; // the head hangs this far over the street beyond the kerb
    switch (side) {
    case 0:
        return {x0 + along, kCityLampHeight, z0 - over};
    case 1:
        return {x0 + along, kCityLampHeight, z0 + span + over};
    case 2:
        return {x0 - over, kCityLampHeight, z0 + along};
    default:
        return {x0 + span + over, kCityLampHeight, z0 + along};
    }
}

float cityLampScale(bool switching, float seconds, float start, float wave, std::uint32_t hash) {
    float on = 1.f;
    if (switching) {
        const float at = start + wave / kWaveSpeed + 0.25f * unitHash(hash);
        const float strike = std::clamp((seconds - at) / 0.35f, 0.f, 1.f);
        const bool flickering = strike > 0.f && strike < 1.f;
        const float flicker =
            flickering && std::sin(90.f * seconds + 7.f * draw(hash, 1)) <= 0.f ? 0.3f : 1.f;
        on = draw(hash, 2) < 0.12f ? 1.f : strike * flicker;
    }
    return draw(hash, 3) < 0.015f ? 0.f : on; // a dead lamp
}

// --- Building the city ------------------------------------------------------------------------------------

namespace {

// shaders/rt/city_patterns.glsl's cityBuildingSeed, cityUnit and cityDraw.
std::uint32_t buildingSeed(const rt::Mat34& t) {
    const auto b = [&](int row) { return std::bit_cast<std::uint32_t>(t.m[row][3]); };
    return rt::pcgHash(b(0) ^ rt::pcgHash(b(1) ^ rt::pcgHash(b(2) + 0x632BE5ABu)));
}

float shaderDraw(std::uint32_t h, std::uint32_t k) {
    return unitHash(rt::pcgHash(h + k * 0x9E3779B9u));
}

} // namespace

std::uint64_t cityWindows(const rt::Material& m, const rt::Mat34& t) {
    const auto style = static_cast<rt::Pattern>(m.pattern);
    if (style != rt::Pattern::CityPunched && style != rt::Pattern::CityCurtain &&
        style != rt::Pattern::CityBands && style != rt::Pattern::CityRound)
        return 0;
    constexpr float kShop = 4.6f; // kCityShopFloor
    const float sx = length(Vec3{t.m[0][0], t.m[1][0], t.m[2][0]});
    const float sy = length(Vec3{t.m[0][1], t.m[1][1], t.m[2][1]});
    const float sz = length(Vec3{t.m[0][2], t.m[1][2], t.m[2][2]});
    const float y0 = t.m[1][3] - 0.5f * sy, y1 = t.m[1][3] + 0.5f * sy;
    const std::uint32_t building = buildingSeed(t);
    const float floorHeight = std::max(m.patternScale, 2.5f);
    const bool punched = style == rt::Pattern::CityPunched;
    const float bayTarget =
        floorHeight * (punched ? 0.95f + 0.5f * shaderDraw(building, 3) : 0.55f + 0.35f * shaderDraw(building, 3));
    // The window's span up its cell (sill, top), by style.
    float sill = 0.22f, top = 0.22f + 0.68f;
    if (punched)
        sill = 0.28f, top = 0.28f + 0.54f;
    else if (style == rt::Pattern::CityBands)
        sill = 0.4f, top = 0.4f + 2.f * (0.2f + 0.05f * shaderDraw(building, 5));
    // The floors whose window lies wholly on this wall, and the shop's.
    std::uint64_t floors = 0;
    for (int storey = 1;; ++storey) {
        const float base = kShop + float(storey - 1) * floorHeight;
        if (base + sill * floorHeight < y0)
            continue;
        if (base + top * floorHeight > y1)
            break;
        ++floors;
    }
    const bool shop = y0 <= kShop * 0.1f && y1 >= kShop * 0.82f;
    std::uint64_t windows = 0;
    const auto wall = [&](float width) {
        const auto bays = std::uint64_t(std::max(1.f, std::floor(width / bayTarget + 0.5f)));
        const auto shopBays = std::uint64_t(std::max(1.f, std::floor(width / 5.5f + 0.5f)));
        windows += bays * floors + (shop ? shopBays : 0);
    };
    if (style == rt::Pattern::CityRound) {
        wall(kPi * sx);
    } else {
        wall(sz), wall(sz), wall(sx), wall(sx);
    }
    return windows;
}

void City::buildCity(SceneDetail detail) {
    std::vector<SceneInstance>& out = instances_;
    std::vector<rt::SphereRecord>& spheres = static_.spheres;
    auto add = [&](std::uint32_t mesh, std::uint32_t material, const rt::Mat34& t) {
        out.push_back({mesh, material, t});
    };
    auto sphere = [&](Vec3 c, float r, Vec3 color, float emission) {
        rt::SphereRecord s{};
        setColor(s.center, c);
        setColor(s.prevCenter, c);
        s.radius = r;
        setColor(s.color, color);
        s.material = kLampGlass;
        s.emission = emission;
        spheres.push_back(s);
    };

    // The ground: one wet asphalt plane to the horizon; the lots stand on it.
    add(kGround, kAsphalt, identity());

    // Lamps (tree lights first, lot by lot), lots and buildings.
    struct Beacon {
        Vec3 at;
        float phase;
    };
    std::vector<Beacon> beacons;
    const Vec3 sodium{1.f, 0.5f, 0.15f}, led{1.f, 0.9f, 0.76f};
    constexpr float kSodium = 380.f, kLed = 300.f;
    stats_ = {};
    for (int m = kCityMinLotM; m < kCityMaxLotM; ++m)
        for (int i = kCityMinLotI; i < kCityMaxLotI; ++i) {
            if (!cityLotKept(i, m, detail))
                continue;
            const float cx = kCell * (static_cast<float>(i) + 0.5f),
                        cz = kCell * (static_cast<float>(m) + 0.5f);
            const float lift = downtownLift(cx, cz);
            // The lot's paving and its lamp posts.
            add(kRoundedBox, kPaving,
                place({cx, 0.5f * kPavement, cz}, {kCell - kStreet, kPavement, kCell - kStreet}));
            add(kLampKit, kDarkMetal,
                translate({kCell * static_cast<float>(i), 0.f, kCell * static_cast<float>(m)}));
            for (std::uint32_t k = 0; k < kCityLampsPerLot; ++k) {
                const std::uint32_t h = lotHash(i, m, 100 + k);
                const bool modern = lift > 1.4f || unitHash(h) < 0.15f;
                const Vec3 c = modern ? led : sodium;
                sphere(cityLampHead(i, m, k), kCityLampRadius, c * (0.85f + 0.3f * draw(h, 1)),
                       modern ? kLed : kSodium);
                lampWave_.push_back(0.f); // filled below
                lampHash_.push_back(lotHash(i, m, 200 + k));
            }

            // The buildings of the 1993 lot, grown with the downtown.
            const bool studioLot = i == kStudioLotI && m == kStudioLotM;
            const std::vector<retro::CityBuilding> lot = retro::cityLot(i, m);
            for (std::size_t b = 0; b < lot.size(); ++b) {
                const retro::CityBuilding& bd = lot[b];
                const auto salt = static_cast<std::uint32_t>(b) * 37u;
                const float x0 = static_cast<float>(bd.base.x0), x1 = static_cast<float>(bd.base.x1);
                const float z0 = static_cast<float>(bd.base.z0), z1 = static_cast<float>(bd.base.z1);
                const float w = x1 - x0, d = z1 - z0;
                const float bx = 0.5f * (x0 + x1), bz = 0.5f * (z0 + z1);
                float height = static_cast<float>(bd.base.y1);
                const float u = unitHash(lotHash(i, m, salt + 1));
                if (lift > 1.05f)
                    height = std::min(330.f, height * lift * (0.65f + 0.7f * u));
                if (studioLot)
                    continue; // built below
                ++stats_.buildings;
                // Style: downtown glass, the old town masonry.
                const float s = unitHash(lotHash(i, m, salt + 2));
                std::uint32_t material;
                if (height > 60.f)
                    material = s < 0.3f    ? kGlassBlue
                               : s < 0.5f  ? kGlassBronze
                               : s < 0.65f ? kGlassGreen
                               : s < 0.85f ? kWhiteBands
                                           : kConcreteFacade;
                else
                    material = s < 0.35f   ? kBrick
                               : s < 0.55f ? kStone
                               : s < 0.72f ? kConcreteFacade
                               : s < 0.86f ? kDarkConcrete
                                           : kWhiteBands;
                if (height <= 60.f) {
                    // A 1993 building: its box, its set-back tower, plant on the roof.
                    add(kUnitBox, material, place({bx, 0.5f * height, bz}, {w, height, d}));
                    float roof = height;
                    if (bd.tower) {
                        const float tx0 = static_cast<float>(bd.top.x0), tx1 = static_cast<float>(bd.top.x1);
                        const float tz0 = static_cast<float>(bd.top.z0), tz1 = static_cast<float>(bd.top.z1);
                        const float top = static_cast<float>(bd.top.y1) *
                                          (lift > 1.05f ? height / static_cast<float>(bd.base.y1) : 1.f);
                        add(kUnitBox, material,
                            place({0.5f * (tx0 + tx1), 0.5f * (height + top), 0.5f * (tz0 + tz1)},
                                  {tx1 - tx0, top - height, tz1 - tz0}));
                        roof = top;
                    }
                    const float plant = unitHash(lotHash(i, m, salt + 3));
                    if (plant < 0.45f && w > 8.f && d > 8.f) {
                        // A water tank (brick and stone) or a plant room.
                        if (material == kBrick || material == kStone)
                            add(kUnitCylinder, kRooftop,
                                place({bx + 0.2f * w * (plant - 0.2f), height + 2.f, bz - 0.15f * d},
                                      {3.2f, 4.f, 3.2f}));
                        else
                            add(kRoundedBox, kRooftop,
                                place({bx - 0.1f * w, height + 1.4f, bz + 0.1f * d},
                                      {0.35f * w, 2.8f, 0.3f * d}));
                    }
                    if (roof >= kBeaconHeight)
                        beacons.push_back({{bx, roof + 0.6f, bz}, unitHash(lotHash(i, m, salt + 4))});
                    continue;
                }

                // A tower: a podium, a shaft in up to three set-back tiers (or a round shaft), a crown.
                const float podium = 9.f + 6.f * unitHash(lotHash(i, m, salt + 5));
                add(kUnitBox, s < 0.5f ? kStone : kDarkConcrete,
                    place({bx, 0.5f * podium, bz}, {w, podium, d}));
                const bool roundShaft = unitHash(lotHash(i, m, salt + 6)) < 0.22f && std::min(w, d) > 18.f;
                float sw = 0.82f * w, sd = 0.82f * d;
                float base = podium;
                float top = height;
                if (roundShaft) {
                    const float r = 0.8f * std::min(w, d);
                    add(kUnitCylinder, kRoundGlass, place({bx, 0.5f * (base + top), bz}, {r, top - base, r}));
                    sw = sd = 0.7f * r;
                } else {
                    const int tiers = 1 + static_cast<int>(unitHash(lotHash(i, m, salt + 7)) * 3.f);
                    for (int t = 0; t < tiers; ++t) {
                        const float tierTop =
                            t + 1 == tiers ? height
                                           : base + (height - base) * (0.5f + 0.2f * static_cast<float>(t));
                        add(kUnitBox, material,
                            place({bx, 0.5f * (base + tierTop), bz}, {sw, tierTop - base, sd}));
                        base = tierTop;
                        sw *= 0.78f;
                        sd *= 0.78f;
                    }
                }
                // The crown: a lit band, a plant room, an antenna mast with the beacon on top.
                const float crown = unitHash(lotHash(i, m, salt + 8));
                float beaconAt = top + 0.6f;
                if (crown < 0.35f) {
                    add(kUnitBox, kCrownLight, place({bx, top - 1.6f, bz}, {sw + 0.3f, 1.2f, sd + 0.3f}));
                    add(kRoundedBox, kRooftop, place({bx, top + 2.5f, bz}, {0.6f * sw, 5.f, 0.6f * sd}));
                    beaconAt = top + 5.6f;
                } else if (crown < 0.7f) {
                    const float mast = 18.f + 40.f * unitHash(lotHash(i, m, salt + 9));
                    add(kRoundedBox, kRooftop, place({bx, top + 2.f, bz}, {0.5f * sw, 4.f, 0.5f * sd}));
                    add(kUnitCylinder, kDarkMetal,
                        place({bx, top + 4.f + 0.5f * mast, bz}, {0.9f, mast, 0.9f}));
                    beaconAt = top + 4.f + mast + 0.6f;
                } else {
                    add(kRoundedBox, kRooftop,
                        place({bx + 0.1f * sw, top + 3.f, bz}, {0.45f * sw, 6.f, 0.4f * sd}));
                    beaconAt = top + 6.6f;
                }
                beacons.push_back({{bx, beaconAt, bz}, unitHash(lotHash(i, m, salt + 10))});
            }
        }
    lampCount_ = static_cast<std::uint32_t>(spheres.size());

    // The far city (detail 1 only): one block per lot beyond the lamp-lit grid out to kFarCity around its
    // middle, so the city reaches the horizon; their windows (prefiltered at that distance) glow as a carpet.
    if (detail.scale >= 1.f) {
        const float midX = 0.5f * kCell * static_cast<float>(kCityMinLotI + kCityMaxLotI);
        const float midZ = 0.5f * kCell * static_cast<float>(kCityMinLotM + kCityMaxLotM);
        const int i0 = static_cast<int>(std::floor((midX - kFarCity) / kCell));
        const int i1 = static_cast<int>(std::ceil((midX + kFarCity) / kCell));
        const int m0 = static_cast<int>(std::floor((midZ - kFarCity) / kCell));
        const int m1 = static_cast<int>(std::ceil((midZ + kFarCity) / kCell));
        constexpr std::uint32_t kFarStyles[] = {kBrick,        kStone,      kConcreteFacade,
                                                kDarkConcrete, kWhiteBands, kGlassBlue};
        for (int m = m0; m < m1; ++m)
            for (int i = i0; i < i1; ++i) {
                if (i >= kCityMinLotI && i < kCityMaxLotI && m >= kCityMinLotM && m < kCityMaxLotM)
                    continue;
                const std::uint32_t h = lotHash(i, m, 900);
                const float inset = 1.5f + 5.f * unitHash(h * 3u);
                const float w = kCell - kStreet - 2.f * inset,
                            d = kCell - kStreet - 2.f * (1.5f + 5.f * unitHash(h * 5u));
                const float u = unitHash(h * 7u);
                const float height = 6.f + 30.f * u * u * u + 8.f * unitHash(h * 11u);
                const float cx = kCell * (static_cast<float>(i) + 0.5f),
                            cz = kCell * (static_cast<float>(m) + 0.5f);
                add(kUnitBox, kFarStyles[(h >> 9) % 6], place({cx, 0.5f * height, cz}, {w, height, d}));
                ++stats_.buildings;
            }
    }

    // The Studio's tower in ASCENT: a dark apartment block on its lot, with the room on its +z face.
    {
        const float x0 = kCell * kStudioLotI + kKerb + 1.5f, x1 = kCell * (kStudioLotI + 1) - kKerb - 1.5f;
        const float z0 = kCell * kStudioLotM + kKerb + 1.5f, z1 = kCell * (kStudioLotM + 1) - kKerb - 1.5f;
        const float h = kStudioTowerHeight;
        const float floorY = kRoomWindowY - 1.4f, ceilingY = floorY + 2.8f;
        const float roomX0 = kRoomWindowX - 2.5f, roomX1 = kRoomWindowX + 2.5f, roomZ0 = z1 - 4.6f;
        const float wall = 0.3f;
        const std::uint32_t mat = kBrick;
        auto boxFrom = [&](float ax, float ay, float az, float bx, float by, float bz,
                           std::uint32_t material) {
            add(kUnitBox, material,
                place({0.5f * (ax + bx), 0.5f * (ay + by), 0.5f * (az + bz)}, {bx - ax, by - ay, bz - az}));
        };
        boxFrom(x0, 0.f, z0, x1, floorY, z1, mat);          // below the room's floor
        boxFrom(x0, ceilingY, z0, x1, h, z1, mat);          // above its ceiling
        boxFrom(x0, floorY, z0, roomX0, ceilingY, z1, mat); // beside it
        boxFrom(roomX1, floorY, z0, x1, ceilingY, z1, mat);
        boxFrom(roomX0, floorY, z0, roomX1, ceilingY, roomZ0, kPlaster); // the room's back
        // The front wall around the window.
        const float wx0 = kRoomWindowX - 0.5f * kRoomWindowW, wx1 = kRoomWindowX + 0.5f * kRoomWindowW;
        const float wy0 = kRoomWindowY - 0.5f * kRoomWindowH, wy1 = kRoomWindowY + 0.5f * kRoomWindowH;
        boxFrom(roomX0, floorY, z1 - wall, roomX1, wy0, z1, kPlaster);
        boxFrom(roomX0, wy1, z1 - wall, roomX1, ceilingY, z1, kPlaster);
        boxFrom(roomX0, wy0, z1 - wall, wx0, wy1, z1, kPlaster);
        boxFrom(wx1, wy0, z1 - wall, roomX1, wy1, z1, kPlaster);
        // The window's slim black frame and its cross.
        boxFrom(wx0, wy0, z1 - wall - 0.02f, wx1, wy0 + 0.05f, z1 - 0.1f, kWindowFrame);
        boxFrom(wx0, wy1 - 0.05f, z1 - wall - 0.02f, wx1, wy1, z1 - 0.1f, kWindowFrame);
        boxFrom(kRoomWindowX - 0.025f, wy0, z1 - 0.2f, kRoomWindowX + 0.025f, wy1, z1 - 0.16f, kWindowFrame);
        // The room's floor, ceiling and inner walls are the faces of these boxes; a plaster lining inside.
        // The room's lining: floor, ceiling and side walls (the boxes around it carry the facade's windows
        // on every face, so the room is lined in front of them).
        boxFrom(roomX0, floorY, roomZ0, roomX1, floorY + 0.03f, z1 - wall, kPlaster);
        boxFrom(roomX0, ceilingY - 0.03f, roomZ0, roomX1, ceilingY, z1 - wall, kPlaster);
        boxFrom(roomX0, floorY, roomZ0, roomX0 + 0.03f, ceilingY, z1 - wall, kPlaster);
        boxFrom(roomX1 - 0.03f, floorY, roomZ0, roomX1, ceilingY, z1 - wall, kPlaster);
        ++stats_.buildings;
    }

    // METROPOLIS's elevated avenue: a deck on piers along the street at x = kAvenueX, in segments that follow
    // its climb, with parapets and lamps.
    std::uint32_t avenueLamps = 0;
    {
        constexpr float kSegment = 21.f;
        for (float z0 = kAvenueStartZ; z0 < kAvenueEndZ - 1.f; z0 += kSegment) {
            const float z1 = std::min(z0 + kSegment, kAvenueEndZ);
            const float y0 = cityAvenueHeight(z0), y1 = cityAvenueHeight(z1);
            const float length = std::hypot(z1 - z0, y1 - y0) + 0.05f; // overlapping a little at the joints
            const float pitch = -std::atan2(y1 - y0, z1 - z0);
            const Vec3 mid{kAvenueX, 0.5f * (y0 + y1), 0.5f * (z0 + z1)};
            const rt::Mat34 frame = compose(translate(mid), rotate({1, 0, 0}, pitch));
            auto piece = [&](std::uint32_t material, Vec3 offset, Vec3 size) {
                add(kUnitBox, material, compose(frame, place(offset, size)));
            };
            piece(kViaduct, {0.f, -0.9f, 0.f}, {15.f, 1.8f, length});
            piece(kDeckAsphalt, {0.f, 0.005f, 0.f}, {13.6f, 0.01f, length});
            piece(kViaduct, {-7.3f, 0.55f, 0.f}, {0.4f, 1.1f, length});
            piece(kViaduct, {7.3f, 0.55f, 0.f}, {0.4f, 1.1f, length});
        }
        for (float z = kAvenueStartZ + 21.f; z < kAvenueEndZ; z += kCell) {
            const float top = cityAvenueHeight(z) - 1.8f;
            add(kUnitBox, kViaduct, place({kAvenueX, 0.5f * top, z}, {3.f, top, 2.2f}));
        }
        for (float z = kAvenueStartZ + 10.f; z < kAvenueEndZ - 5.f; z += 24.f) {
            const float deck = cityAvenueHeight(z);
            for (int side = -1; side <= 1; side += 2) {
                const float sx = kAvenueX + static_cast<float>(side) * 6.9f;
                add(kUnitBox, kDarkMetal, place({sx, deck + 3.6f, z}, {0.14f, 7.2f, 0.14f}));
                add(kUnitBox, kDarkMetal,
                    place({sx - static_cast<float>(side) * 1.f, deck + 7.15f, z}, {2.f, 0.1f, 0.1f}));
                sphere({sx - static_cast<float>(side) * 1.9f, deck + 6.85f, z}, kCityLampRadius, led, kLed);
                lampWave_.push_back(0.f);
                lampHash_.push_back(rt::pcgHash(0xA7E0u + avenueLamps));
                ++avenueLamps;
            }
        }
    }

    // The beacons, last of the tree's lights.
    for (const Beacon& b : beacons) {
        sphere(b.at, 0.42f, {1.f, 0.06f, 0.03f}, 900.f);
        lampWave_.push_back(b.phase);
        lampHash_.push_back(0);
    }
    beaconCount_ = static_cast<std::uint32_t>(beacons.size());

    // VECTOR CITY's switch-on wave: each lamp's distance from where the camera is at the accent, along the
    // ground.
    for (std::uint32_t k = 0; k < lampCount_ + avenueLamps; ++k) {
        const rt::SphereRecord& s = spheres[k];
        const float dx = s.center[0] - kWaveX, dz = s.center[2] - kWaveZ;
        lampWave_[k] = std::sqrt(dx * dx + dz * dz);
    }

    stats_.lamps = lampCount_ + avenueLamps;
    stats_.beacons = beaconCount_;
    stats_.treeLights = static_cast<std::uint32_t>(spheres.size());
}

// --- Signs -------------------------------------------------------------------------------------------------

namespace {

struct Sign {
    Vec3 centre;
    Vec3 facing; // the emitting side (+x or -x)
    float width, height;
    std::uint32_t material;
    bool blade; // perpendicular to the facade, lit on both faces
};

// The signs along CITY NIGHT's street and VECTOR CITY's main street: lightboxes over the shops and blades
// standing out from the facades, from the lots' hashes.
std::vector<Sign> citySigns() {
    std::vector<Sign> signs;
    auto street = [&](float facadeLeft, float facadeRight, float from, float to, std::uint32_t salt,
                      int maxCount, std::vector<std::uint32_t> palette) {
        int count = 0;
        for (float z = from; z < to && count < maxCount; ++count) {
            const std::uint32_t h = rt::pcgHash(salt + static_cast<std::uint32_t>(count) * 0x9E3779B9u);
            const bool left = (h & 1u) != 0;
            const bool blade = unitHash(h * 3u) < 0.4f;
            const float facade = left ? facadeLeft : facadeRight;
            const Vec3 out{left ? 1.f : -1.f, 0.f, 0.f};
            Sign s;
            s.material = kSign0 + palette[(h >> 4) % palette.size()];
            s.blade = blade;
            s.facing = out;
            if (blade) {
                s.width = 0.7f;
                s.height = 2.5f + 3.f * unitHash(h * 5u);
                s.centre = {facade + out.x * 1.05f, 6.f + 0.5f * s.height + 4.f * unitHash(h * 7u), z};
            } else {
                s.width = 3.f + 3.f * unitHash(h * 5u);
                s.height = 0.75f + 0.5f * unitHash(h * 7u);
                s.centre = {facade + out.x * 0.08f, 4.95f + 0.5f * s.height, z};
            }
            // Skip the junctions.
            const float q = z - kCell * std::floor(z / kCell + 0.5f);
            if (std::abs(q) > kKerb + 2.5f)
                signs.push_back(s);
            z += 7.f + 9.f * unitHash(h * 11u);
        }
    };
    // CITY NIGHT's street: few signs, sodium and white; VECTOR CITY's: the bright ends of the 1993 city's
    // Gouraud ramps made neon (its amber as sodium gold, its pale rose as white) and one red.
    street(kNightStreetX - kKerb - 1.5f, kNightStreetX + kKerb + 1.5f, 400.f, 545.f, 0x51A1u, 14,
           {0, 1, 2, 7});
    const std::size_t vectorFirst = signs.size();
    street(-kKerb - 1.5f, kKerb + 1.5f, 172.f, 330.f, 0x51B7u, 14, {5, 2, 6, 5});
    // VECTOR CITY's one red: the first blade on the left past the puddle, which the puddle mirrors.
    for (std::size_t k = vectorFirst; k < signs.size(); ++k)
        if (signs[k].blade && signs[k].facing.x > 0.f && signs[k].centre.z > 185.f) {
            signs[k].material = kSign0 + 3;
            break;
        }
    // The red sign at the far end of CITY NIGHT's street, a blade read from down the street.
    Sign far;
    far.material = kSignFar;
    far.blade = true;
    far.width = 0.9f;
    far.height = 5.2f;
    far.facing = {-1.f, 0.f, 0.f};
    far.centre = {kNightStreetX + kKerb + 1.5f - 1.25f, 9.5f, 556.f};
    signs.push_back(far);
    return signs;
}

} // namespace

// --- The scene ---------------------------------------------------------------------------------------------

City::City(SceneDetail detail) : detail_(detail) {
    static_.meshes = buildMeshes();
    static_.splatShader = "modern_city_splats.comp.spv";
    static_.mediumShader = "modern_city_clouds.comp.spv";
    static_.capacity.mediumGrid = {kCloudGrid[0], kCloudGrid[1], kCloudGrid[2]};
    static_.materials = buildMaterials({});
    buildCity(detail);

    // Signs: a quad (two for a blade) per sign, each a rect light; a blade's housing between its faces.
    for (const Sign& s : citySigns()) {
        const std::uint32_t material = s.material;
        const Vec3 e =
            colorOf(static_.materials[material].emission) * static_.materials[material].emissionScale;
        auto face = [&](Vec3 centre, float yaw) {
            const rt::Mat34 t = place(centre, yaw, {s.width, s.height, 1.f});
            SceneInstance in{kSignQuad, material, t};
            in.light = static_cast<std::uint32_t>(lights_.size() + 1); // after the tree's entry
            instances_.push_back(in);
            lights_.push_back(rectLightForQuad(t, 1.f, 1.f, rt::LightKind::Emitter, e, 1.f));
            signMaterial_.push_back(material);
        };
        if (s.blade) {
            instances_.push_back(
                {kUnitBox, kDarkMetal, place(s.centre, {0.12f, s.height + 0.1f, s.width + 0.1f})});
            // Faces along +z and -z (the blade stands out across x, read from up and down the street).
            face(s.centre + Vec3{0.f, 0.f, 0.065f}, 0.f);
            face(s.centre - Vec3{0.f, 0.f, 0.065f}, kPi);
        } else {
            face(s.centre, s.facing.x > 0.f ? 0.5f * kPi : -0.5f * kPi);
        }
    }
    stats_.signLights = static_cast<std::uint32_t>(lights_.size());

    // CITY NIGHT's lamps: the kCityNightLamps live ones nearest its street, the district's edge frayed by each
    // lamp's own draw.
    {
        const Vec3 centre{kNightStreetX, 0.f, 470.f};
        std::vector<std::pair<float, std::uint32_t>> order;
        for (std::uint32_t k = 0; k < stats_.lamps; ++k) {
            if (cityLampScale(false, 0.f, 0.f, 0.f, lampHash_[k]) <= 0.f)
                continue; // a dead lamp
            const rt::SphereRecord& lamp = static_.spheres[k];
            const float d = std::hypot(lamp.center[0] - centre.x, lamp.center[2] - centre.z);
            order.push_back({d * (0.7f + 0.6f * draw(lampHash_[k], 5)), k});
        }
        const std::size_t lit = std::min<std::size_t>(kCityNightLamps, order.size());
        std::partial_sort(order.begin(), order.begin() + std::ptrdiff_t(lit), order.end());
        nightLit_.assign(stats_.lamps, 0);
        for (std::size_t k = 0; k < lit; ++k)
            nightLit_[order[k].second] = 1;
        stats_.nightLamps = static_cast<std::uint32_t>(lit);
    }

    // The light tree: one sphere instance holding every lamp and beacon, the frame's light 0.
    const auto n = static_cast<std::uint32_t>(static_.spheres.size());
    SceneInstance tree{0, kLampGlass, identity()};
    tree.kind = rt::GeometryKind::Spheres;
    tree.firstSphere = 0;
    tree.sphereCount = n;
    tree.light = 0;
    treeInstance_ = static_cast<std::uint32_t>(instances_.size());
    instances_.push_back(tree);
    treeLights_ = treeLightsOf(static_, tree, static_.materials, n);
    rt::RectLight entry{};
    entry.kind = static_cast<std::uint32_t>(rt::LightKind::Tree);
    entry.selectWeight = 1.f;
    entry.gate = rt::kNoLight;
    lights_.insert(lights_.begin(), entry);

    // Window count for the card: the facade pattern's own cell grid on every wall of every building
    // (cityWindows mirrors city_patterns.glsl's bays and floors).
    std::uint64_t windows = 0;
    for (const SceneInstance& in : instances_)
        windows += cityWindows(static_.materials[in.material], in.transform);
    stats_.windows = windows;

    // Capacities: the static city, the busiest chapter's traffic.
    std::uint32_t cars = 0;
    for (Effect e : {Effect::City, Effect::Ascent, Effect::CityNight, Effect::Metropolis}) {
        std::uint32_t c = 0;
        for (const Lane& l : lanesFor(e))
            c += l.cars;
        cars = std::max(cars, c);
    }
    static_.capacity.instances = static_cast<std::uint32_t>(instances_.size()) + 4 * cars;
    static_.capacity.lights = static_cast<std::uint32_t>(lights_.size());
    static_.capacity.materials = kMaterialCount;
    static_.capacity.spheres = n;
    static_.capacity.treeLights = n;
    show::TimelineFrame probe;
    probe.effect = Effect::CityNight;
    static_.capacity.splats = splatsFor(probe)->count;
}

// --- Splats ------------------------------------------------------------------------------------------------

namespace {

// shaders/modern/city_splats.comp's layout: kRainPoints points per drop.
constexpr std::uint32_t kRainPoints = 8;
constexpr std::uint32_t kFullRainDrops = 60000;

} // namespace

std::optional<SplatFrame> City::splatsFor(const show::TimelineFrame& frame) const {
    // The rain: CITY NIGHT only (the capacity probe, a frame without a duration, asks for the most).
    if (frame.effect != Effect::CityNight)
        return std::nullopt;
    const float detail = std::clamp(detail_.scale, 0.f, 1.f);
    const auto baseDrops = std::max(2000u, static_cast<std::uint32_t>(std::lround(kFullRainDrops * detail)));
    std::uint32_t drops = 2 * baseDrops;
    Vec3 velocity{};
    if (frame.chapterDuration > 0.0) {
        // The rain doubles over the bar after the accent.
        const float bar = std::max(cityBar(frame, 1) - cityBar(frame, 0), 0.5f);
        const float more = smooth(static_cast<float>(frame.localSeconds - frame.accentSeconds) / bar);
        drops = static_cast<std::uint32_t>(std::lround(static_cast<float>(baseDrops) * (1.f + more)));
        // The camera's velocity over the last 1/60 s: the streaks run along the drops' velocity relative to
        // it.
        show::TimelineFrame before = frame;
        constexpr double kStep = 1.0 / 60.0;
        before.localSeconds -= kStep;
        before.trackSeconds -= kStep;
        before.showSeconds -= kStep;
        velocity = (cityCamera(frame).eye - cityCamera(before).eye) / static_cast<float>(kStep);
        if (length(velocity) > 20.f)
            velocity = Vec3{}; // a cut
    }
    SplatFrame s;
    s.count = kRainPoints * drops;
    const float kept = detail >= 1.f ? 0.f : std::max(260.f, 2200.f * std::sqrt(detail));
    s.params = {static_cast<float>(drops),
                kept,
                16.f,
                22.f,
                velocity.x,
                velocity.y,
                velocity.z,
                1.f,
                0.f,
                0.f,
                0.f,
                0.f,
                0.f,
                0.f,
                0.f,
                0.f};
    return s;
}

// --- Traffic -----------------------------------------------------------------------------------------------

std::vector<City::Lane> City::lanesFor(Effect effect) const {
    std::vector<Lane> lanes;
    // Four lanes of a street along z at x (or along x at z), cars every ~spacing m over [from, to]. `view` is
    // the chapter's main direction of view: lanes running against it show head lights, with it tail lights.
    auto street = [&](bool alongZ, float at, float from, float to, float y, float spacing, Vec3 view,
                      std::uint32_t salt, bool inner = false) {
        for (int lane = 0; lane < 4; ++lane) {
            if (inner && (lane == 1 || lane == 3))
                continue; // two lanes only, the kerb lanes left free (where the camera walks)
            const float offset = lane < 2 ? -1.6f - 2.8f * static_cast<float>(lane)
                                          : 1.6f + 2.8f * static_cast<float>(lane - 2);
            // Right-hand traffic: lanes on +x of a z street run +z.
            const float dir = offset > 0.f ? 1.f : -1.f;
            Lane l;
            l.direction = alongZ ? Vec3{0, 0, dir} : Vec3{-dir, 0, 0};
            const float start = dir > 0.f ? from : to;
            l.origin = alongZ ? Vec3{at + offset, y, start} : Vec3{dir > 0.f ? to : from, y, at + offset};
            l.length = to - from;
            l.speed = 9.f + 6.f * unitHash(rt::pcgHash(salt + static_cast<std::uint32_t>(lane)));
            l.cars = std::max(1u, static_cast<std::uint32_t>(l.length / spacing));
            l.seed = rt::pcgHash(salt * 31u + static_cast<std::uint32_t>(lane));
            l.head = dot(l.direction, view) < 0.f;
            l.avenue = alongZ && at == kAvenueX && y > 0.f;
            lanes.push_back(l);
        }
    };
    switch (effect) {
    case Effect::City: {
        const Vec3 view{0.5f, 0.f, 0.85f};
        street(true, 0.f, -120.f, 760.f, 0.f, 22.f, view, 1, true);
        for (int k = 0; k < 6; ++k)
            street(false, kCell * static_cast<float>(3 + 2 * k), -300.f, 700.f, 0.f, 30.f, view, 10 + k);
        street(true, kCell * 6.f, 0.f, 900.f, 0.f, 26.f, view, 20);
        street(true, kCell * 12.f, 100.f, 1100.f, 0.f, 26.f, view, 21);
        break;
    }
    case Effect::Ascent: {
        const Vec3 view{0.6f, 0.f, 0.8f};
        street(false, kCell * 13.f, 200.f, 1200.f, 0.f, 24.f, view, 30);
        street(true, kCell * 13.f, 300.f, 1300.f, 0.f, 24.f, view, 31);
        street(true, kCell * 18.f, 300.f, 1400.f, 0.f, 26.f, view, 32);
        street(false, kCell * 18.f, 300.f, 1500.f, 0.f, 26.f, view, 33);
        street(true, kAvenueX, kAvenueStartZ, kAvenueEndZ, kAvenueDeck, 20.f, view, 34);
        break;
    }
    case Effect::CityNight: {
        const Vec3 view{0.f, 0.f, 1.f};
        street(true, kNightStreetX, 120.f, 640.f, 0.f, 26.f, view, 40, true);
        street(false, kCell * 10.f, 60.f, 460.f, 0.f, 26.f, view, 41);
        street(false, kCell * 8.f, 60.f, 460.f, 0.f, 30.f, view, 42);
        street(false, kCell * 12.f, 60.f, 460.f, 0.f, 30.f, view, 43);
        break;
    }
    default: { // METROPOLIS
        const Vec3 view{0.f, 0.f, 1.f};
        street(true, kAvenueX, kAvenueStartZ, kAvenueEndZ, kAvenueDeck, 16.f, view, 50);
        street(true, kAvenueX - kCell, 0.f, 2100.f, 0.f, 24.f, view, 51);
        street(true, kAvenueX + kCell, 0.f, 2100.f, 0.f, 24.f, view, 52);
        for (int k = 0; k < 8; ++k)
            street(false, kCell * static_cast<float>(20 + 4 * k), 700.f, 1900.f, 0.f, 28.f, view, 60 + k);
        break;
    }
    }
    return lanes;
}

void City::addTraffic(SceneFrame& f, Effect effect, float seconds) const {
    // A long exposure from the air (the cars' bodies blur away), a short one at street level and on the
    // avenue.
    const bool street = effect == Effect::CityNight || (effect == Effect::City && f.camera.shot == 1);
    const float exposure = street ? 0.1f : effect == Effect::Metropolis ? 0.25f : 0.45f;
    const bool bodies = street || effect == Effect::Metropolis;
    for (const Lane& l : lanesFor(effect)) {
        for (std::uint32_t c = 0; c < l.cars; ++c) {
            const std::uint32_t h = rt::pcgHash(l.seed + c * 0x9E3779B9u);
            // Evenly spread with some jitter, all at the lane's speed (no car runs into another).
            const float slot = (static_cast<float>(c) + 0.35f * unitHash(h)) / static_cast<float>(l.cars);
            float s = slot * l.length + l.speed * seconds;
            s = s - l.length * std::floor(s / l.length);
            Vec3 at = l.origin + l.direction * s;
            const float yaw = std::atan2(l.direction.x, l.direction.z);
            // On the elevated avenue the car follows its climb (pitched along the slope it drives).
            float pitch = 0.f;
            if (l.avenue) {
                at.y = cityAvenueHeight(at.z);
                const float rise = cityAvenueHeight(at.z + 0.5f * l.direction.z) -
                                   cityAvenueHeight(at.z - 0.5f * l.direction.z);
                pitch = -std::atan(rise);
            }
            const rt::Mat34 car =
                compose(translate(at), compose(rotate({0, 1, 0}, yaw), rotate({1, 0, 0}, pitch)));
            const std::uint32_t paint = kCarPaint0 + (h >> 7) % 4;
            if (bodies) {
                f.instances.push_back({kCarBody, paint, car});
                f.instances.push_back({kCarCabin, kCarGlass, car});
                f.instances.push_back({kCarWheels, kTyre, car});
            }
            // The trail runs back from the head lights (or from the tail), in the car's frame.
            const float trail = std::max(0.6f, l.speed * exposure);
            const float front = l.head ? 2.26f : -2.3f;
            f.instances.push_back({kTrail, l.head ? kHeadTrail : kTailTrail,
                                   compose(car, place({0.f, 0.68f, front - 0.5f * trail}, {1.f, 1.f, trail}))});
        }
    }
}

// --- Cameras -----------------------------------------------------------------------------------------------

float metropolisAvenueZ(float t, float d) {
    // At kMetropolisSpeed, eased in over the first second, reaching the crest's start (z 695) on D, then
    // slowing to 12 m/s over the next 3.4 s.
    const float z0 = 695.f - kMetropolisSpeed * (d - 0.5f);
    const float ease = 1.f;
    if (t < ease)
        return z0 + kMetropolisSpeed * 0.5f * t * t / ease;
    if (t < d)
        return z0 + kMetropolisSpeed * (t - 0.5f * ease);
    const float slow = 3.4f, u = std::min(t - d, slow);
    const float v1 = 12.f;
    const float z = z0 + kMetropolisSpeed * (d - 0.5f * ease) + kMetropolisSpeed * u -
                    (kMetropolisSpeed - v1) * u * u / (2.f * slow);
    return z + v1 * std::max(0.f, t - d - slow);
}

namespace {

struct Key {
    float time;
    Vec3 eye, target;
};

// Position along timed keys: a cubic Hermite through each key with Catmull-Rom tangents over the keys'
// times (so the speed is continuous whatever the spacing), easing in and out at the ends.
Vec3 hermite(Vec3 p0, Vec3 p1, Vec3 m0, Vec3 m1, float h, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return p0 * (2.f * t3 - 3.f * t2 + 1.f) + m0 * (h * (t3 - 2.f * t2 + t)) + p1 * (-2.f * t3 + 3.f * t2) +
           m1 * (h * (t3 - t2));
}

std::pair<Vec3, Vec3> alongTimed(std::span<const Key> keys, float time) {
    const int n = static_cast<int>(keys.size());
    if (time <= keys[0].time)
        return {keys[0].eye, keys[0].target};
    if (time >= keys[static_cast<std::size_t>(n - 1)].time)
        return {keys[static_cast<std::size_t>(n - 1)].eye, keys[static_cast<std::size_t>(n - 1)].target};
    int i = 0;
    while (i + 2 < n && time >= keys[static_cast<std::size_t>(i + 1)].time)
        ++i;
    auto tangent = [&](int k, bool eye) {
        if (k <= 0 || k >= n - 1)
            return Vec3{0, 0, 0}; // ease in and out at the ends
        const Key& a = keys[static_cast<std::size_t>(k - 1)];
        const Key& b = keys[static_cast<std::size_t>(k + 1)];
        return ((eye ? b.eye : b.target) - (eye ? a.eye : a.target)) / (b.time - a.time);
    };
    const Key& a = keys[static_cast<std::size_t>(i)];
    const Key& b = keys[static_cast<std::size_t>(i + 1)];
    const float h = b.time - a.time;
    const float t = (time - a.time) / h;
    return {hermite(a.eye, b.eye, tangent(i, true), tangent(i + 1, true), h, t),
            hermite(a.target, b.target, tangent(i, false), tangent(i + 1, false), h, t)};
}

Camera makeCamera(Vec3 eye, Vec3 target, float fov, std::uint32_t shot, Vec3 up = {0, 1, 0}) {
    Camera c;
    c.eye = eye;
    c.target = target;
    c.up = up;
    c.fovYDegrees = fov;
    c.focusDistance = std::max(length(target - eye), 1.f);
    c.target = eye + normalize(target - eye) * 25.f; // the aim, at a fixed distance (it turns, not slides)
    c.shot = shot;
    return c;
}

// The lenses (36 mm equivalents, as vertical fields of view on 16:9).
constexpr float kLens24 = 45.2f, kLens35 = 32.f, kLens50 = 23.1f, kLens85 = 13.6f;

// ASCENT's crane: out of the window, over the roofs, up to where the cloud deck lies (its top at
// kAscentCloudTop, burst through on D), level on the accent, the city a floor of light below.
constexpr float kAscentWindowZ = kCell * (kStudioLotM + 1) - kKerb - 1.5f; // the tower's +z face
constexpr float kAscentCloudTop = 380.f, kAscentCloudBase = 255.f;
// The deck's box (world) and voxels.
constexpr Vec3 kCloudLo{200.f, kAscentCloudBase, 250.f}, kCloudHi{1400.f, kAscentCloudTop + 30.f, 1450.f};

} // namespace

Vec3 cityFromStudio(Vec3 p) {
    constexpr float kStudioWindowX = -1.25f, kStudioWindowY = 1.5f, kStudioOutside = -1.05f;
    return {kRoomWindowX - (p.x - kStudioWindowX), kRoomWindowY + (p.y - kStudioWindowY),
            kAscentWindowZ + (kStudioOutside - p.z)};
}

Vec3 cityFromStudioDirection(Vec3 d) {
    return {-d.x, d.y, -d.z};
}

float cityBar(const show::TimelineFrame& frame, int bars) {
    const show::BeatGridView grid = show::beatGrid(frame.track);
    const double chapterStart = frame.trackSeconds - frame.localSeconds;
    const double accent = chapterStart + frame.accentSeconds;
    int beat = grid.beatAt(accent + 1e-6);
    if (beat < 0 || grid.count() == 0)
        return static_cast<float>(frame.accentSeconds); // no grid: the accent stands for D
    while (beat > 0 && grid.bar(beat) != 0)
        --beat; // D: the downbeat at or before the accent
    int found = 0;
    while (found != bars) {
        const int step = bars > 0 ? 1 : -1;
        const int next = beat + step;
        if (next < 0 || next >= grid.count())
            break;
        beat = next;
        if (grid.bar(beat) == 0)
            found += step;
    }
    return static_cast<float>(grid.beatTime(beat) - chapterStart);
}

namespace {

Weather cityWeather(const show::TimelineFrame& frame) {
    Weather w;
    const auto local = static_cast<float>(frame.localSeconds);
    const float accent = static_cast<float>(frame.accentSeconds);
    switch (frame.effect) {
    case Effect::City: {
        // A wet night. The signs flicker on with the snare across the wipe and hold from D.
        w.wetness = 0.85f;
        const float wipe = cityBar(frame, -2), d = cityBar(frame, 0);
        const float progress = std::clamp((local - wipe) / std::max(d - wipe, 0.1f), 0.f, 1.f);
        w.signs = local >= d ? 1.f : std::max(progress * progress, local >= wipe ? frame.snare : 0.f);
        break;
    }
    case Effect::CityNight: {
        // Sleet: every street wet, the puddles rippling on the kick; the red sign at the far end strikes on
        // the accent (a fluorescent flicker for 0.4 s).
        w.wetness = 1.f;
        w.ripples = 0.012f + 0.05f * frame.kick;
        const float t = local - accent;
        w.farSign = t < 0.f ? 0.f : (t > 0.4f ? 1.f : (std::sin(70.f * t) > 0.2f ? 1.f : 0.15f));
        break;
    }
    case Effect::Ascent:
        w.wetness = 0.8f;
        break;
    default: {
        // On the accent every dark window of the city switches on at once, then most go out again over four
        // bars.
        w.wetness = 0.7f;
        const float t = local - accent;
        const float bars = std::max(cityBar(frame, 4) - cityBar(frame, 0), 1.f);
        w.windowsOn = t < 0.f ? 0.f : 0.25f + 0.75f * std::exp(-3.f * t / bars);
        break;
    }
    }
    return w;
}

} // namespace

MediumFrame cityMedium(const show::TimelineFrame& frame) {
    MediumFrame m;
    // METROPOLIS's haze, lit by the 41,839 lamps of the whole city, came out 3.5 % darker at half rate.
    m.halfRate = frame.effect != Effect::Metropolis;
    const auto local = static_cast<float>(frame.localSeconds);
    FogDesc fog;
    fog.albedo = {0.93f, 0.94f, 0.96f};
    fog.phaseG = 0.72f; // forward scattering: the halos round the lamps, the shafts under the moon
    switch (frame.effect) {
    case Effect::City:
        fog.density = 0.0028f * (0.75f + 0.5f * frame.energy); // thickening with the music
        fog.falloff = 1.f / 45.f;
        break;
    case Effect::CityNight:
        fog.density = 0.011f; // sleet: wet air, sodium halos
        fog.falloff = 1.f / 35.f;
        break;
    case Effect::Ascent:
        fog.density = 0.003f;
        fog.falloff = 1.f / 60.f;
        break;
    default: // METROPOLIS: hazy low down, thinning as the avenue climbs
        fog.density = 0.0045f;
        fog.falloff = 1.f / 70.f;
        break;
    }
    m.fog = fog;
    if (frame.effect == Effect::Ascent) {
        // The cloud deck: the camera climbs into it and bursts out of its top on D; over the last two bars
        // before D the deck opens round the climb on each kick.
        const float d = cityBar(frame, 0), before = cityBar(frame, -2);
        const float progress = std::clamp((local - before) / std::max(d - before, 0.1f), 0.f, 1.f);
        GridMediumDesc grid;
        grid.size = kCloudGrid;
        grid.worldToMedium = mediumBox(kCloudLo, kCloudHi);
        grid.densityScale = 1.f;
        grid.albedo = {0.97f, 0.97f, 0.98f};
        grid.phaseG = 0.7f; // silver edges towards the moon
        grid.emissionScale = 0.f; // the deck scatters and emits nothing (city_clouds.comp bakes no emission)
        const Vec3 size = kCloudHi - kCloudLo;
        const float wind = 0.004f * local;
        const Vec3 climb{kRoomWindowX + 50.f, 0.f, kAscentWindowZ + 80.f}; // where the camera bursts out
        // A deck about one optical depth thick on average (three where it billows): a floor the city glows
        // through in its gaps, with silver edges towards the moon.
        grid.params = {0.58f,
                       0.06f,
                       0.8f * progress * (0.55f + 0.45f * frame.kick),
                       0.f,
                       wind,
                       0.3f * wind,
                       (climb.x - kCloudLo.x) / size.x,
                       (climb.z - kCloudLo.z) / size.z,
                       0.025f,
                       0.f,
                       0.f,
                       0.f,
                       0.f,
                       0.f,
                       0.f,
                       0.f};
        m.grid = grid;
    }
    return m;
}

std::vector<CityShot> cityShots(const show::TimelineFrame& frame) {
    const auto duration = static_cast<float>(frame.chapterDuration);
    const float accent = std::clamp(static_cast<float>(frame.accentSeconds), 0.f, duration);
    auto at = [&](float t) { return std::clamp(t, 0.f, duration); };
    switch (frame.effect) {
    case Effect::City:
        return {{0.f, accent}, {accent, duration}};
    case Effect::CityNight: {
        const float d = at(cityBar(frame, 0)), s1 = at(cityBar(frame, -2)), s3 = at(cityBar(frame, 5));
        return {{0.f, s1}, {s1, d}, {d, s3}, {s3, duration}};
    }
    case Effect::Metropolis: {
        const float s3 = at(cityBar(frame, 5));
        return {{0.f, s3}, {s3, duration}};
    }
    default:
        return {{0.f, duration}};
    }
}

Camera cityCamera(const show::TimelineFrame& frame) {
    const auto local = static_cast<float>(frame.localSeconds);
    const std::vector<CityShot> shots = cityShots(frame);
    std::uint32_t shot = 0;
    while (shot + 1 < shots.size() && local >= shots[shot + 1].start)
        ++shot;
    const CityShot& span = shots[shot];
    const float into = local - span.start, length = std::max(span.end - span.start, 1e-3f);
    const float accent = static_cast<float>(frame.accentSeconds);
    const float duration = static_cast<float>(frame.chapterDuration);
    const float d = cityBar(frame, 0);

    switch (frame.effect) {
    case Effect::City: {
        if (shot == 0) {
            // The 1993 flight, flown continuously and locked to it until the accent.
            const retro::CityPose p = retro::cityPoseAt(frame, static_cast<double>(local));
            const auto yaw = static_cast<float>(p.yaw), pitch = static_cast<float>(p.pitch),
                       roll = static_cast<float>(p.roll);
            const Vec3 forward{std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                               std::cos(yaw) * std::cos(pitch)};
            const Vec3 right{std::cos(yaw), 0.f, -std::sin(yaw)};
            const Vec3 level = cross(forward, right);
            const Vec3 up = level * std::cos(roll) + right * std::sin(roll);
            const Vec3 eye{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
            return makeCamera(eye, eye + forward * 30.f, static_cast<float>(retro::kCityFovY), shot, up);
        }
        // Cut on the accent: down on the main street, 24 mm, a hand's breadth over a puddle that mirrors the
        // signs, pushing in; from four bars after D it rises back to the 1993 altitude for the hand-off.
        // The push and the rise are separate moves (one spline through both would dip the camera under the
        // street before the rise).
        const float rise = cityBar(frame, 4);
        const Key street0{accent, {-4.5f, 0.28f, 168.f}, {-2.f, 2.2f, 210.f}};
        const Key street1{rise, {-4.4f, 0.32f, 175.f}, {-1.8f, 2.6f, 216.f}};
        const std::array<Key, 3> keys = {{
            street1,
            {rise + 0.6f * (duration - rise), {-2.f, 26.f, 186.f}, {4.f, 10.f, 236.f}},
            {duration + 0.3f, {2.f, 70.f, 200.f}, {14.f, 40.f, 262.f}},
        }};
        const float push = smooth((local - accent) / std::max(rise - accent, 0.1f));
        const auto [eye, target] = local < rise ? std::pair{lerp(street0.eye, street1.eye, push),
                                                             lerp(street0.target, street1.target, push)}
                                                : alongTimed(keys, local);
        Camera c = makeCamera(eye, target, kLens24, shot);
        // The puddle sharp, the street beyond soft; the focus pulls out over the bar the rise starts on.
        const float pull = smooth((local - rise) / std::max(cityBar(frame, 5) - rise, 0.5f));
        c.aperture = 0.012f * (1.f - pull);
        c.focusDistance = 6.f + pull * (evo::modern::length(target - eye) - 6.f);
        return c;
    }
    case Effect::Ascent: {
        // One crane, 35 mm, on from STUDIO NIGHT's last frame (the Studio's camera just out of its window,
        // carried to this room's window: the same eye, the same view, the same moon): over the roofs, up
        // through the cloud deck to burst out on D, and level on the accent with the city a floor of light
        // below.
        const float W = kRoomWindowX, Y = kRoomWindowY, Z = kAscentWindowZ;
        const float below = cityBar(frame, -2);
        const double cut = frame.showSeconds - frame.localSeconds;
        auto studio = [&](double before) {
            const Camera c = studioCamera(show::sampleTimeline(cut - before));
            const Vec3 eye = cityFromStudio(c.eye);
            return Key{static_cast<float>(-before), eye,
                       eye + cityFromStudioDirection(normalize(c.target - c.eye)) * 20.f};
        };
        const std::array<Key, 8> keys = {{
            studio(0.6),
            studio(1e-4),
            {3.4f, {W - 0.2f, Y + 2.6f, Z + 9.f}, {W + 8.f, Y - 5.f, Z + 40.f}},
            {6.5f, {W + 2.f, Y + 9.f, Z + 20.f}, {W + 30.f, Y - 12.f, Z + 70.f}},
            {below, {W + 20.f, 150.f, Z + 40.f}, {kDowntownX - 300.f, 40.f, kDowntownZ - 300.f}},
            {d, {W + 50.f, kAscentCloudTop + 12.f, Z + 80.f}, {W + 120.f, kAscentCloudTop + 40.f, Z + 180.f}},
            {accent,
             {W + 60.f, kAscentCloudTop + 22.f, Z + 95.f},
             {kDowntownX, kAscentCloudTop - 40.f, kDowntownZ}},
            {duration + 0.5f,
             {W + 150.f, kAscentCloudTop + 70.f, Z + 210.f},
             {kDowntownX, 120.f, kDowntownZ}},
        }};
        const auto [eye, target] = alongTimed(keys, local);
        return makeCamera(eye, target, kLens35, shot);
    }
    case Effect::CityNight: {
        const float s = smooth(into / length);
        if (shot == 0) {
            // High over the street in the rain, 85 mm: the lamps stacked down it, compressed.
            const Vec3 eye{kNightStreetX + 10.f, 44.f - 2.f * s, 150.f + 10.f * s};
            const Vec3 target{kNightStreetX - 1.f, 0.f, 430.f + 10.f * s};
            return makeCamera(eye, target, kLens85, shot);
        }
        if (shot == 1) {
            // Down to a wet crossing, 24 mm, 20 cm over a puddle: a sign's reflection fills the lower third.
            const Vec3 eye{kNightStreetX - 4.2f, 0.2f, 397.f + 1.2f * s};
            const Vec3 target{kNightStreetX - 3.2f, 1.6f, 430.f};
            Camera c = makeCamera(eye, target, kLens24, shot);
            c.aperture = 0.01f;
            c.focusDistance = 4.f;
            return c;
        }
        if (shot == 2) {
            // From D, a walk down the street on its pavement, 35 mm, towards the far end where the red sign
            // lights on the accent.
            const float z = 404.f + 1.4f * into;
            const Vec3 eye{kNightStreetX - 4.4f, 1.62f + 0.03f * std::sin(5.2f * local), z};
            const Vec3 target{kNightStreetX - 1.f, 2.4f, z + 60.f};
            Camera c = makeCamera(eye, target, kLens35, shot);
            c.aperture = 0.006f;
            c.focusDistance = 30.f;
            return c;
        }
        // Up to the rooftops, turning to look out over the city to the horizon.
        const float z = 404.f + 1.4f * (span.start - shots[2].start);
        const Vec3 eye{kNightStreetX - 4.4f + 3.f * s, 1.62f + 50.f * s, z + 12.f * s};
        const Vec3 target{kNightStreetX + 100.f * s, 2.4f + 40.f * s, z + 60.f + 300.f * s};
        return makeCamera(eye, target, kLens35, shot);
    }
    default: { // METROPOLIS
        if (shot == 0) {
            // Over the elevated avenue at 200 km/h, 24 mm, high enough that nothing passes close (no object
            // crosses more than 60 px of a 4K frame per frame: the shutter's slices stay a streak): the avenue
            // a river of light leading to the downtown, the city to the horizon. It climbs from bar 6 and
            // crests on D, the whole city below; the camera slows over the crest, rises and looks out over
            // it as every window switches on at the accent.
            const float z = metropolisAvenueZ(local, d);
            const float lift = kMetropolisRise * smooth((local - d) / 6.f);
            const Vec3 eye{kAvenueX - kMetropolisSide, cityAvenueHeight(z) + kMetropolisHeight + lift, z};
            const float look = smooth((local - d + 1.f) / 5.f);
            const Vec3 ahead{kAvenueX, cityAvenueHeight(z + 380.f) + 8.f, z + 380.f};
            const Vec3 over{kDowntownX + 150.f, 70.f, kDowntownZ + 300.f};
            return makeCamera(eye, lerp(ahead, over, look), kLens24, shot);
        }
        // A slow 85 mm pan across the downtown's skyline from far off at the towers' height (their lit crowns
        // against the night, the city's lights below), tilting up past their tops to the stars for the
        // hand-off.
        const float s = smooth(into / length);
        const Vec3 eye{kDowntownX - 1250.f, 175.f + 60.f * s, kDowntownZ - 1050.f + 120.f * s};
        const Vec3 target{kDowntownX - 260.f + 520.f * s, 150.f + 420.f * s * s * s,
                          kDowntownZ - 60.f + 260.f * s};
        return makeCamera(eye, target, kLens85, shot);
    }
    }
}

float cityExposureEV(const show::TimelineFrame& frame) {
    const std::uint32_t shot = cityCamera(frame).shot;
    switch (frame.effect) {
    case Effect::City:
        return shot == 0 ? 0.7f : -0.3f; // the street shot looks into the signs and their reflections
    case Effect::CityNight:
        return shot == 1 ? 0.f : 0.6f;
    default:
        return 0.3f;
    }
}

EnvironmentDesc cityEnvironment(Effect) {
    // Night: a high moon over the downtown, the city's glow along the horizon.
    EnvironmentDesc sky = environmentDefaults(EnvironmentKind::NightSky);
    // The Studio's moon (Studio::kMoonDirection) seen out of the room's window, which faces +z here (-z there).
    sky.discDirection = normalize(Vec3{0.2104f, 0.6913f, 0.6913f});
    sky.discAngularRadius = 0.6f * kPi / 180.f;
    sky.discRadiance = {38.f, 42.f, 48.f}; // moonlight: a faint fill under the street lights
    sky.horizon = {0.04f, 0.031f, 0.027f};
    sky.zenith = {0.004f, 0.007f, 0.018f};
    sky.ground = sky.horizon; // past the far city's edge the haze over the plain
    sky.townDensity = 0.f;
    sky.starDensity = 0.25f; // the city's glow drowns the faint stars
    sky.starBrightness = 0.35f;
    sky.haze = 0.55f;
    sky.select = 0.2f;
    return sky;
}

SceneFrame City::frame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    const Effect effect = frame.effect;
    const auto local = static_cast<float>(frame.localSeconds);
    const float accent = static_cast<float>(frame.accentSeconds);
    f.camera = cityCamera(frame);
    f.instances = instances_;
    f.lights = lights_;
    f.exposureEV = cityExposureEV(frame);
    f.environment = cityEnvironment(effect);
    f.materials = buildMaterials(cityWeather(frame));
    // The signs' lights follow their materials' levels.
    for (std::size_t k = 1; k < f.lights.size(); ++k) {
        const rt::Material& m = f.materials[signMaterial_[k - 1]];
        setColor(f.lights[k].emission, colorOf(m.emission) * m.emissionScale);
    }

    // The tree's lights this frame: the lamps (switching on in VECTOR CITY's wave from D), the beacons
    // blinking.
    const float wave = cityBar(frame, 0);
    f.treeLights = treeLights_;
    const std::uint32_t lamps = stats_.lamps;
    for (std::uint32_t k = 0; k < static_cast<std::uint32_t>(f.treeLights.size()); ++k) {
        TreeLightFrame& l = f.treeLights[k];
        if (k < lamps) {
            l.scale = cityLampScale(effect == Effect::City, local, wave, lampWave_[k], lampHash_[k]);
            if (effect == Effect::CityNight && draw(lampHash_[k], 4) < 0.04f)
                l.scale *= 1.f - 0.75f * frame.snare; // a tired lamp flickers on the snare
            if (effect == Effect::CityNight && nightLit_[k] == 0)
                l.scale = 0.f; // outside the lit district
        } else {
            // A beacon: a short red flash about once a second, each tower on its own clock; together on the
            // accent in ASCENT, and on the beat in METROPOLIS after its accent.
            const float phase = lampWave_[k];
            float cycle = local / 1.1f + phase;
            if ((effect == Effect::Ascent && local >= accent) ||
                (effect == Effect::Metropolis && local >= accent))
                cycle = effect == Effect::Metropolis ? frame.beatPhase : (local - accent) / 1.1f;
            const float c = cycle - std::floor(cycle);
            l.scale = std::exp(-c * 9.f) + 0.02f;
        }
    }

    addTraffic(f, effect, local);
    // Paths that find an emitter by chance after a rough bounce (a lamp mirrored in a puddle, a lit window or a
    // light trail across the street, none of them sampled) are held near a night wall's level instead of
    // firing as speckles: the lamps' light is next-event estimation's job.
    f.clampIndirect = 0.25f;
    f.splats = splatsFor(frame);
    f.medium = cityMedium(frame);
    if (effect == Effect::Ascent)
        f.environment.discRadiance = {150.f, 165.f, 185.f}; // above the clouds the moon is the key
    return f;
}

// --- The epoch cards
// ----------------------------------------------------------------------------------------------

double cityHorizonKm() {
    return std::sqrt(2.0 * kEarthRadius * double(kAvenueCrest + kMetropolisEye)) / 1000.0;
}

std::string City::nowCard(show::Effect effect) const {
    // The full city's numbers (each pinned against the City at detail 1 by the scene test).
    const std::string year = "2026";
    switch (effect) {
    case show::Effect::City:
        return year + kCardDot + groupThousands(kCityCardWindows) + " WINDOWS" + kCardDot +
               groupThousands(kCityCardLamps) + " LAMPS" + kCardDot + "WET STREETS";
    case show::Effect::Ascent:
        return year + kCardDot + "ASCENT" + kCardDot + "VOLUMETRIC CLOUDS" + kCardDot + "MOONLIGHT SHAFTS";
    case show::Effect::CityNight:
        return year + kCardDot + "CITY NIGHT" + kCardDot + groupThousands(kCityNightLamps) + " LIGHTS" +
               kCardDot + "LIGHT TREE" + kCardDot + "RAIN";
    case show::Effect::Metropolis:
        return year + kCardDot + "METROPOLIS" + kCardDot + groupThousands(kCityCardInstances) + " INSTANCES" +
               kCardDot + groupThousands(kCityCardLights) + " LIGHTS" + kCardDot +
               groupThousands(static_cast<std::uint64_t>(cityHorizonKm())) + " KM TO THE HORIZON";
    default:
        return {};
    }
}

} // namespace evo::modern
