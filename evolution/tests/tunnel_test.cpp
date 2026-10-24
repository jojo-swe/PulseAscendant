// TUNNEL, chapter 4: the registry entry, the palette, the path and the rings on the CPU (every ring centre
// where the analytic path puts it, C(0) = 0 and C'(0) = 0, every dot where the documented formula puts it and
// at the tunnel's radius in view space, the nearest ring the largest), the solid tube's faces (under the
// triangle limit with margin on every retro frame, every vertex on its ring, neighbouring bands sharing
// vertices exactly, culling only hidden faces, in a correct painter's order against a ray-cast reference),
// and the 320x200 palette index image from the retro stage on the GPU: deterministic per retro frame, with
// many ramp shades and a sensible coverage in both variants. Runs with the validation layer on and fails on
// any message it reports.

#include "gfx/context.hpp"
#include "ray_cast.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "retro/tunnel.hpp"
#include "retro_stage_fixture.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::Palette;
using evo::retro::RetroScene;
using evo::retro::TunnelFace;
using evo::retro::TunnelRing;
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

constexpr std::uint32_t kWidth = RetroStageFixture::kWidth, kHeight = RetroStageFixture::kHeight;
constexpr std::size_t kPixels = std::size_t{kWidth} * kHeight;
constexpr int kTunnelChapter = 4;
constexpr std::uint32_t kRate = evo::retro::kTunnelRetroRate;
constexpr double kTau = 2 * std::numbers::pi;

// A TUNNEL frame at `local` seconds into the chapter.
TimelineFrame tunnel(double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(kTunnelChapter) + local);
    if (frame.chapter != kTunnelChapter || frame.effect != Effect::Tunnel)
        throw std::runtime_error("expected a TUNNEL frame at local time " + std::to_string(local));
    return frame;
}

double duration() {
    return tunnel(0).chapterDuration;
}

// The number of retro frames the chapter shows (the last one partial).
std::uint32_t frameCount() {
    return static_cast<std::uint32_t>(std::ceil(duration() * kRate));
}

// The chapter-local time a fraction `within` of the way through a retro frame (just inside the chapter for
// the last, partial one).
double timeIn(std::uint32_t retroFrame, double within = .5) {
    return std::min((retroFrame + within) / kRate, duration() - 1e-6);
}

TimelineFrame frameOf(std::uint32_t retroFrame, double within = .5) {
    return tunnel(timeIn(retroFrame, within));
}

RetroScene sceneAt(std::uint32_t retroFrame) {
    RetroScene scene;
    evo::retro::tunnelScene(scene, frameOf(retroFrame), retroFrame);
    return scene;
}

bool isDot(std::uint8_t index) {
    return index >= evo::retro::kTunnelDotRamp &&
           index < evo::retro::kTunnelDotRamp + evo::retro::kTunnelDotShades;
}
bool isTube(std::uint8_t index) {
    return index >= evo::retro::kTunnelTubeA &&
           index < evo::retro::kTunnelTubeB + evo::retro::kTunnelTubeShades;
}

int brightness(std::uint32_t entry) {
    return static_cast<int>((entry & 0xff) + (entry >> 8 & 0xff) + (entry >> 16 & 0xff));
}

// --- The analytic tunnel, written out from the header's formulas ------------------------------------------

std::array<double, 2> path(double w) {
    std::array<double, 2> p{0, 0};
    for (const evo::retro::TunnelWave& wave : evo::retro::kTunnelPathX)
        p[0] += wave.amplitude * std::sin(wave.frequency * w + wave.phase);
    for (const evo::retro::TunnelWave& wave : evo::retro::kTunnelPathY)
        p[1] += wave.amplitude * std::sin(wave.frequency * w + wave.phase);
    return p;
}

std::array<double, 2> slope(double w) {
    std::array<double, 2> p{0, 0};
    for (const evo::retro::TunnelWave& wave : evo::retro::kTunnelPathX)
        p[0] += wave.amplitude * wave.frequency * std::cos(wave.frequency * w + wave.phase);
    for (const evo::retro::TunnelWave& wave : evo::retro::kTunnelPathY)
        p[1] += wave.amplitude * wave.frequency * std::cos(wave.frequency * w + wave.phase);
    return p;
}

