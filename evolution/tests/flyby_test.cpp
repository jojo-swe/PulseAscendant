// FLYBY, chapter 1: the registry entry, the palette and the scene on the CPU (the ship's model, shading and
// ramps, its painter's order against a ray-cast reference, also from cameras the flight never reaches, its
// place above y = 136 px on every retro frame, the camera's swoop, the starfield, the engines' flicker), and
// the 320x200 palette index image from the retro stage on the GPU: deterministic per retro frame, the ship
// covering 5..40 % of the frame at three moments of the chapter (at least 4 % throughout) and drawn only
// inside its projected bounding box, over a field of stars. Runs with the validation layer on and fails on
// any message it reports.

#include "gfx/context.hpp"
#include "ray_cast.hpp"
#include "retro/effects.hpp"
#include "retro/flyby.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "retro/scroller.hpp"
#include "retro_stage_fixture.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::render::RetroStage;
using evo::retro::FlybyFace;
using evo::retro::FlybyPart;
using evo::retro::Palette;
using evo::retro::RetroScene;
using evo::show::Effect;
using evo::show::TimelineFrame;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

constexpr std::uint32_t kWidth = RetroStage::kWidth, kHeight = RetroStage::kHeight;
constexpr std::size_t kPixels = std::size_t{kWidth} * kHeight;
constexpr int kFlybyChapter = 1;
constexpr std::uint32_t kRate = evo::retro::kFlybyRetroRate;

// A FLYBY frame at `local` seconds into the chapter.
TimelineFrame flyby(double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(kFlybyChapter) + local);
    if (frame.chapter != kFlybyChapter || frame.effect != Effect::Flyby)
        throw std::runtime_error("expected a FLYBY frame at local time " + std::to_string(local));
    return frame;
}

double duration() {
    return flyby(0).chapterDuration;
}

// The last retro frame of the chapter.
std::uint32_t lastFrame() {
    return static_cast<std::uint32_t>(std::floor(duration() * kRate)) - 1;
}

// The retro frame `fraction` of the way through the chapter, and a time `within` of the way through it.
std::uint32_t frameAt(double fraction) {
    return static_cast<std::uint32_t>(fraction * lastFrame());
}
double timeIn(std::uint32_t retroFrame, double within = .5) {
    return (retroFrame + within) / kRate;
}

bool isShip(std::uint8_t index) {
    return index >= evo::retro::kFlybyShipFirst && index <= evo::retro::kFlybyShipLast;
}
bool isStar(std::uint8_t index) {
    return index >= evo::retro::kFlybyStar && index < evo::retro::kFlybyStar + evo::retro::kFlybyShades;
}

RetroScene sceneAt(std::uint32_t retroFrame) {
    RetroScene scene;
    evo::retro::flybyScene(scene, flyby(timeIn(retroFrame)), retroFrame);
    return scene;
}

int brightness(std::uint32_t entry) {
    return static_cast<int>((entry & 0xff) + (entry >> 8 & 0xff) + (entry >> 16 & 0xff));
}

void testRegistry() {
    const evo::retro::EffectInfo& info = evo::retro::effectFor(Effect::Flyby);
    CHECK(info.shader == nullptr); // a scene on black
    CHECK(info.background == nullptr);
    CHECK(info.retroRate == 35 && kRate == evo::retro::kScrollerRetroRate);
    CHECK(info.palette == &evo::retro::flybyPalette);
    CHECK(info.text == &evo::retro::flybyText);
    CHECK(info.scene == &evo::retro::flybyScene);
    CHECK(evo::retro::kFlybyIndex == evo::retro::kFlybyWhite);
}

