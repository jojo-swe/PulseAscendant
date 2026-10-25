// VECTOR CITY, chapter 8: the registry entry and palette; the city's generation from its fixed seed (pinned
// hash values, every building inside its lot, disjoint, below the height limit); the flight (on the main
// street, never inside a building, banking before the accent); every retro frame's scene (exactly the faces
// through addTriangle, well under the triangle limit, near-plane clipping exercised, deterministic); the
// painter's order against a ray-cast reference; and the 320x200 palette index image on the GPU (the city
// covers the lower half, the expected ramps appear, deterministic per retro frame). Runs with the validation
// layer on and fails on any message it reports.

#include "gfx/context.hpp"
#include "ray_cast.hpp"
#include "retro/city.hpp"
#include "retro/effects.hpp"
#include "retro/math.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "retro/vec3.hpp"
#include "retro_stage_fixture.hpp"
#include "show/cues.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::CityBox;
using evo::retro::CityBuilding;
using evo::retro::CityFace;
using evo::retro::RetroScene;
using evo::retro::Vec3;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::test::RetroStageFixture;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr int kWidth = static_cast<int>(RetroStageFixture::kWidth);
constexpr int kHeight = static_cast<int>(RetroStageFixture::kHeight);
constexpr int kCityChapter = 8;
constexpr std::uint32_t kRate = evo::retro::kCityRetroRate;
// The most triangles a frame may draw here: well under the rasteriser's 2048.
constexpr std::size_t kTriangleBudget = 1400;

TimelineFrame city(double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(kCityChapter) + local);
    if (frame.chapter != kCityChapter || frame.effect != Effect::City)
        throw std::runtime_error("expected a VECTOR CITY frame at local time " + std::to_string(local));
    return frame;
}

double duration() {
    return city(0).chapterDuration;
}

std::uint32_t frameCount() {
    return static_cast<std::uint32_t>(std::ceil(duration() * kRate));
}

double timeIn(std::uint32_t retroFrame, double within = .5) {
    return std::min((retroFrame + within) / kRate, duration() - 1e-6);
}

TimelineFrame frameOf(std::uint32_t retroFrame) {
    return city(timeIn(retroFrame));
}

std::uint32_t frameAt(double seconds) {
    return static_cast<std::uint32_t>(seconds * kRate);
}

double accent() {
    return city(0).accentSeconds;
}

RetroScene sceneAt(std::uint32_t retroFrame) {
    RetroScene scene;
    evo::retro::cityScene(scene, frameOf(retroFrame), retroFrame);
    return scene;
}

bool sameScene(const RetroScene& a, const RetroScene& b) {
    return a.triangles.size() == b.triangles.size() &&
           (a.triangles.empty() || std::memcmp(a.triangles.data(), b.triangles.data(),
                                               a.triangles.size() * sizeof(a.triangles[0])) == 0) &&
           a.triangleDepth == b.triangleDepth && a.points.empty() && b.points.empty();
}

int brightness(std::uint32_t entry) {
    return static_cast<int>((entry & 0xff) + (entry >> 8 & 0xff) + (entry >> 16 & 0xff));
}

void testRegistry() {
    const evo::retro::EffectInfo& info = evo::retro::effectFor(Effect::City);
    CHECK(info.shader == nullptr);
    CHECK(info.retroRate == 35 && kRate == 35);
    CHECK(info.palette == &evo::retro::cityPalette);
    CHECK(info.background == &evo::retro::cityBackground);
    CHECK(info.scene == &evo::retro::cityScene);
    CHECK(info.text == nullptr && info.texture == nullptr);
    CHECK(evo::retro::cityBackground(city(3), 99) == evo::retro::kCityHaze);
}