double travel(std::uint32_t retroFrame) {
    return evo::retro::kTunnelSpeed * retroFrame / kRate;
}

std::array<double, 2> centre(double s, double z) {
    const std::array<double, 2> distant = path(s + z), here = path(s), tangent = slope(s);
    return {distant[0] - here[0] - z * tangent[0], distant[1] - here[1] - z * tangent[1]};
}

double twist(std::uint32_t retroFrame, double w) {
    return evo::retro::kTunnelTwistRate * retroFrame / kRate + evo::retro::kTunnelTwistPerDepth * w;
}

// A dot as the scene should hold it: its projection, index and size, and whether its square is on screen.
struct Dot {
    double x, y; // pixels
    std::uint8_t index, size;
    bool visible;
};

Dot expectedDot(const TunnelRing& ring, std::size_t i, std::uint32_t retroFrame) {
    const double a = kTau * static_cast<double>(i) / evo::retro::kTunnelRingDots + twist(retroFrame, ring.w);
    const double world[3] = {ring.centre[0] + evo::retro::kTunnelRadius * std::cos(a),
                             ring.centre[1] + evo::retro::kTunnelRadius * std::sin(a), ring.z};
    const evo::retro::Projected p = evo::retro::project(evo::retro::tunnelCamera(), world);
    Dot dot{};
    dot.x = p.x, dot.y = p.y;
    const double fade = std::clamp(
        (evo::retro::kTunnelFar - ring.z) / (evo::retro::kTunnelFar - evo::retro::kTunnelNear), 0.0, 1.0);
    dot.index = static_cast<std::uint8_t>(
        evo::retro::kTunnelDotRamp + std::lround((evo::retro::kTunnelDotShades - 1) * std::pow(fade, 1.5)));
    dot.size = ring.z < evo::retro::kTunnelDotsLarge ? 3 : ring.z < evo::retro::kTunnelDotsMedium ? 2 : 1;
    // The square's first pixel as points.comp computes it.
    const auto [firstX, firstY] =
        evo::retro::pointCorner({evo::retro::toFixed(p.x), evo::retro::toFixed(p.y), 0, dot.size});
    dot.visible = !(firstX + dot.size <= 0 || firstX >= static_cast<std::int32_t>(kWidth) ||
                    firstY + dot.size <= 0 || firstY >= static_cast<std::int32_t>(kHeight));
    return dot;
}

// --- CPU tests --------------------------------------------------------------------------------------------

void testRegistry() {
    const evo::retro::EffectInfo& info = evo::retro::effectFor(Effect::Tunnel);
    CHECK(info.shader == nullptr); // a scene on black
    CHECK(info.background == nullptr);
    CHECK(info.text == nullptr);
    CHECK(info.retroRate == 35 && kRate == 35);
    CHECK(info.palette == &evo::retro::tunnelPalette);
    CHECK(info.scene == &evo::retro::tunnelScene);
}

// Black at 0, white at 255, the dot ramp and the two tube ramps brightening entry by entry from near black,
// the two tube ramps of different hues, the rest black; the same at any frame.
void testPalette() {
    Palette palette{};
    palette.fill(0x12345678u);
    evo::retro::tunnelPalette(palette, tunnel(1), 10);
    CHECK(palette[0] == evo::retro::rgba(0, 0, 0));
    CHECK(palette[255] == evo::retro::rgba(255, 255, 255));
    for (std::uint32_t entry : palette)
        CHECK((entry >> 24) == 0xff);
    const auto rising = [&](std::size_t first, std::size_t count) {
        for (std::size_t i = first; i + 1 < first + count; ++i)
            CHECK(brightness(palette[i + 1]) > brightness(palette[i]));
        CHECK(brightness(palette[first]) < 3 * 40); // fades into the dark
        CHECK(brightness(palette[first + count - 1]) > 3 * 180);
    };
    using evo::retro::kTunnelDotRamp, evo::retro::kTunnelDotShades;
    using evo::retro::kTunnelTubeA, evo::retro::kTunnelTubeB, evo::retro::kTunnelTubeShades;
    rising(kTunnelDotRamp, kTunnelDotShades);
    rising(kTunnelTubeA, kTunnelTubeShades);
    rising(kTunnelTubeB, kTunnelTubeShades);
    // A blue, B violet: B has more red than A at the same shade.
    for (std::size_t i = 8; i < kTunnelTubeShades; ++i)
        CHECK((palette[kTunnelTubeB + i] & 0xff) > (palette[kTunnelTubeA + i] & 0xff));
    static_assert(kTunnelDotRamp + kTunnelDotShades == kTunnelTubeA &&
                  kTunnelTubeA + kTunnelTubeShades == kTunnelTubeB);
    for (std::size_t i = 1; i < 255; ++i)
        if (i < kTunnelDotRamp || i >= kTunnelTubeB + kTunnelTubeShades)
            CHECK(palette[i] == evo::retro::rgba(0, 0, 0));
    Palette later{};
    evo::retro::tunnelPalette(later, tunnel(15), 600);
    CHECK(later == palette);
}