// Black space and shadow at 0, the scroller's white at 255, five ramps that brighten entry by entry, the
// stars grey, and nothing else lit.
void testPalette() {
    Palette palette{};
    palette.fill(0x12345678u);
    evo::retro::flybyPalette(palette, flyby(1), 10);
    CHECK(palette[0] == evo::retro::rgba(0, 0, 0));
    CHECK(palette[255] == evo::retro::rgba(255, 255, 255));
    for (std::uint32_t entry : palette)
        CHECK((entry >> 24) == 0xff);
    for (std::size_t base : {16u, 32u, 48u, 64u, 128u}) {
        for (std::size_t i = base; i + 1 < base + 16; ++i)
            CHECK(brightness(palette[i + 1]) > brightness(palette[i]));
        CHECK(brightness(palette[base]) > 0);
    }
    // Hull grey-blue (blue strongest), wings teal (green and blue over red), cockpit amber (red over
    // green over blue), engines orange to white, stars grey.
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint32_t hull = palette[16 + i], wing = palette[32 + i], cockpit = palette[48 + i];
        const std::uint32_t star = palette[128 + i];
        CHECK((hull >> 16 & 0xff) >= (hull & 0xff));
        CHECK((wing >> 8 & 0xff) > (wing & 0xff) && (wing >> 16 & 0xff) > (wing & 0xff));
        CHECK((cockpit & 0xff) > (cockpit >> 8 & 0xff) && (cockpit >> 8 & 0xff) >= (cockpit >> 16 & 0xff));
        CHECK(std::abs(static_cast<int>(star & 0xff) - static_cast<int>(star >> 8 & 0xff)) <= 4);
    }
    CHECK(palette[64] == evo::retro::rgba(150, 34, 0) && palette[79] == evo::retro::rgba(255, 255, 255));
    for (std::size_t i = 1; i < 256; ++i)
        if (!(i >= 16 && i < 80) && !(i >= 128 && i < 144) && i != 255)
            CHECK(palette[i] == evo::retro::rgba(0, 0, 0));
    // Fixed: the same at any retro frame.
    Palette later{};
    evo::retro::flybyPalette(later, flyby(15), 600);
    CHECK(later == palette);
}

// The scene on the CPU: the ship's faces (flat, in its ramps, at most its model's faces after culling), the
// stars (in their greys, one or two pixels), the camera's swoop and the engines' flicker.
void testScene() {
    CHECK(evo::retro::kFlybyShipTriangles >= 40 && evo::retro::kFlybyShipTriangles <= 80);
    std::size_t fewestTriangles = 9999, fewestPoints = 9999, mostPoints = 0;
    std::set<std::uint8_t> engineIndices;
    for (std::uint32_t retroFrame = 0; retroFrame <= lastFrame(); retroFrame += 7) {
        const RetroScene scene = sceneAt(retroFrame);
        CHECK(scene.triangles.size() <= evo::retro::kFlybyShipTriangles);
        CHECK(scene.points.size() <= evo::retro::kFlybyStars);
        for (const evo::retro::RetroTriangle& t : scene.triangles) {
            CHECK(t.mode == evo::retro::kFlat && isShip(t.index[0]));
            if (t.index[0] >= evo::retro::kFlybyEngine)
                engineIndices.insert(t.index[0]);
        }
        for (const evo::retro::RetroPoint& p : scene.points)
            CHECK(isStar(p.index) && (p.size == 1 || p.size == 2));
        fewestTriangles = std::min(fewestTriangles, scene.triangles.size());
        fewestPoints = std::min(fewestPoints, scene.points.size());
        mostPoints = std::max(mostPoints, scene.points.size());
    }
    std::printf(
        "flyby: at least %zu ship triangles drawn, %zu..%zu stars, %zu engine shades over the chapter\n",
        fewestTriangles, fewestPoints, mostPoints, engineIndices.size());
    CHECK(fewestTriangles >= 20);     // a closed ship shows about half its faces
    CHECK(fewestPoints >= 300);       // most of the 512 stars, the ones behind the ship and off screen aside
    CHECK(engineIndices.size() >= 6); // the nozzles and flames flicker

    // The swoop: from behind and below the ship (nose along +z) to a three-quarter view of its right side,
    // above it.
    const evo::retro::Camera start = evo::retro::flybyCamera(flyby(0), 0);
    const evo::retro::Camera end = evo::retro::flybyCamera(flyby(duration() - .01), lastFrame());
    CHECK(start.eye[2] < 0 && start.eye[1] < 0 && std::abs(start.eye[0]) < -start.eye[2] / 4);
    CHECK(end.eye[0] > 0 && end.eye[0] > std::abs(end.eye[2]) / 2 && end.eye[1] > 0);
}