// Index 0 black; the sky darkens from the horizon glow to the zenith; the sun brightens upwards; every
// building, road, lot, window and marking ramp runs into the haze colour (the background), the building ramps
// at kCityRampHaze; the two building ramps differ; the palette is the same at every frame.
void testPalette() {
    using namespace evo::retro;
    Palette palette{};
    palette.fill(0x12345678u);
    cityPalette(palette, city(1), 10);
    CHECK(palette[0] == rgba(0, 0, 0));
    for (std::uint32_t entry : palette)
        CHECK((entry >> 24) == 0xff);
    const std::uint32_t haze = palette[kCityHaze];
    CHECK(palette[kCitySky] == haze);
    CHECK(brightness(palette[kCitySky + 4]) > brightness(palette[kCitySky + kCitySkyShades - 1]) + 300);
    for (int k = 12; k < kCitySkyShades; ++k) // above the glow the sky only darkens
        CHECK(brightness(palette[kCitySky + k]) <= brightness(palette[kCitySky + k - 1]));
    for (int k = 1; k < kCitySunShades; ++k)
        CHECK(brightness(palette[kCitySun + k]) > brightness(palette[kCitySun + k - 1]));
    const int hazeShade = static_cast<int>(std::lround(kCityRampHaze * (kCityRampShades - 1)));
    for (std::uint8_t ramp : {kCityRampA, kCityRampB}) {
        const auto close = [&](std::uint32_t a, std::uint32_t b) {
            for (int s = 0; s < 24; s += 8)
                if (std::abs(static_cast<int>(a >> s & 0xff) - static_cast<int>(b >> s & 0xff)) > 8)
                    return false;
            return true;
        };
        CHECK(close(palette[static_cast<std::size_t>(ramp + hazeShade)], haze));
        CHECK(brightness(palette[ramp]) < 120); // near black at the street
        CHECK(brightness(palette[static_cast<std::size_t>(ramp + kCityRampShades - 1)]) > 600);
    }
    CHECK(palette[kCityRampA + 10] != palette[kCityRampB + 10]);
    for (std::uint8_t ramp : {kCityRoad, kCityLot, kCityWindow})
        CHECK(palette[static_cast<std::size_t>(ramp + kCityGroundShades - 1)] == haze);
    CHECK(palette[kCityMarking + kCityMarkingShades - 1] == haze);
    CHECK(brightness(palette[kCityRoad]) < brightness(palette[kCityLot]));
    CHECK(brightness(palette[kCityWindow]) > 550);
    Palette later{};
    cityPalette(later, city(12), 400);
    CHECK(later == palette);
}

bool sameBox(const CityBox& a, const CityBox& b) {
    return a.x0 == b.x0 && a.x1 == b.x1 && a.y0 == b.y0 && a.y1 == b.y1 && a.z0 == b.z0 && a.z1 == b.z1 &&
           a.ramp == b.ramp;
}

bool sameBuilding(const CityBuilding& a, const CityBuilding& b) {
    return sameBox(a.base, b.base) && a.tower == b.tower && (!a.tower || sameBox(a.top, b.top)) &&
           a.sx0 == b.sx0 && a.sx1 == b.sx1 && a.sz0 == b.sz0 && a.sz1 == b.sz1;
}

bool overlap(double a0, double a1, double b0, double b1) {
    return a0 < b1 && b0 < a1;
}