// The path, the travel, the twist and the rings, against the header's formulas.
void testRings() {
    for (double w : {0.0, 1.25, 7.5, 33.3, 61.0}) {
        CHECK(std::abs(evo::retro::tunnelPath(w)[0] - path(w)[0]) < 1e-12);
        CHECK(std::abs(evo::retro::tunnelPath(w)[1] - path(w)[1]) < 1e-12);
        CHECK(std::abs(evo::retro::tunnelPathSlope(w)[0] - slope(w)[0]) < 1e-12);
        CHECK(std::abs(evo::retro::tunnelPathSlope(w)[1] - slope(w)[1]) < 1e-12);
        // The slope is the path's derivative.
        const double h = 1e-5;
        CHECK(std::abs((path(w + h)[0] - path(w - h)[0]) / (2 * h) - slope(w)[0]) < 1e-6);
        CHECK(std::abs((path(w + h)[1] - path(w - h)[1]) / (2 * h) - slope(w)[1]) < 1e-6);
    }
    // The camera rides the path along its tangent: whatever the travel, the ring centre at depth 0 is on the
    // view axis and the tunnel leaves it with zero slope, C(0) = 0 and C'(0) = 0.
    for (double s : {0.0, 3.7, 12.25, 30.0, 47.9}) {
        const std::array<double, 2> c0 = evo::retro::tunnelRingCentre(s, 0);
        CHECK(std::abs(c0[0]) < 1e-12 && std::abs(c0[1]) < 1e-12);
        const double h = 1e-4;
        const std::array<double, 2> ahead = evo::retro::tunnelRingCentre(s, h);
        const std::array<double, 2> behind = evo::retro::tunnelRingCentre(s, -h);
        CHECK(std::abs(ahead[0] - behind[0]) / (2 * h) < 1e-6 &&
              std::abs(ahead[1] - behind[1]) / (2 * h) < 1e-6);
        // ... and curves away beyond it (the path bends), so the test above is not vacuous.
        const std::array<double, 2> distant = evo::retro::tunnelRingCentre(s, 20);
        CHECK(std::hypot(distant[0], distant[1]) > 0.5);
    }
    for (std::uint32_t retroFrame = 0; retroFrame < frameCount(); ++retroFrame) {
        CHECK(evo::retro::tunnelTravel(retroFrame) == travel(retroFrame));
        CHECK(std::abs(evo::retro::tunnelTwist(retroFrame, 3.5) - twist(retroFrame, 3.5)) < 1e-12);
        const std::vector<TunnelRing> rings = evo::retro::tunnelRings(retroFrame);
        CHECK(rings.size() == evo::retro::kTunnelRings);
        const double s = travel(retroFrame);
        for (std::size_t k = 0; k < rings.size(); ++k) {
            const TunnelRing& ring = rings[k];
            CHECK(ring.z > evo::retro::kTunnelNear && ring.z <= evo::retro::kTunnelFar + 1e-9);
            CHECK(std::abs(ring.w - s - ring.z) < 1e-9);
            // On the world's ring grid, and far to near one spacing apart.
            CHECK(std::abs(ring.w / .25 - std::round(ring.w / .25)) < 1e-9);
            if (k > 0)
                CHECK(std::abs(rings[k - 1].z - ring.z - .25) < 1e-9);
            const std::array<double, 2> c = centre(s, ring.z);
            CHECK(std::abs(ring.centre[0] - c[0]) < 1e-9 && std::abs(ring.centre[1] - c[1]) < 1e-9);
        }
    }
    // The tube starts halfway through the chapter.
    CHECK(evo::retro::tunnelTubeFrame(tunnel(1)) ==
          static_cast<std::uint32_t>(std::floor(duration() * kRate / 2)));
}