// The shading: shade 0 for a face turned from the light (ambient alone), 15 for one facing it squarely,
// rising in between; never past 15 into the next ramp.
void testShading() {
    using evo::retro::flybyShade;
    CHECK(flybyShade(evo::retro::kFlybyAmbient) == 0);
    CHECK(flybyShade(1.0) == 15 && flybyShade(1.0 - 1e-12) == 15 && flybyShade(1.5) == 15);
    CHECK(flybyShade(0.0) == 0 && flybyShade(-1.0) == 0);
    for (int shade = 0; shade < 16; ++shade)
        CHECK(flybyShade((shade + .5) / 16) == shade);
}

// The ship's faces on the CPU over the chapter: each in its declared ramp, the same faces flybyScene draws;
// all four ramps in use, each over its range of shades; and on every retro frame every vertex of the ship at
// y <= 136 px (about 132 at the lowest), clear of the greeting and the epoch card.
void testShipFaces() {
    // Per ramp (hull, wing, cockpit, engine): the lowest and highest shade drawn.
    std::map<std::uint8_t, std::pair<int, int>> shades;
    for (std::uint32_t retroFrame = 0; retroFrame <= lastFrame(); retroFrame += 7) {
        const TimelineFrame frame = flyby(timeIn(retroFrame));
        const std::vector<FlybyFace> faces = evo::retro::flybyShipFaces(frame, retroFrame);
        CHECK(!faces.empty() && faces.size() <= evo::retro::kFlybyShipTriangles);
        for (const FlybyFace& face : faces) {
            CHECK(face.ramp == evo::retro::kFlybyHull || face.ramp == evo::retro::kFlybyWing ||
                  face.ramp == evo::retro::kFlybyCockpit || face.ramp == evo::retro::kFlybyEngine);
            CHECK(face.index >= face.ramp && face.index < face.ramp + evo::retro::kFlybyShades);
            // Flames glow; wings are teal; the fuselage is hull or canopy; a pod is hull or nozzle.
            switch (face.part) {
            case FlybyPart::Flame:
                CHECK(face.ramp == evo::retro::kFlybyEngine);
                break;
            case FlybyPart::Wing:
                CHECK(face.ramp == evo::retro::kFlybyWing);
                break;
            case FlybyPart::Fuselage:
                CHECK(face.ramp == evo::retro::kFlybyHull || face.ramp == evo::retro::kFlybyCockpit);
                break;
            case FlybyPart::Pod:
                CHECK(face.ramp == evo::retro::kFlybyHull || face.ramp == evo::retro::kFlybyEngine);
                break;
            }
            const int shade = face.index - face.ramp;
            auto [entry, added] = shades.try_emplace(face.ramp, shade, shade);
            entry->second.first = std::min(entry->second.first, shade);
            entry->second.second = std::max(entry->second.second, shade);
        }
        // flybyScene draws exactly these faces, in this order (none of them is clipped: the ship is whole on
        // the screen), then the stars.
        RetroScene fromFaces;
        const evo::retro::Camera camera = evo::retro::flybyCamera(frame, retroFrame);
        for (const FlybyFace& face : faces) {
            const std::uint8_t indices[3] = {face.index, face.index, face.index};
            evo::retro::addTriangle(fromFaces, camera, face.world, indices, evo::retro::kFlat);
        }
        const RetroScene scene = sceneAt(retroFrame);
        CHECK(scene.triangles.size() == fromFaces.triangles.size());
        for (std::size_t i = 0; i < scene.triangles.size(); ++i)
            CHECK(std::equal(std::begin(scene.triangles[i].x), std::end(scene.triangles[i].x),
                             std::begin(fromFaces.triangles[i].x)) &&
                  std::equal(std::begin(scene.triangles[i].y), std::end(scene.triangles[i].y),
                             std::begin(fromFaces.triangles[i].y)) &&
                  scene.triangles[i].index[0] == fromFaces.triangles[i].index[0]);
    }
    const auto range = [&](std::uint8_t ramp) { return shades.at(ramp); };
    std::printf("flyby: shades hull %d..%d, wing %d..%d, cockpit %d..%d, engine %d..%d\n",
                range(evo::retro::kFlybyHull).first, range(evo::retro::kFlybyHull).second,
                range(evo::retro::kFlybyWing).first, range(evo::retro::kFlybyWing).second,
                range(evo::retro::kFlybyCockpit).first, range(evo::retro::kFlybyCockpit).second,
                range(evo::retro::kFlybyEngine).first, range(evo::retro::kFlybyEngine).second);
    CHECK(shades.size() == 4);
    // Each lit ramp from shade 0 (turned from the light: ambient alone) to near its top: the hull exactly
    // 0..15, the wings and canopy within one shade of their measured tops (14 and 15); the nozzles and
    // flames flicker over 11..15 and 5..9 by construction.
    CHECK(range(evo::retro::kFlybyHull) == std::make_pair(0, 15));
    CHECK(range(evo::retro::kFlybyWing).first == 0 && range(evo::retro::kFlybyWing).second >= 13);
    CHECK(range(evo::retro::kFlybyCockpit).first == 0 && range(evo::retro::kFlybyCockpit).second >= 14);
    CHECK(range(evo::retro::kFlybyEngine) == std::make_pair(5, 15));

    std::int32_t lowest = INT32_MIN, highest = INT32_MAX;
    for (std::uint32_t retroFrame = 0; retroFrame <= lastFrame(); ++retroFrame)
        for (const evo::retro::RetroTriangle& t : sceneAt(retroFrame).triangles)
            for (int v = 0; v < 3; ++v) {
                lowest = std::max(lowest, t.y[v]);
                highest = std::min(highest, t.y[v]);
            }
    std::printf("flyby: the ship's vertices span y = %.2f..%.2f px over every retro frame\n", highest / 16.0,
                lowest / 16.0);
    CHECK(lowest <= 136 * 16);
    CHECK(highest >= 4 * 16); // and it stays inside the frame at the top
}