// The city from its fixed seed: pinned hash values (plain unsigned arithmetic, the same on every platform),
// and over a 41 x 41 block of lots: every building inside its lot (sidewalk kept), disjoint from the others,
// on its 2 x 2 grid cells, no roof above kCityMaxHeight, a tower inside its base and on its roof; the same
// lot twice is the same; heights, splits and both ramps vary.
void testGeneration() {
    using namespace evo::retro;
    CHECK(cityHash(0, 0, 0) == cityHash(0, 0, 0));
    std::printf("city: hash(0, 0, 0) = 0x%08x, hash(-3, 7, 42) = 0x%08x\n", cityHash(0, 0, 0),
                cityHash(-3, 7, 42));
    CHECK(cityHash(0, 0, 0) == 0x986f80edu);
    CHECK(cityHash(-3, 7, 42) == 0xf5c16cdau);
    CHECK(cityHash(1, 0, 0) != cityHash(0, 1, 0) && cityHash(0, 0, 1) != cityHash(0, 0, 0));
    std::set<std::size_t> splits;
    std::set<int> ramps;
    double lowest = 1e9, highest = 0;
    std::size_t towers = 0, buildings = 0;
    for (std::int32_t i = -20; i <= 20; ++i)
        for (std::int32_t m = -20; m <= 20; ++m) {
            const std::vector<CityBuilding> lot = cityLot(i, m);
            const std::vector<CityBuilding> again = cityLot(i, m);
            CHECK(lot.size() == again.size());
            for (std::size_t k = 0; k < lot.size(); ++k)
                CHECK(sameBuilding(lot[k], again[k]));
            CHECK(lot.size() == 1 || lot.size() == 2 || lot.size() == 4);
            splits.insert(lot.size() * 10 + static_cast<std::size_t>(lot[0].sx1 - lot[0].sx0));
            const double inner = kCityStreet / 2 + kCitySidewalk;
            for (std::size_t k = 0; k < lot.size(); ++k) {
                const CityBuilding& b = lot[k];
                ++buildings;
                const CityBox& box = b.base;
                CHECK(box.x0 >= kCityCell * i + inner - 1e-9 && box.x1 <= kCityCell * (i + 1) - inner + 1e-9);
                CHECK(box.z0 >= kCityCell * m + inner - 1e-9 && box.z1 <= kCityCell * (m + 1) - inner + 1e-9);
                CHECK(box.x0 < box.x1 && box.z0 < box.z1 && box.y0 == 0 && box.y1 > 0);
                CHECK(box.y1 <= kCityMaxHeight);
                // Its grid cells: column 0 lies below the lot's middle, 1 above.
                const double mx = kCityCell * i + kCityCell / 2, mz = kCityCell * m + kCityCell / 2;
                CHECK((b.sx0 == 0) == (box.x0 < mx) && (b.sx1 == 1) == (box.x1 > mx));
                CHECK((b.sz0 == 0) == (box.z0 < mz) && (b.sz1 == 1) == (box.z1 > mz));
                ramps.insert(box.ramp);
                lowest = std::min(lowest, box.y1);
                highest = std::max(highest, b.tower ? b.top.y1 : box.y1);
                if (b.tower) {
                    ++towers;
                    CHECK(b.top.x0 > box.x0 && b.top.x1 < box.x1 && b.top.z0 > box.z0 && b.top.z1 < box.z1);
                    CHECK(b.top.y0 == box.y1 && b.top.y1 > b.top.y0 && b.top.y1 <= kCityMaxHeight);
                }
                for (std::size_t j = 0; j < k; ++j)
                    CHECK(!(overlap(box.x0, box.x1, lot[j].base.x0, lot[j].base.x1) &&
                            overlap(box.z0, box.z1, lot[j].base.z0, lot[j].base.z1)));
            }
        }
    std::printf("city: %zu buildings in 41 x 41 lots, %zu with a tower, roofs %.1f .. %.1f\n", buildings,
                towers, lowest, highest);
    CHECK(splits.size() == 4); // whole, two along x, two along z, four
    CHECK(ramps.size() == 2);
    CHECK(towers > 50);
    CHECK(lowest < 10 && highest > 45);
}