// The dots of every retro frame before the tube: the 96 x 64 dots of the formula, each in the scene within
// one pixel, in its ramp shade and size, unless its square lies wholly off the screen; each drawn dot,
// unprojected at its ring's depth, at the tunnel's radius from the ring centre in view space; the ring
// centres on the projected path within one pixel; the rings shrinking with depth, the nearest the largest.
void testDots() {
    const double focalY = 100.0 / std::tan(evo::retro::kTunnelFovY * std::numbers::pi / 360.0);
    const double focalX = focalY * evo::retro::kPixelAspect;
    double worstRadius = 0;
    const std::uint32_t tubeFrame = evo::retro::tunnelTubeFrame(tunnel(1));
    std::size_t fewest = SIZE_MAX, most = 0, ringsChecked = 0;
    double worstCentre = 0;
    for (std::uint32_t retroFrame = 0; retroFrame < tubeFrame; ++retroFrame) {
        const RetroScene scene = sceneAt(retroFrame);
        CHECK(scene.triangles.empty());
        const std::vector<TunnelRing> rings = evo::retro::tunnelRings(retroFrame);
        const evo::retro::Camera camera = evo::retro::tunnelCamera();
        std::size_t next = 0, total = 0;
        double previousExtent = 0;
        bool partialSeen = false;
        for (const TunnelRing& ring : rings) {
            double sumX = 0, sumY = 0, minX = 1e9, maxX = -1e9;
            std::size_t visible = 0;
            for (std::size_t i = 0; i < evo::retro::kTunnelRingDots; ++i) {
                const Dot dot = expectedDot(ring, i, retroFrame);
                ++total;
                if (!dot.visible)
                    continue;
                CHECK(next < scene.points.size());
                const evo::retro::RetroPoint& p = scene.points[next++];
                CHECK(std::abs(p.x - dot.x * 16) <= 16 && std::abs(p.y - dot.y * 16) <= 16);
                CHECK(p.index == dot.index && p.size == dot.size);
                // Back into view space at the ring's depth: on the ring's circle, to the rounding of 28.4.
                const double viewX = (p.x / 16.0 - 160) * ring.z / focalX;
                const double viewY = (100 - p.y / 16.0) * ring.z / focalY;
                const double radius = std::hypot(viewX - ring.centre[0], viewY - ring.centre[1]);
                const double error = std::abs(radius - evo::retro::kTunnelRadius) / (ring.z / focalX);
                worstRadius = std::max(worstRadius, error);
                CHECK(error <= 1.0 / 16); // in pixels: half a 1/16 pixel each way
                sumX += p.x / 16.0, sumY += p.y / 16.0;
                minX = std::min(minX, p.x / 16.0), maxX = std::max(maxX, p.x / 16.0);
                ++visible;
            }
            if (visible == evo::retro::kTunnelRingDots) {
                // A whole ring: its dots' mean is its centre (all at one depth), on the projected path.
                const double world[3] = {ring.centre[0], ring.centre[1], ring.z};
                const evo::retro::Projected c = evo::retro::project(camera, world);
                const double dots = evo::retro::kTunnelRingDots;
                const double error = std::hypot(sumX / dots - c.x, sumY / dots - c.y);
                worstCentre = std::max(worstCentre, error);
                CHECK(error <= 1);
                // Far to near, each whole ring wider than the last; partly hidden rings only nearer.
                CHECK(!partialSeen);
                CHECK(maxX - minX > previousExtent);
                previousExtent = maxX - minX;
                ++ringsChecked;
            } else if (visible > 0 || previousExtent > 0) {
                partialSeen = true;
            }
        }
        CHECK(total == evo::retro::kTunnelRings * evo::retro::kTunnelRingDots);
        CHECK(next == scene.points.size()); // nothing else drawn
        fewest = std::min(fewest, scene.points.size());
        most = std::max(most, scene.points.size());
    }
    std::printf(
        "tunnel: dots %zu..%zu of %zu on screen before the tube; %zu whole rings within %.3f px of the "
        "path; dots within %.4f px of the ring radius\n",
        fewest, most, evo::retro::kTunnelRings * evo::retro::kTunnelRingDots, ringsChecked, worstCentre,
        worstRadius);
    CHECK(fewest >= 5900); // measured 5946..6015
}