// The painter's order on the CPU: rays through the pixel centres of an 80x50 grid (every 4th pixel each way)
// against the ship's faces in world space. The face drawn last among those a ray hits must show the nearest
// one's index. One known seam may show instead: a wing root sinks into the fuselage, so a wing drawn after
// the fuselage can show a sliver of its root through the hull, but only a sliver (within kSeamDepth of the
// nearest hit along the ray) and for few rays. An engine's two crossed flame triangles cut through each
// other as well, but they share one index, so they never show.
constexpr double kSeamDepth = 0.3; // world units; at most 0.273 over the flight

struct OrderErrors {
    std::size_t rays = 0, seams = 0, wrong = 0, worstFrameSeams = 0;
    double deepestSeam = 0;
};

// Adds one view's rays: the faces in painter's order and the camera that sees them.
void addOrderErrors(OrderErrors& errors, const std::vector<FlybyFace>& faces,
                    const evo::retro::Camera& camera, std::uint32_t retroFrame) {
    std::size_t frameSeams = 0;
    for (int gy = 0; gy < 50; ++gy)
        for (int gx = 0; gx < 80; ++gx) {
            const double px = gx * 4 + 1.5, py = gy * 4 + 1.5;
            const evo::test::Ray ray = evo::test::rayThrough(camera, px, py);
            int nearest = -1, last = -1;
            double nearestT = 1e30, lastT = 0;
            for (std::size_t i = 0; i < faces.size(); ++i) {
                const double t = evo::test::hit(ray, faces[i].world);
                if (t <= 0)
                    continue;
                if (t < nearestT)
                    nearest = static_cast<int>(i), nearestT = t;
                last = static_cast<int>(i), lastT = t;
            }
            if (last < 0)
                continue;
            ++errors.rays;
            const FlybyFace& a = faces[static_cast<std::size_t>(nearest)];
            const FlybyFace& b = faces[static_cast<std::size_t>(last)];
            if (lastT <= nearestT * (1 + 1e-9) || a.index == b.index)
                continue; // the nearest shows (or one level with it on a shared edge, or its twin)
            const bool wingRoot = (a.part == FlybyPart::Fuselage && b.part == FlybyPart::Wing) ||
                                  (a.part == FlybyPart::Wing && b.part == FlybyPart::Fuselage);
            if (wingRoot && lastT - nearestT <= kSeamDepth) {
                ++errors.seams;
                ++frameSeams;
                errors.deepestSeam = std::max(errors.deepestSeam, lastT - nearestT);
            } else {
                ++errors.wrong;
                if (errors.wrong <= 5)
                    std::printf("flyby: retro frame %u, pixel (%.0f, %.0f): part %d side %d drawn over the "
                                "nearer part %d side %d, %.3f behind it\n",
                                retroFrame, px, py, static_cast<int>(b.part), b.side,
                                static_cast<int>(a.part), a.side, lastT - nearestT);
            }
        }
    errors.worstFrameSeams = std::max(errors.worstFrameSeams, frameSeams);
}