// The eye's distance to a box (0 inside).
double distanceTo(const CityBox& box, const evo::retro::Camera& camera) {
    const double dx = std::max({box.x0 - camera.eye[0], 0.0, camera.eye[0] - box.x1});
    const double dy = std::max({box.y0 - camera.eye[1], 0.0, camera.eye[1] - box.y1});
    const double dz = std::max({box.z0 - camera.eye[2], 0.0, camera.eye[2] - box.z1});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The flight at accent `a` (the chapter's frame with its accent replaced), over every retro frame of the
// chapter and at least up to kCityTurnEase + 2 s past the accent: it starts on the main street and stays on
// it, low, until the climb; it moves at the flight speed without jumps; the eye keeps at least 2 units from
// every building; it banks at least 25 degrees by the accent (the wipe to 2026 ends there). On every
// `sceneEvery`-th frame the scene passes checkScene within the triangle budget. Returns the nearest
// building's distance. The chapter's frame with its accent replaced (the flight reads only the accent from
// it).
TimelineFrame withAccent(double a) {
    TimelineFrame frame = city(0);
    frame.accentSeconds = a;
    return frame;
}

double checkFlight(double a, std::uint32_t sceneEvery) {
    using namespace evo::retro;
    const TimelineFrame base = withAccent(a);
    const auto frames =
        std::max(frameCount(), static_cast<std::uint32_t>(std::ceil((a + kCityTurnEase + 2) * kRate)));
    double nearest = 1e9, maxRoll = 0, rollAtAccent = 0;
    std::size_t maxTriangles = 0;
    CityPose previous = cityPose(base, 0);
    CHECK(previous.x == 0 && previous.z == kCityStartZ && previous.y == kCityFlightHeight);
    // The most the bank may change in a retro frame, with 20 % to spare: the turn's smoothstep (slope 1.5)
    // over its ramp in or its ease out, plus the street sway (1.5 degrees at 1.1 radians a second) and its
    // fade over the climb.
    const double rollStep =
        1.2 *
        (radians(kCityBank) * 1.5 / std::min((kCityTurn[1] - kCityTurn[0]) * a, kCityTurnEase) +
         radians(1.5) * (1.1 + 1.5 / ((kCityClimb[1] - kCityClimb[0]) * a))) /
        kRate;
    for (std::uint32_t f = 0; f < frames; ++f) {
        const CityPose pose = cityPose(base, f);
        const double t = static_cast<double>(f) / kRate;
        CHECK(std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.z));
        const double step = std::hypot(pose.x - previous.x, pose.z - previous.z);
        if (f > 0)
            CHECK(std::abs(step - kCitySpeed / kRate) < 1e-3);
        CHECK(std::abs(pose.yaw - previous.yaw) < 0.02 && std::abs(pose.roll - previous.roll) < rollStep);
        previous = pose;
        if (t < kCityClimb[0] * a)
            CHECK(pose.x == 0 && pose.y == kCityFlightHeight);
        if (t <= kCityTurn[0] * a)
            CHECK(pose.yaw == 0 && pose.x == 0);
        if (t <= a)
            maxRoll = std::max(maxRoll, pose.roll);
        if (f == frameAt(a))
            rollAtAccent = pose.roll;
        const Camera camera = cityCamera(base, f);
        const auto cell = [](double v) {
            return static_cast<std::int32_t>(std::floor((v + kCityStreet / 2) / kCityCell));
        };
        for (std::int32_t i = cell(pose.x) - 1; i <= cell(pose.x) + 1; ++i)
            for (std::int32_t m = cell(pose.z) - 1; m <= cell(pose.z) + 1; ++m)
                for (const CityBuilding& b : cityLot(i, m)) {
                    nearest = std::min(nearest, distanceTo(b.base, camera));
                    if (b.tower)
                        nearest = std::min(nearest, distanceTo(b.top, camera));
                }
        if (f % sceneEvery == 0) {
            RetroScene scene;
            cityScene(scene, base, f);
            checkScene(scene);
            maxTriangles = std::max(maxTriangles, scene.triangles.size());
        }
    }
    const double degrees = 180 / std::numbers::pi;
    std::printf(
        "city: accent %5.2f s: nearest building %.2f units from the eye; bank %.1f deg at most, %.1f at "
        "the accent; at most %zu triangles\n",
        a, nearest, maxRoll * degrees, rollAtAccent * degrees, maxTriangles);
    CHECK(nearest >= 2);
    CHECK(maxRoll * degrees >= 25 && rollAtAccent * degrees >= 25);
    CHECK(maxTriangles <= kTriangleBudget);
    return nearest;
}

// The flight at the chapter's own accent (every retro frame of the chapter, from its timeline frames) and
// at accents across the whole clamped range kCityMinAccent .. kCityMaxAccent, so a retimed chapter keeps
// the flight safe; past the range the accent is clamped.
void testFlight() {
    using namespace evo::retro;
    const double a = accent();
    std::printf("city: %u retro frames, accent at %.2f s\n", frameCount(), a);
    CHECK(a > kCityMinAccent && a < kCityMaxAccent);
    for (std::uint32_t f = 0; f < frameCount(); f += 37) // every timeline frame carries the same accent
        CHECK(cityPose(frameOf(f), f).yaw == cityPose(withAccent(a), f).yaw);
    checkFlight(a, 5); // testScenes checks every scene at this accent
    for (double sweep : {kCityMinAccent, 6.5, 8.0, 12.0, 16.0, 20.0, 25.0, 30.0, 35.0, kCityMaxAccent})
        checkFlight(sweep, 5);
    for (std::uint32_t f : {0u, 100u, 300u, 1500u}) {
        CHECK(cityPose(withAccent(1), f).yaw == cityPose(withAccent(kCityMinAccent), f).yaw);
        CHECK(cityPose(withAccent(100), f).x == cityPose(withAccent(kCityMaxAccent), f).x);
    }
}

// View depth of a world point.
double viewDepth(const evo::retro::Camera& camera, const double p[3]) {
    return evo::retro::project(camera, p).z;
}

// A palette ramp, first and last index.
struct Ramp {
    std::uint8_t first, last;
};

int rampOf(std::uint8_t index, std::span<const Ramp> ramps) {
    for (std::size_t r = 0; r < ramps.size(); ++r)
        if (index >= ramps[r].first && index <= ramps[r].last)
            return static_cast<int>(r);
    return -1;
}

// Every retro frame: the scene is exactly cityFaces through addTriangle, in order; it stays within the
// triangle budget; the same retro frame always gives the same bytes. A gouraud face's three indices lie in
// one ramp (sky, sun, building A or B, road, lot), so the interpolation never leaks into a neighbouring
// ramp; a flat face is a marking or a window. Near-plane clipping: faces that cross view depth kNearPlane
// and still put pieces on the screen occur on at least 100 frames and two thirds of the street frames (the
// road under the camera shows once it reaches about 6 units ahead). The polygons of the city (walls, roofs,
// street tiles, markings and windows; two triangles each, the sky and sun left out) average about the
// number the epoch card states.
void testScenes() {
    using namespace evo::retro;
    const auto ramp = [](std::uint8_t first, int shades) {
        return Ramp{first, static_cast<std::uint8_t>(first + shades - 1)};
    };
    const Ramp gouraudRamps[] = {ramp(kCitySky, kCitySkyShades),     ramp(kCitySun, kCitySunShades),
                                 ramp(kCityRampA, kCityRampShades),  ramp(kCityRampB, kCityRampShades),
                                 ramp(kCityRoad, kCityGroundShades), ramp(kCityLot, kCityGroundShades)};
    const Ramp flatRamps[] = {ramp(kCityMarking, kCityMarkingShades), ramp(kCityWindow, kCityGroundShades)};
    std::size_t maxTriangles = 0, minTriangles = 1u << 30, crossingFrames = 0, streetFrames = 0,
                streetCrossing = 0;
    std::size_t crossingTotal = 0, cityFacesTotal = 0, cityFacesMax = 0, detailTotal = 0;
    const double a = accent();
    for (std::uint32_t f = 0; f < frameCount(); ++f) {
        const TimelineFrame frame = frameOf(f);
        const Camera camera = cityCamera(frame, f);
        const std::vector<CityFace> faces = cityFaces(frame, f);
        RetroScene expected, scene;
        std::size_t crossing = 0, cityFaceCount = 0;
        for (const CityFace& face : faces) {
            CHECK(face.mode == kFlat || face.mode == kGouraud);
            if (face.mode == kGouraud) {
                const int r = rampOf(face.index[0], gouraudRamps);
                CHECK(r >= 0 && rampOf(face.index[1], gouraudRamps) == r &&
                      rampOf(face.index[2], gouraudRamps) == r);
            } else {
                CHECK(rampOf(face.index[0], flatRamps) >= 0);
                CHECK(face.index[1] == face.index[0] && face.index[2] == face.index[0]);
                ++detailTotal;
            }
            const std::size_t before = expected.triangles.size();
            addTriangle(expected, camera, face.world, face.index, face.mode);
            const double z[3] = {viewDepth(camera, face.world[0]), viewDepth(camera, face.world[1]),
                                 viewDepth(camera, face.world[2])};
            const bool crosses =
                std::min({z[0], z[1], z[2]}) < kNearPlane && std::max({z[0], z[1], z[2]}) > kNearPlane;
            if (crosses && expected.triangles.size() > before)
                ++crossing;
            if (face.index[0] >= kCityRampA) // not the sky or the sun
                ++cityFaceCount;
        }
        cityScene(scene, frame, f);
        CHECK(sameScene(scene, expected));
        checkScene(scene);
        maxTriangles = std::max(maxTriangles, scene.triangles.size());
        minTriangles = std::min(minTriangles, scene.triangles.size());
        cityFacesTotal += cityFaceCount;
        cityFacesMax = std::max(cityFacesMax, cityFaceCount);
        crossingTotal += crossing;
        if (crossing > 0)
            ++crossingFrames;
        if (static_cast<double>(f) / kRate < kCityClimb[0] * a) {
            ++streetFrames;
            if (crossing > 0) // the road under the camera, cut at the near plane, reaches the screen
                ++streetCrossing;
        }
        if (f % 97 == 0) { // the same frame again, after others: identical
            RetroScene again;
            cityScene(again, frameOf(f), f);
            CHECK(sameScene(again, scene));
        }
    }
    const double polygons = static_cast<double>(cityFacesTotal) / 2 / frameCount();
    std::printf(
        "city: %zu..%zu triangles a frame (budget %zu, limit %zu); city polygons %.0f a frame on "
        "average (%.0f of them markings and windows), %zu at most; %zu frames clip at the near plane (%zu "
        "faces in all; %zu of the %zu street frames)\n",
        minTriangles, maxTriangles, kTriangleBudget, kMaxTriangles, polygons,
        static_cast<double>(detailTotal) / 2 / frameCount(), cityFacesMax / 2, crossingFrames, crossingTotal,
        streetCrossing, streetFrames);
    const std::string card = evo::show::cues[kCityChapter].thenCard;
    CHECK(card.find(std::to_string(kCityPolygons) + " POLYGONS") != std::string::npos);
    CHECK(polygons > kCityPolygons - 50.0 && polygons < kCityPolygons + 50.0);
    CHECK(maxTriangles <= kTriangleBudget);
    CHECK(minTriangles >= 100);
    CHECK(crossingFrames >= 100 && streetCrossing * 3 >= streetFrames * 2);
}

// An independent reference for what a ray must show: the nearest of the ground plane and every building box
// (cityLot) in the cells within kCityDrawDistance of the eye, by a slab test, with no culling, ordering or
// faces involved. t is the distance along the ray (infinity for a miss: the ray goes to the sky), cosine
// the absolute cosine between the ray and the surface's normal there.
struct Surface {
    double t = std::numeric_limits<double>::infinity();
    double cosine = 1;
};

void hitBox(const evo::test::Ray& ray, const CityBox& box, Surface& nearest) {
    const double lo[3] = {box.x0, box.y0, box.z0}, hi[3] = {box.x1, box.y1, box.z1};
    double enter = -std::numeric_limits<double>::infinity(), leave = std::numeric_limits<double>::infinity();
    int axis = -1;
    for (int k = 0; k < 3; ++k) {
        const double o = ray.origin[static_cast<std::size_t>(k)],
                     d = ray.direction[static_cast<std::size_t>(k)];
        if (d == 0) {
            if (o < lo[k] || o > hi[k])
                return;
            continue;
        }
        const double t0 = std::min((lo[k] - o) / d, (hi[k] - o) / d),
                     t1 = std::max((lo[k] - o) / d, (hi[k] - o) / d);
        if (t0 > enter)
            enter = t0, axis = k;
        leave = std::min(leave, t1);
    }
    if (axis >= 0 && enter <= leave && enter > 0 && enter < nearest.t)
        nearest = {enter, std::abs(ray.direction[static_cast<std::size_t>(axis)])};
}

class CityReference {
public:
    explicit CityReference(const evo::retro::Camera& camera)
        : eye_{camera.eye[0], camera.eye[1], camera.eye[2]} {
        using namespace evo::retro;
        const std::int32_t i0 = cellOf(eye_[0] - kCityDrawDistance), i1 = cellOf(eye_[0] + kCityDrawDistance);
        const std::int32_t m0 = cellOf(eye_[2] - kCityDrawDistance), m1 = cellOf(eye_[2] + kCityDrawDistance);
        for (std::int32_t i = i0; i <= i1; ++i)
            for (std::int32_t m = m0; m <= m1; ++m) {
                if (!inReach(i, m))
                    continue;
                for (const CityBuilding& b : cityLot(i, m)) {
                    boxes_.push_back(b.base);
                    if (b.tower)
                        boxes_.push_back(b.top);
                }
            }
    }

    Surface nearest(const evo::test::Ray& ray) const {
        Surface surface;
        if (ray.direction[1] < 0) { // the ground, where the city has a cell
            const double t = -ray.origin[1] / ray.direction[1];
            const double x = ray.origin[0] + t * ray.direction[0], z = ray.origin[2] + t * ray.direction[2];
            if (inReach(cellOf(x), cellOf(z)))
                surface = {t, -ray.direction[1]};
        }
        for (const CityBox& box : boxes_)
            hitBox(ray, box, surface);
        return surface;
    }

private:
    static std::int32_t cellOf(double v) {
        using namespace evo::retro;
        return static_cast<std::int32_t>(std::floor((v + kCityStreet / 2) / kCityCell));
    }
    // A cell (its lot and the streets at its lower x and z) within the draw distance of the eye on the
    // ground.
    bool inReach(std::int32_t i, std::int32_t m) const {
        using namespace evo::retro;
        const double x0 = kCityCell * i - kCityStreet / 2, z0 = kCityCell * m - kCityStreet / 2;
        const double dx = std::max({x0 - eye_[0], 0.0, eye_[0] - (x0 + kCityCell)});
        const double dz = std::max({z0 - eye_[2], 0.0, eye_[2] - (z0 + kCityCell)});
        return dx * dx + dz * dz <= kCityDrawDistance * kCityDrawDistance;
    }

    Vec3 eye_;
    std::vector<CityBox> boxes_;
};

// The painter's order on the CPU: on every 9th retro frame, a ray through each pixel centre of an 80 x 50
// grid against the frame's faces (beyond the near plane). The face drawn last among those it hits must be
// the nearest (or tie with it: the two triangles of a quad, tiles that share an edge). And it must be what
// the independent reference (CityReference) sees: the sky or the sun (or nothing, the haze) where the
// reference misses; otherwise a face at the reference's distance, or up to 0.1 units in front of the
// reference's surface (a window 0.08 out from its wall, a marking 0.02 above the road), measured along the
// surface's normal. That catches faces or cells culled that the camera can see, which the first check
// cannot. Rays whose reference hit is within twice the near plane are left out (clipped).
void testPainter() {
    using namespace evo::retro;
    std::size_t rays = 0, wrong = 0, buildingRays = 0, referenced = 0, skyRays = 0, mismatched = 0;
    for (std::uint32_t f = 2; f < frameCount(); f += 9) {
        const TimelineFrame frame = frameOf(f);
        const Camera camera = cityCamera(frame, f);
        const std::vector<CityFace> faces = cityFaces(frame, f);
        const CityReference reference(camera);
        const Vec3 forward = viewBasis(camera).forward;
        for (int gy = 0; gy < 50; ++gy)
            for (int gx = 0; gx < 80; ++gx) {
                const evo::test::Ray ray = evo::test::rayThrough(camera, gx * 4 + 1.5, gy * 4 + 1.5);
                const double cosine = dot(ray.direction, forward);
                int nearest = -1, last = -1;
                double nearestT = std::numeric_limits<double>::infinity(), lastT = 0;
                for (std::size_t i = 0; i < faces.size(); ++i) {
                    const double t = evo::test::hit(ray, faces[i].world);
                    if (t <= 0 || t * cosine < kNearPlane)
                        continue;
                    if (t < nearestT)
                        nearest = static_cast<int>(i), nearestT = t;
                    last = static_cast<int>(i), lastT = t;
                }

                const Surface expected = reference.nearest(ray);
                if (expected.t * cosine >= 2 * kNearPlane) {
                    ++referenced;
                    bool matches = false;
                    if (!std::isfinite(expected.t)) {
                        ++skyRays;
                        matches = last < 0 || faces[static_cast<std::size_t>(last)].index[0] < kCityHaze;
                    } else if (last >= 0) {
                        const double slack = 0.1 / std::max(expected.cosine, 1e-3);
                        matches = lastT <= expected.t * (1 + 1e-9) + 1e-9 && lastT >= expected.t - slack;
                    }
                    if (!matches) {
                        if (mismatched < 5)
                            std::printf(
                                "city: frame %u, ray (%d, %d): last face %d at %.4f, reference %.4f\n", f, gx,
                                gy, last < 0 ? -1 : faces[static_cast<std::size_t>(last)].index[0],
                                last < 0 ? 0.0 : lastT, expected.t);
                        ++mismatched;
                    }
                }

                if (last < 0)
                    continue;
                ++rays;
                const std::uint8_t shown = faces[static_cast<std::size_t>(nearest)].index[0];
                if (shown >= kCityRampA && shown < kCityRoad)
                    ++buildingRays;
                if (lastT > nearestT * (1 + 1e-9))
                    ++wrong;
            }
    }
    std::printf(
        "city: painter's order: %zu rays, %zu on buildings, %zu wrong; against the reference: %zu rays "
        "(%zu to the sky), %zu mismatched\n",
        rays, buildingRays, wrong, referenced, skyRays, mismatched);
    CHECK(rays >= 200000 && buildingRays >= 50000);
    CHECK(wrong == 0);
    CHECK(referenced >= 200000 && skyRays >= 20000);
    CHECK(mismatched == 0);
}

bool isSky(std::uint8_t index) {
    return index >= evo::retro::kCitySky && index <= evo::retro::kCityHaze;
}

// The stage's picture at the street, mid flight, the bank and after the accent: the city covers most of the
// lower half; only the city's indices appear; at the street the sun shows near the middle, with windows,
// markings, road, lots and both building ramps; deterministic per retro frame.
void testImages(evo::gfx::Context& ctx) {
    using namespace evo::retro;
    RetroStageFixture fixture(ctx);
    const double a = accent();
    const std::uint32_t frames[] = {frameAt(1.5), frameAt(0.6 * a), frameAt(0.97 * a), frameAt(a + 4)};
    const double coverage[] = {0.85, 0.85, 0.6, 0.6};
    for (std::size_t n = 0; n < std::size(frames); ++n) {
        const std::uint32_t retroFrame = frames[n];
        bool rendered = false;
        const std::vector<std::uint8_t> image = fixture.render(city(timeIn(retroFrame, .1)), &rendered);
        CHECK(rendered && fixture.stage().retroFrame() == retroFrame);
        std::size_t lower = 0, lowerCity = 0;
        std::map<std::uint8_t, std::size_t> counts;
        int sunMin = kWidth, sunMax = -1;
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x) {
                const std::uint8_t v = image[static_cast<std::size_t>(y) * kWidth + x];
                ++counts[v];
                CHECK(v >= kCitySky && v < kCityWindow + kCityGroundShades &&
                      (v <= kCityHaze || v >= kCityRampA));
                if (y >= kHeight / 2) {
                    ++lower;
                    if (!isSky(v))
                        ++lowerCity;
                }
                if (v >= kCitySun && v < kCitySun + kCitySunShades)
                    sunMin = std::min(sunMin, x), sunMax = std::max(sunMax, x);
            }
        const auto range = [&](std::uint8_t from, int count) {
            std::size_t sum = 0;
            for (const auto& [index, c] : counts)
                if (index >= from && index < from + count)
                    sum += c;
            return sum;
        };
        const double covered = static_cast<double>(lowerCity) / static_cast<double>(lower);
        std::printf(
            "city: retro frame %u: %.1f %% of the lower half is city, %zu indices; sky %zu, sun %zu, A "
            "%zu, B %zu, road %zu, lot %zu, windows %zu, markings %zu px\n",
            retroFrame, 100 * covered, counts.size(), range(kCitySky, kCitySkyShades),
            range(kCitySun, kCitySunShades), range(kCityRampA, kCityRampShades),
            range(kCityRampB, kCityRampShades), range(kCityRoad, kCityGroundShades),
            range(kCityLot, kCityGroundShades), range(kCityWindow, kCityGroundShades),
            range(kCityMarking, kCityMarkingShades));
        CHECK(covered >= coverage[n]);
        CHECK(counts.size() >= 40);
        CHECK(range(kCityRampA, kCityRampShades) > 500 && range(kCityRampB, kCityRampShades) > 500);
        CHECK(range(kCityWindow, kCityGroundShades) > 20);
        if (n == 0) { // down the street: the road, its markings and the sun at the vanishing point
            CHECK(range(kCitySky, kCitySkyShades) > 500);
            CHECK(range(kCityRoad, kCityGroundShades) > 3000 && range(kCityLot, kCityGroundShades) > 200);
            CHECK(range(kCityMarking, kCityMarkingShades) > 10);
            CHECK(sunMin >= kWidth / 3 && sunMax < kWidth * 2 / 3);
        }

        // The same retro frame at another moment within it, with the music signals pushed, after another
        // frame: identical.
        fixture.render(city(timeIn(retroFrame + 40)), &rendered);
        CHECK(rendered);
        TimelineFrame again = city(timeIn(retroFrame, .9));
        again.kick = 1;
        again.energy = 1;
        again.beatPhase = .5f;
        CHECK(fixture.render(again, &rendered) == image);
        CHECK(rendered);
    }
    // A fresh stage renders the same retro frame identically.
    RetroStageFixture other(ctx);
    CHECK(other.render(city(timeIn(frames[1]))) == fixture.render(city(timeIn(frames[1], .3))));
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testPalette();
        testGeneration();
        testFlight();
        testScenes();
        testPainter();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testImages(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("city_test: %d checks passed\n", checksPassed);
    return 0;
}