// The tube from its first frame to the end: at most kTubeBudget triangles on every retro frame (the
// rasteriser takes 2048), flat in the tube's ramps, the faces exactly what the scene draws; the dots give way
// as the front sweeps in and are gone after it.
constexpr std::size_t kTubeBudget = 1000; // measured 927

void testTube() {
    const TimelineFrame first = tunnel(1);
    const std::uint32_t tubeFrame = evo::retro::tunnelTubeFrame(first);
    const auto sweepEnd =
        tubeFrame + static_cast<std::uint32_t>(std::ceil(evo::retro::kTunnelTubeSweepSeconds * kRate));
    std::size_t most = 0, fewest = SIZE_MAX;
    std::set<std::uint8_t> indices;
    double lastFront = 1e9;
    for (std::uint32_t retroFrame = tubeFrame; retroFrame < frameCount(); ++retroFrame) {
        const TimelineFrame frame = frameOf(retroFrame);
        const double front = evo::retro::tunnelTubeFront(frame, retroFrame);
        CHECK(front <= lastFront); // the front only comes nearer
        lastFront = front;
        const RetroScene scene = sceneAt(retroFrame);
        most = std::max(most, scene.triangles.size());
        if (retroFrame >= sweepEnd) {
            CHECK(front == evo::retro::kTunnelTubeNear);
            CHECK(scene.points.empty());
            fewest = std::min(fewest, scene.triangles.size());
        }
        for (const evo::retro::RetroTriangle& t : scene.triangles) {
            CHECK(t.mode == evo::retro::kFlat && isTube(t.index[0]));
            indices.insert(t.index[0]);
        }
        for (const evo::retro::RetroPoint& p : scene.points)
            CHECK(isDot(p.index));

        // The scene's triangles are the faces, in order, as addTriangle makes them.
        RetroScene fromFaces;
        for (const TunnelFace& face : evo::retro::tunnelTubeFaces(frame, retroFrame)) {
            const std::uint8_t three[3] = {face.index, face.index, face.index};
            evo::retro::addTriangle(fromFaces, evo::retro::tunnelCamera(), face.world, three,
                                    evo::retro::kFlat);
        }
        CHECK(fromFaces.triangles.size() == scene.triangles.size());
        for (std::size_t i = 0; i < scene.triangles.size(); ++i)
            CHECK(std::equal(std::begin(scene.triangles[i].x), std::end(scene.triangles[i].x),
                             std::begin(fromFaces.triangles[i].x)) &&
                  std::equal(std::begin(scene.triangles[i].y), std::end(scene.triangles[i].y),
                             std::begin(fromFaces.triangles[i].y)) &&
                  scene.triangles[i].index[0] == fromFaces.triangles[i].index[0]);
    }
    CHECK(evo::retro::tunnelTubeFront(first, tubeFrame - 1) >= evo::retro::kTunnelFar);
    std::printf(
        "tunnel: the tube draws %zu..%zu triangles a frame once whole (the budget %zu, the limit %zu), "
        "%zu indices\n",
        fewest, most, kTubeBudget, evo::retro::kMaxTriangles, indices.size());
    CHECK(most <= kTubeBudget);
    CHECK(fewest >= 760);        // measured 780
    CHECK(indices.size() >= 30); // two ramps, many shades
}