// Every 7th retro frame of the flight.
void testPainterOrder() {
    OrderErrors errors;
    for (std::uint32_t retroFrame = 0; retroFrame <= lastFrame(); retroFrame += 7) {
        const TimelineFrame frame = flyby(timeIn(retroFrame));
        addOrderErrors(errors, evo::retro::flybyShipFaces(frame, retroFrame),
                       evo::retro::flybyCamera(frame, retroFrame), retroFrame);
    }
    std::printf("flyby: painter's order: %zu rays hit the ship; %zu at the wing-root seam (at most %zu in a "
                "frame, at most %.3f behind the nearest face); %zu wrong\n",
                errors.rays, errors.seams, errors.worstFrameSeams, errors.deepestSeam, errors.wrong);
    CHECK(errors.rays >= 10000);
    CHECK(errors.wrong == 0);
    // The wing root's strip under the hull's shoulder (the straight root lies up to 0.075 units inside the
    // kinked shoulder line): about 1 % of the ship's rays, at most 10 of a frame's ~450 (where the near
    // wing's leading edge runs on along the hull's underside).
    CHECK(errors.worstFrameSeams <= 15);
    CHECK(errors.seams <= errors.rays / 50);
}

// Cameras near the ship's centre plane (|x| < 0.34 in model space), which the flight never reaches: the
// fuselage is then drawn after the near side. At retro frames 0..3 the ship's attitude is level within a
// degree, so these eyes stay within 0.1 of the plane x = 0 in model space.
void testCentrePlane() {
    struct View {
        evo::retro::Camera camera;
        const char* name;
    };
    const View views[] = {
        {{{0.1, 5.5, -1.5}, {0, 0, 0}, {0, 0, 1}, 42}, "above"},
        {{{-0.15, -5.5, 0.5}, {0, 0, 0}, {0, 0, 1}, 42}, "below"},
        {{{0.05, 1.5, 7.5}, {0, 0, 0}, {0, 1, 0}, 42}, "ahead"},
        {{{-0.2, -1.2, -7.5}, {0, 0, 0}, {0, 1, 0}, 42}, "behind"},
    };
    OrderErrors errors;
    for (std::uint32_t retroFrame = 0; retroFrame < 4; ++retroFrame)
        for (const View& view : views) {
            const std::vector<FlybyFace> faces = evo::retro::flybyShipFaces(retroFrame, view.camera);
            // The branch taken: the last face but the flames is the fuselage's.
            const auto lastSolid = std::find_if(
                faces.rbegin(), faces.rend(), [](const FlybyFace& f) { return f.part != FlybyPart::Flame; });
            CHECK(lastSolid != faces.rend() && lastSolid->part == FlybyPart::Fuselage);
            addOrderErrors(errors, faces, view.camera, retroFrame);
        }
    std::printf("flyby: near the centre plane: %zu rays hit the ship, %zu at the seam (at most %.3f behind), "
                "%zu wrong\n",
                errors.rays, errors.seams, errors.deepestSeam, errors.wrong);
    CHECK(errors.rays >= 5000);
    CHECK(errors.wrong == 0);
    CHECK(errors.seams <= errors.rays / 50);
}

// The share of the frame the ship covers.
double coverage(const std::vector<std::uint8_t>& indices) {
    return static_cast<double>(std::count_if(indices.begin(), indices.end(), isShip)) / kPixels;
}