// The tube's painter's order on the CPU: for every 13th retro frame of the whole tube, a ray through each
// pixel centre of an 80x50 grid against its faces (view space, camera at the origin). The face drawn last
// among those it hits must show the nearest one's index.
void testTubeOrder() {
    const std::uint32_t tubeFrame = evo::retro::tunnelTubeFrame(tunnel(1));
    const auto whole =
        tubeFrame + static_cast<std::uint32_t>(std::ceil(evo::retro::kTunnelTubeSweepSeconds * kRate));
    std::size_t rays = 0, wrong = 0;
    for (std::uint32_t retroFrame = tubeFrame + 3; retroFrame < frameCount(); retroFrame += 13) {
        const std::vector<TunnelFace> faces = evo::retro::tunnelTubeFaces(frameOf(retroFrame), retroFrame);
        std::size_t covered = 0;
        for (int gy = 0; gy < 50; ++gy)
            for (int gx = 0; gx < 80; ++gx) {
                const evo::test::Ray ray =
                    evo::test::rayThrough(evo::retro::tunnelCamera(), gx * 4 + 1.5, gy * 4 + 1.5);
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
                ++rays, ++covered;
                if (lastT > nearestT * (1 + 1e-9) && faces[static_cast<std::size_t>(last)].index !=
                                                         faces[static_cast<std::size_t>(nearest)].index)
                    ++wrong;
            }
        // Once whole, the tube fills the frame but for its dark far end.
        if (retroFrame >= whole)
            CHECK(covered >= 80 * 50 * 9 / 10);
    }
    std::printf("tunnel: tube painter's order: %zu rays hit the tube, %zu wrong\n", rays, wrong);
    CHECK(rays >= 20000);
    CHECK(wrong == 0);
}

// The tube's order during the sweep, where its outside shows: on every retro frame of the sweep, a ray
// through every 2nd pixel (a 160x100 grid). A band is convex, so a ray that crosses both its outside and its
// inside meets the outside first: drawing a band's outer faces after its inner ones (tunnel.cpp) is what
// keeps the outside in front. At most 2 rays may show a face behind the nearest (measured: 1; drawing each
// band's outer faces before its inner ones gives 11, which testTubeOrder's sparser grid does not see).
void testTubeSweepOrder() {
    const std::uint32_t tubeFrame = evo::retro::tunnelTubeFrame(tunnel(1));
    const auto whole =
        tubeFrame + static_cast<std::uint32_t>(std::ceil(evo::retro::kTunnelTubeSweepSeconds * kRate));
    constexpr int kGridX = 160, kGridY = 100;
    std::vector<evo::test::Ray> rays;
    for (int gy = 0; gy < kGridY; ++gy)
        for (int gx = 0; gx < kGridX; ++gx)
            rays.push_back(evo::test::rayThrough(evo::retro::tunnelCamera(), gx * 2 + .5, gy * 2 + .5));
    std::size_t hits = 0, wrong = 0;
    for (std::uint32_t retroFrame = tubeFrame; retroFrame < whole; ++retroFrame) {
        const std::vector<TunnelFace> faces = evo::retro::tunnelTubeFaces(frameOf(retroFrame), retroFrame);
        std::vector<double> nearestT(rays.size(), 1e30), lastT(rays.size(), 0);
        std::vector<int> nearest(rays.size(), -1), last(rays.size(), -1);
        for (std::size_t i = 0; i < faces.size(); ++i) {
            // Only the rays within the face's projected box (every vertex is in front of the camera).
            double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
            for (const auto& v : faces[i].world) {
                const evo::retro::Projected p = evo::retro::project(evo::retro::tunnelCamera(), v);
                CHECK(p.z > 0);
                minX = std::min(minX, p.x), maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
            }
            const int gx0 = std::max(0, static_cast<int>(std::floor((minX - .5) / 2)) - 1);
            const int gx1 = std::min(kGridX - 1, static_cast<int>(std::ceil((maxX - .5) / 2)) + 1);
            const int gy0 = std::max(0, static_cast<int>(std::floor((minY - .5) / 2)) - 1);
            const int gy1 = std::min(kGridY - 1, static_cast<int>(std::ceil((maxY - .5) / 2)) + 1);
            for (int gy = gy0; gy <= gy1; ++gy)
                for (int gx = gx0; gx <= gx1; ++gx) {
                    const auto r = static_cast<std::size_t>(gy * kGridX + gx);
                    const double t = evo::test::hit(rays[r], faces[i].world);
                    if (t <= 0)
                        continue;
                    if (t < nearestT[r])
                        nearest[r] = static_cast<int>(i), nearestT[r] = t;
                    last[r] = static_cast<int>(i), lastT[r] = t;
                }
        }
        for (std::size_t r = 0; r < rays.size(); ++r) {
            if (last[r] < 0)
                continue;
            ++hits;
            if (lastT[r] > nearestT[r] * (1 + 1e-9) && faces[static_cast<std::size_t>(last[r])].index !=
                                                           faces[static_cast<std::size_t>(nearest[r])].index)
                ++wrong;
        }
    }
    std::printf("tunnel: tube order over the sweep (frames %u..%u): %zu rays hit the tube, %zu wrong\n",
                tubeFrame, whole - 1, hits, wrong);
    CHECK(hits >= 100000); // measured 124563
    CHECK(wrong <= 2);
}

// The nearest hit along a ray beyond the near plane among the faces, or infinity.
double nearestHit(const evo::test::Ray& ray, const std::vector<TunnelFace>& faces) {
    double nearest = std::numeric_limits<double>::infinity();
    for (const TunnelFace& face : faces) {
        const double t = evo::test::hit(ray, face.world);
        if (t > 0 && t * ray.direction[2] >= evo::retro::kNearPlane)
            nearest = std::min(nearest, t);
    }
    return nearest;
}

// The tube's geometry and culling, on every 9th retro frame of the tube (the sweep included), from all its
// faces before culling: every vertex at the tube's radius from its ring's centre and on the tube rings'
// grid; neighbouring bands sharing their ring's 16 vertices bit for bit; and, on a 40x25 grid of rays, the
// nearest culled face where the nearest of all faces is (culling only drops faces something nearer hides).
void testTubeCulling() {
    const std::uint32_t tubeFrame = evo::retro::tunnelTubeFrame(tunnel(1));
    std::size_t frames = 0, rays = 0, culled = 0, total = 0;
    double worstRadius = 0;
    for (std::uint32_t retroFrame = tubeFrame; retroFrame < frameCount(); retroFrame += 9) {
        const TimelineFrame frame = frameOf(retroFrame);
        const std::vector<TunnelFace> all = evo::retro::tunnelTubeFaces(frame, retroFrame, false);
        const std::vector<TunnelFace> drawn = evo::retro::tunnelTubeFaces(frame, retroFrame);
        if (all.empty())
            continue;
        ++frames;
        CHECK(all.size() % (2 * evo::retro::kTunnelSegments) == 0 && drawn.size() <= all.size());
        culled += all.size() - drawn.size();
        total += all.size();

        // The vertices, ring by ring (grouped by their exact depth).
        const double s = travel(retroFrame);
        std::map<double, std::set<std::array<double, 3>>> rings;
        for (const TunnelFace& face : all)
            for (const auto& v : face.world) {
                const std::array<double, 2> c = centre(s, v[2]);
                const double radius = std::hypot(v[0] - c[0], v[1] - c[1]);
                worstRadius = std::max(worstRadius, std::abs(radius - evo::retro::kTunnelRadius));
                CHECK(std::abs(radius - evo::retro::kTunnelRadius) < 1e-9);
                const double m = (v[2] + s) / evo::retro::kTunnelBandLength;
                CHECK(std::abs(m - std::round(m)) < 1e-9);
                rings[v[2]].insert({v[0], v[1], v[2]});
            }
        // Bands + 1 rings of exactly 16 vertices: where two bands meet they use the same 16 points.
        const std::size_t bands = all.size() / (2 * evo::retro::kTunnelSegments);
        CHECK(rings.size() == bands + 1);
        for (const auto& [z, vertices] : rings)
            CHECK(vertices.size() == evo::retro::kTunnelSegments);

        for (int gy = 0; gy < 25; ++gy)
            for (int gx = 0; gx < 40; ++gx) {
                const evo::test::Ray ray =
                    evo::test::rayThrough(evo::retro::tunnelCamera(), gx * 8 + 3.5, gy * 8 + 3.5);
                const double nearest = nearestHit(ray, all);
                if (std::isinf(nearest))
                    continue;
                ++rays;
                CHECK(std::abs(nearestHit(ray, drawn) - nearest) <= 1e-9 * nearest);
            }
    }
    std::printf("tunnel: tube culling: %zu frames, %zu rays, %zu of %zu faces culled; vertices within %.1e "
                "of the radius\n",
                frames, rays, culled, total, worstRadius);
    CHECK(frames >= 20 && rays >= 10000);
    CHECK(culled * 4 >= total); // a good share is culled, or the test above proves little
}