void testImages(evo::gfx::Context& ctx) {
    using Fixture = evo::test::RetroStageFixture;
    Fixture fixture(ctx);

    // Over the whole chapter, every second of it (diagnostic: the range of the ship's coverage).
    double least = 1, most = 0;
    for (std::uint32_t retroFrame = 0; retroFrame <= lastFrame(); retroFrame += kRate) {
        const double share = coverage(fixture.render(flyby(timeIn(retroFrame))));
        least = std::min(least, share);
        most = std::max(most, share);
    }
    std::printf("flyby: the ship covers %.1f..%.1f %% of the frame, sampled every second\n", 100 * least,
                100 * most);
    CHECK(least >= .04); // even edge-on, mid-chapter

    for (double fraction : {.12, .5, .88}) {
        const std::uint32_t retroFrame = frameAt(fraction);
        bool rendered = false;
        const std::vector<std::uint8_t> image = fixture.render(flyby(timeIn(retroFrame, .1)), &rendered);
        CHECK(rendered && fixture.stage().retroFrame() == retroFrame);

        // Only black, the ship's ramps, the stars' greys and the greeting's white.
        for (std::uint8_t index : image)
            CHECK(index == 0 || index == 255 || isShip(index) || isStar(index));

        // The ship covers 5..40 % of the frame, mostly in its upper 70 % (clear of the epoch card).
        const double share = coverage(image);
        std::size_t ship = 0, upper = 0, stars = 0;
        for (std::uint32_t y = 0; y < kHeight; ++y)
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const std::uint8_t index = image[y * kWidth + x];
                ship += isShip(index);
                upper += isShip(index) && y < kHeight * 7 / 10;
                stars += isStar(index);
            }

        // No ship colour outside the ship's projected bounding box: the extent of its triangles' vertices.
        const RetroScene scene = sceneAt(retroFrame);
        CHECK(!scene.triangles.empty());
        std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
        for (const evo::retro::RetroTriangle& t : scene.triangles)
            for (int v = 0; v < 3; ++v) {
                minX = std::min(minX, t.x[v]), maxX = std::max(maxX, t.x[v]);
                minY = std::min(minY, t.y[v]), maxY = std::max(maxY, t.y[v]);
            }
        std::size_t outside = 0;
        for (std::uint32_t y = 0; y < kHeight; ++y)
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const std::int32_t cx = static_cast<std::int32_t>(x) * 16 + 8;
                const std::int32_t cy = static_cast<std::int32_t>(y) * 16 + 8;
                outside +=
                    isShip(image[y * kWidth + x]) && (cx < minX || cx > maxX || cy < minY || cy > maxY);
            }
        std::printf(
            "flyby: retro frame %u (%.0f %% through): ship %.1f %% of the frame (%.0f %% of it in the "
            "upper 70 %%), box (%d..%d, %d..%d) px, %zu star pixels of %zu stars\n",
            retroFrame, 100 * fraction, 100 * share, 100.0 * upper / std::max<std::size_t>(ship, 1),
            minX / 16, maxX / 16, minY / 16, maxY / 16, stars, scene.points.size());
        CHECK(share >= .05 && share <= .40);
        CHECK(upper >= ship * 9 / 10);
        CHECK(outside == 0);
        CHECK(stars >= 300);
        std::set<std::uint8_t> starShades, shipShades;
        for (std::uint8_t index : image) {
            if (isStar(index))
                starShades.insert(index);
            if (isShip(index))
                shipShades.insert(index);
        }
        CHECK(starShades.size() >= 6); // stars at many depths
        CHECK(shipShades.size() >= 6); // faces in several shades and ramps

        // Deterministic: the same retro frame at another moment within it, with the music signals pushed,
        // after another frame, gives the identical image.
        fixture.render(flyby(timeIn(retroFrame + 40)), &rendered);
        CHECK(rendered);
        TimelineFrame again = flyby(timeIn(retroFrame, .9));
        again.kick = 1;
        again.energy = 1;
        again.beatPhase = .5f;
        CHECK(fixture.render(again, &rendered) == image);
        CHECK(rendered);
    }

    // A second stage, fresh, renders the same retro frame identically.
    Fixture other(ctx);
    const std::uint32_t retroFrame = frameAt(.5);
    CHECK(other.render(flyby(timeIn(retroFrame))) == fixture.render(flyby(timeIn(retroFrame, .3))));
}

} // namespace

int main() {
    try {
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testPalette();
        testScene();
        testShading();
        testShipFaces();
        testPainterOrder();
        testCentrePlane();
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
    std::printf("flyby_test: %d checks passed\n", checksPassed);
    return 0;
}