// --- GPU --------------------------------------------------------------------------------------------------

// The dot frame where the tunnel's far end swings furthest from the centre (a bend).
std::uint32_t curveFrame() {
    const std::uint32_t tubeFrame = evo::retro::tunnelTubeFrame(tunnel(1));
    std::uint32_t best = 0;
    double widest = 0;
    for (std::uint32_t retroFrame = 0; retroFrame < tubeFrame; ++retroFrame) {
        const TunnelRing farthest = evo::retro::tunnelRings(retroFrame).front();
        const double offset = std::hypot(farthest.centre[0], farthest.centre[1]) / farthest.z;
        if (offset > widest)
            widest = offset, best = retroFrame;
    }
    return best;
}

void testImages(evo::gfx::Context& ctx) {
    RetroStageFixture fixture(ctx);
    const std::uint32_t tubeFrame = evo::retro::tunnelTubeFrame(tunnel(1));
    const std::uint32_t frames[] = {2 * kRate, curveFrame(), frameCount() - 3 * kRate};
    std::printf("tunnel: the curve at retro frame %u (%.3f s into the chapter)\n", frames[1],
                timeIn(frames[1]));
    for (std::uint32_t retroFrame : frames) {
        const bool tube = retroFrame >= tubeFrame;
        bool rendered = false;
        const std::vector<std::uint8_t> image = fixture.render(frameOf(retroFrame, .1), &rendered);
        CHECK(rendered && fixture.stage().retroFrame() == retroFrame);
        std::set<std::uint8_t> shades;
        std::size_t lit = 0;
        for (std::uint8_t index : image) {
            CHECK(index == 0 || (tube ? isTube(index) : isDot(index)));
            if (index != 0) {
                shades.insert(index);
                ++lit;
            }
        }
        const double share = static_cast<double>(lit) / kPixels;
        std::printf("tunnel: retro frame %u (%s): %.1f %% of the frame lit in %zu indices\n", retroFrame,
                    tube ? "tube" : "dots", 100 * share, shades.size());
        if (tube) {
            CHECK(share >= .9);
            CHECK(shades.size() >= 24);
        } else {
            CHECK(share >= .03 && share <= .35);
            CHECK(shades.size() >= 40);
        }

        // Deterministic: the same retro frame at another moment within it, with the music signals pushed,
        // after another frame, gives the identical image.
        fixture.render(frameOf(retroFrame > 40 ? retroFrame - 40 : retroFrame + 40), &rendered);
        CHECK(rendered);
        TimelineFrame again = frameOf(retroFrame, .9);
        again.kick = 1;
        again.energy = 1;
        again.beatPhase = .5f;
        CHECK(fixture.render(again, &rendered) == image);
        CHECK(rendered);
    }
    // A second stage, fresh, renders the same retro frames identically.
    RetroStageFixture other(ctx);
    for (std::uint32_t retroFrame : {frames[1], frames[2]})
        CHECK(other.render(frameOf(retroFrame)) == fixture.render(frameOf(retroFrame, .3)));
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testPalette();
        testRings();
        testDots();
        testTube();
        testTubeOrder();
        testTubeSweepOrder();
        testTubeCulling();
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
    std::printf("tunnel_test: %d checks passed\n", checksPassed);
    return 0;
}
