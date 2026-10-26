// The 1993 rasterisers: the CPU scene functions (projection, near-plane and guard-band clipping,
// quantisation, painter's sort) and the GPU passes (shaders/retro/polys.comp and points.comp through
// render::SceneRaster and the retro stage) against a CPU reference rasteriser written here. Pins shared edges (no cracks, no pixel drawn twice, under rotation) and primitives far off
// screen or behind the camera. Runs with the validation layer on and fails on any message it reports.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/sync.hpp"
#include "render/retro_stage.hpp"
#include "render/scene_raster.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/cues.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"
#include "text/text_pass.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::Camera;
using evo::retro::RetroPoint;
using evo::retro::RetroScene;
using evo::retro::RetroTriangle;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr int kWidth = evo::retro::kScreenWidth, kHeight = evo::retro::kScreenHeight;
constexpr std::size_t kPixels = std::size_t{kWidth} * kHeight;
using Pixels = std::vector<std::uint8_t>; // a 320x200 index image, row by row

template <typename Exception, typename F> bool throws(F&& f) {
    try {
        f();
    } catch (const Exception&) {
        return true;
    }
    return false;
}

// --- The CPU reference rasteriser ------------------------------------------------------------------------

// The 4x4 Bayer thresholds of shaders/retro/common.glsl.
int bayer4(int x, int y) {
    static constexpr int kMatrix[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return kMatrix[(y & 3) * 4 + (x & 3)];
}

std::int64_t edgeFunction(std::int64_t ax, std::int64_t ay, std::int64_t bx, std::int64_t by, std::int64_t px,
                          std::int64_t py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

bool topLeft(std::int64_t ax, std::int64_t ay, std::int64_t bx, std::int64_t by) {
    const std::int64_t dy = by - ay;
    return dy < 0 || (dy == 0 && bx > ax);
}

bool coveredBy(std::int64_t e, bool owns) {
    return e > 0 || (e == 0 && owns);
}

// One triangle drawn the way polys.comp documents it, in 64-bit integers (so it also takes triangles beyond
// the guard band, which the GPU never sees).
void referenceTriangle(Pixels& image, const RetroTriangle& t) {
    std::int64_t x[3] = {t.x[0], t.x[1], t.x[2]}, y[3] = {t.y[0], t.y[1], t.y[2]};
    std::uint32_t index[3] = {t.index[0], t.index[1], t.index[2]};
    std::int64_t area = edgeFunction(x[0], y[0], x[1], y[1], x[2], y[2]);
    if (area < 0) {
        std::swap(x[1], x[2]);
        std::swap(y[1], y[2]);
        std::swap(index[1], index[2]);
        area = -area;
    }
    if (area == 0)
        return;
    const int shift =
        std::max(0, static_cast<int>(std::bit_width(static_cast<std::uint64_t>(area))) - 1 - 17);
    for (int j = 0; j < kHeight; ++j) {
        for (int i = 0; i < kWidth; ++i) {
            const std::int64_t px = i * 16 + 8, py = j * 16 + 8;
            const std::int64_t w0 = edgeFunction(x[1], y[1], x[2], y[2], px, py);
            const std::int64_t w1 = edgeFunction(x[2], y[2], x[0], y[0], px, py);
            const std::int64_t w2 = edgeFunction(x[0], y[0], x[1], y[1], px, py);
            if (!coveredBy(w0, topLeft(x[1], y[1], x[2], y[2])) ||
                !coveredBy(w1, topLeft(x[2], y[2], x[0], y[0])) ||
                !coveredBy(w2, topLeft(x[0], y[0], x[1], y[1])))
                continue;
            std::uint8_t& pixel = image[static_cast<std::size_t>(j) * kWidth + i];
            if (t.mode == evo::retro::kGouraud) {
                const std::uint64_t u0 = static_cast<std::uint64_t>(w0) >> shift;
                const std::uint64_t u1 = static_cast<std::uint64_t>(w1) >> shift;
                const std::uint64_t u2 = static_cast<std::uint64_t>(w2) >> shift;
                const std::uint64_t total = u0 + u1 + u2;
                const std::uint64_t weighted = u0 * index[0] + u1 * index[1] + u2 * index[2];
                const std::uint64_t dithered =
                    (32 * weighted + (2 * static_cast<std::uint64_t>(bayer4(i, j)) + 1) * total) /
                    (32 * total);
                pixel = static_cast<std::uint8_t>(dithered);
            } else if (t.mode == evo::retro::kGlenz) {
                pixel |= static_cast<std::uint8_t>(index[0]);
            } else {
                pixel = static_cast<std::uint8_t>(index[0]);
            }
        }
    }
}

void referencePoints(Pixels& image, const std::vector<RetroPoint>& points) {
    for (const RetroPoint& point : points) {
        const int size = point.size;
        // floor(x / 16 - size / 2 + 1 / 2), as an arithmetic shift of the 28.4 value.
        const int firstX = (point.x - size * 8 + 8) >> 4, firstY = (point.y - size * 8 + 8) >> 4;
        for (int y = std::max(firstY, 0); y < std::min(firstY + size, kHeight); ++y)
            for (int x = std::max(firstX, 0); x < std::min(firstX + size, kWidth); ++x)
                image[static_cast<std::size_t>(y) * kWidth + x] = point.index;
    }
}

Pixels reference(const RetroScene& scene, std::uint8_t clear) {
    Pixels image(kPixels, clear);
    for (const RetroTriangle& t : scene.triangles)
        referenceTriangle(image, t);
    referencePoints(image, scene.points);
    return image;
}

std::size_t differences(const Pixels& a, const Pixels& b) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        count += a[i] != b[i];
    return count;
}

RetroTriangle screenTriangle(double ax, double ay, double bx, double by, double cx, double cy,
                             std::uint8_t mode, std::uint8_t i0, std::uint8_t i1 = 0, std::uint8_t i2 = 0) {
    using evo::retro::toFixed;
    RetroTriangle t{};
    t.x[0] = toFixed(ax), t.y[0] = toFixed(ay);
    t.x[1] = toFixed(bx), t.y[1] = toFixed(by);
    t.x[2] = toFixed(cx), t.y[2] = toFixed(cy);
    t.index[0] = i0;
    t.index[1] = mode == evo::retro::kGouraud ? i1 : i0;
    t.index[2] = mode == evo::retro::kGouraud ? i2 : i0;
    t.mode = mode;
    return t;
}

RetroPoint screenPoint(double x, double y, std::uint8_t index, std::uint8_t size) {
    RetroPoint p{};
    p.x = evo::retro::toFixed(x);
    p.y = evo::retro::toFixed(y);
    p.index = index;
    p.size = size;
    return p;
}

// Looking down +z from the origin, y up.
Camera axisCamera(double fovY) {
    return Camera{{0, 0, 0}, {0, 0, 1}, {0, 1, 0}, fovY};
}

// --- The GPU fixture ---------------------------------------------------------------------------------------

// A scene rasteriser with its own ring and index image; every draw clears the image, draws, submits, waits
// and reads it back.
class Fixture {
public:
    explicit Fixture(evo::gfx::Context& ctx)
        : ring_(ctx), raster_(ctx, ring_),
          image_(ctx, {kWidth, kHeight, VK_FORMAT_R8_UINT,
                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       1, "raster test index"}),
          readback_(ctx, {kPixels, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "raster test readback"}) {}

    // The scenes drawn one after the other in one frame, over the clear index.
    Pixels draw(const std::vector<const RetroScene*>& scenes, std::uint8_t clear) {
        VkCommandBuffer cmd = ring_.begin();
        image_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkClearColorValue value{};
        value.uint32[0] = clear;
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, image_.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
        for (const RetroScene* scene : scenes)
            raster_.draw(cmd, image_, *scene);
        image_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                          VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kWidth, kHeight, 1};
        vkCmdCopyImageToBuffer(cmd, image_.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.handle(),
                               1, &region);
        evo::gfx::bufferBarrier2(cmd, readback_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                 VK_ACCESS_2_HOST_READ_BIT);
        ring_.wait(ring_.submit());
        readback_.invalidate();
        const auto* data = static_cast<const std::uint8_t*>(readback_.mapped());
        return {data, data + kPixels};
    }

    Pixels draw(const RetroScene& scene, std::uint8_t clear = 0) { return draw({&scene}, clear); }

    evo::gfx::FrameRing& ring() { return ring_; }
    evo::render::SceneRaster& raster() { return raster_; }
    evo::gfx::Image& image() { return image_; }

private:
    // The ring is declared first and so destroyed last; ~SceneRaster waits for it before its buffers go.
    evo::gfx::FrameRing ring_;
    evo::render::SceneRaster raster_;
    evo::gfx::Image image_;
    evo::gfx::Buffer readback_;
};

// How many triangles of the scene cover each pixel: the triangles go through the GPU in batches of eight,
// each as a glenz face with its own bit over 0, and the bits of every batch are counted.
std::vector<int> coverageCounts(Fixture& fixture, const RetroScene& scene) {
    std::vector<int> counts(kPixels, 0);
    for (std::size_t first = 0; first < scene.triangles.size(); first += 8) {
        RetroScene batch;
        for (std::size_t i = first; i < std::min(first + 8, scene.triangles.size()); ++i) {
            RetroTriangle t = scene.triangles[i];
            t.mode = evo::retro::kGlenz;
            t.index[0] = t.index[1] = t.index[2] = static_cast<std::uint8_t>(1u << (i - first));
            batch.triangles.push_back(t);
        }
        const Pixels bits = fixture.draw(batch);
        for (std::size_t p = 0; p < kPixels; ++p)
            counts[p] += std::popcount(static_cast<unsigned>(bits[p]));
    }
    return counts;
}

// --- CPU: the scene functions ----------------------------------------------------------------------------

void testProjection() {
    using evo::retro::project;
    using evo::retro::toFixed;
    const Camera camera = axisCamera(90); // 100 pixels a unit up at z = 1, 120 across
    const double ahead[3] = {0, 0, 5}, right[3] = {1, 0, 1}, up[3] = {0, 1, 1}, behind[3] = {0, 0, -2};
    const auto a = project(camera, ahead);
    CHECK(a.x == 160 && a.y == 100 && a.z == 5);
    const auto r = project(camera, right);
    CHECK(std::abs(r.x - 280) < 1e-9 && std::abs(r.y - 100) < 1e-9);
    const auto u = project(camera, up);
    CHECK(std::abs(u.x - 160) < 1e-9 && std::abs(u.y - 0) < 1e-9); // y up is up the screen
    CHECK(project(camera, behind).z == -2);

    // A turned camera: looking down -x from (5, 0, 0), the origin is straight ahead at depth 5.
    const Camera turned{{5, 0, 0}, {0, 0, 0}, {0, 1, 0}, 90};
    const double origin[3] = {0, 0, 0};
    const auto o = project(turned, origin);
    CHECK(std::abs(o.x - 160) < 1e-9 && std::abs(o.y - 100) < 1e-9 && std::abs(o.z - 5) < 1e-12);

    CHECK(toFixed(2.5) == 40);
    CHECK(toFixed(-1.0) == -16);
    CHECK(toFixed(0.03125) == 1); // half a 28.4 unit rounds away from zero
    CHECK(toFixed(-0.03125) == -1);
}

void testAddTriangle() {
    using evo::retro::addTriangle;
    using evo::retro::kFlat;
    using evo::retro::kGouraud;
    using evo::retro::project;
    using evo::retro::toFixed;
    const Camera camera = axisCamera(90);
    const std::uint8_t flat[3] = {9, 9, 9};

    // In front and on screen: one triangle, the projected vertices quantised, the mean depth recorded.
    {
        RetroScene scene;
        const double world[3][3] = {{0, 0, 2}, {1, 0, 2}, {0, 1, 4}};
        addTriangle(scene, camera, world, flat, kFlat);
        CHECK(scene.triangles.size() == 1 && scene.triangleDepth.size() == 1);
        CHECK(std::abs(scene.triangleDepth[0] - 8.0 / 3) < 1e-12);
        for (int v = 0; v < 3; ++v) {
            const auto p = project(camera, world[v]);
            CHECK(scene.triangles[0].x[v] == toFixed(p.x) && scene.triangles[0].y[v] == toFixed(p.y));
        }
        CHECK(scene.triangles[0].mode == kFlat && scene.triangles[0].index[2] == 9);
    }
    // Wholly behind the near plane, wholly off screen, or of no area once quantised: nothing.
    {
        RetroScene scene;
        const double behind[3][3] = {{0, 0, 0.05}, {1, 0, -1}, {0, 1, 0.09}};
        const double offRight[3][3] = {{10, 0, 2}, {12, 0, 2}, {11, 1, 2}};
        const double tiny[3][3] = {{0, 0, 2}, {1e-6, 0, 2}, {0, 1e-6, 2}};
        addTriangle(scene, camera, behind, flat, kFlat);
        addTriangle(scene, camera, offRight, flat, kFlat);
        addTriangle(scene, camera, tiny, flat, kFlat);
        CHECK(scene.triangles.empty() && scene.triangleDepth.empty());
    }
    // Across the near plane: one vertex behind leaves a quad (two triangles), two behind a triangle, each
    // reaching down to z = 0.1 and no further; every piece keeps the original's mean depth.
    {
        RetroScene scene;
        const double oneBehind[3][3] = {{-0.2, -0.2, 1}, {0.2, -0.2, 1}, {0, 1.0, -1}};
        addTriangle(scene, camera, oneBehind, flat, kFlat);
        CHECK(scene.triangles.size() == 2);
        const double twoBehind[3][3] = {{-0.2, -0.2, 1}, {0.2, -0.2, -1}, {0, 0.2, -1}};
        addTriangle(scene, camera, twoBehind, flat, kFlat);
        CHECK(scene.triangles.size() == 3);
        CHECK(scene.triangleDepth == (std::vector<double>{1.0 / 3, 1.0 / 3, -1.0 / 3}));
    }
    // A gouraud edge clipped halfway takes the index halfway along it.
    {
        RetroScene scene;
        const double world[3][3] = {{0, -0.05, 1.1}, {0.05, 0.05, 1.1}, {0, 0, -0.9}};
        const std::uint8_t ramp[3] = {200, 200, 0};
        addTriangle(scene, camera, world, ramp, kGouraud);
        CHECK(!scene.triangles.empty());
        bool halfway = false;
        for (const RetroTriangle& t : scene.triangles)
            for (int v = 0; v < 3; ++v)
                halfway = halfway || t.index[v] == 100;
        CHECK(halfway);
    }
    // A vertex a million pixels to the right: cut to the guard band, so every vertex stays inside it.
    {
        RetroScene scene;
        const double world[3][3] = {{0, 0, 1}, {(1e6 - 160) / 120, 0.3, 1}, {-0.5, -0.5, 1}};
        addTriangle(scene, camera, world, flat, kFlat);
        CHECK(!scene.triangles.empty() && scene.triangles.size() <= 5);
        for (const RetroTriangle& t : scene.triangles) {
            for (int v = 0; v < 3; ++v) {
                CHECK(t.x[v] >= toFixed(evo::retro::kGuardMinX) && t.x[v] <= toFixed(evo::retro::kGuardMaxX));
                CHECK(t.y[v] >= toFixed(evo::retro::kGuardMinY) && t.y[v] <= toFixed(evo::retro::kGuardMaxY));
            }
        }
    }
}

void testAddPoint() {
    using evo::retro::addPoint;
    using evo::retro::project;
    using evo::retro::toFixed;
    const Camera camera = axisCamera(60);
    RetroScene scene;
    const double ahead[3] = {0.3, -0.2, 3}, behind[3] = {0, 0, 0.05}, offScreen[3] = {50, 0, 1},
                 edge[3] = {0, 0, 1e9};
    addPoint(scene, camera, ahead, 7, 2);
    addPoint(scene, camera, behind, 7, 2);
    addPoint(scene, camera, offScreen, 7, 2);
    addPoint(scene, camera, edge, 8, 1);
    CHECK(scene.points.size() == 2 && scene.pointDepth.size() == 2);
    const auto p = project(camera, ahead);
    CHECK(scene.points[0].x == toFixed(p.x) && scene.points[0].y == toFixed(p.y));
    CHECK(scene.points[0].index == 7 && scene.points[0].size == 2 && scene.pointDepth[0] == 3);
    CHECK(scene.points[1].index == 8 && scene.pointDepth[1] == 1e9);
}

void testSort() {
    RetroScene scene;
    for (int i = 0; i < 6; ++i) {
        RetroTriangle t{};
        t.index[0] = static_cast<std::uint8_t>(i);
        scene.triangles.push_back(t);
        RetroPoint p{};
        p.index = static_cast<std::uint8_t>(i);
        scene.points.push_back(p);
    }
    scene.triangleDepth = {2, 5, 2, 9, 5, 1};
    scene.pointDepth = {1, 1, 3, 1, 3, 0};
    evo::retro::sortBackToFront(scene);
    std::vector<int> triangles, points;
    for (const RetroTriangle& t : scene.triangles)
        triangles.push_back(t.index[0]);
    for (const RetroPoint& p : scene.points)
        points.push_back(p.index);
    CHECK(triangles == (std::vector<int>{3, 1, 4, 0, 2, 5})); // farthest first, ties in their order
    CHECK(points == (std::vector<int>{2, 4, 0, 1, 3, 5}));
    CHECK(scene.triangleDepth == (std::vector<double>{9, 5, 5, 2, 2, 1}));
    CHECK(scene.pointDepth == (std::vector<double>{3, 3, 1, 1, 1, 0}));

    scene.pointDepth.pop_back();
    CHECK(throws<std::logic_error>([&] { evo::retro::sortBackToFront(scene); }));
}

// --- GPU: the polygon modes against the reference --------------------------------------------------------

void testModes(Fixture& fixture) {
    using evo::retro::kFlat;
    using evo::retro::kGlenz;
    using evo::retro::kGouraud;
    RetroScene scene;
    // Flat, both windings, overlapping: the later one is on top.
    scene.triangles.push_back(screenTriangle(10.3, 12.7, 150.2, 20.1, 40.6, 120.9, kFlat, 17));
    scene.triangles.push_back(screenTriangle(60.5, 30.25, 90.75, 140.5, 160.125, 60.0, kFlat, 33));
    // Gouraud, steep ramps across a large triangle, and a small one.
    scene.triangles.push_back(screenTriangle(170.4, 5.2, 315.9, 40.3, 200.1, 190.6, kGouraud, 0, 255, 96));
    scene.triangles.push_back(screenTriangle(20.2, 150.1, 70.3, 195.7, 5.5, 185.25, kGouraud, 40, 47, 44));
    // Glenz: three overlapping faces with codes 1, 2 and 4 over a base of 64 (the clear index).
    scene.triangles.push_back(screenTriangle(100.5, 100.5, 220.5, 110.5, 150.5, 195.5, kGlenz, 1));
    scene.triangles.push_back(screenTriangle(130.0, 90.0, 250.0, 150.0, 120.0, 180.0, kGlenz, 2));
    scene.triangles.push_back(screenTriangle(240.0, 95.0, 110.0, 120.0, 190.0, 199.0, kGlenz, 4));
    // Partly off every edge of the screen (inside the guard band), and a degenerate one that draws nothing.
    scene.triangles.push_back(screenTriangle(-100.3, -50.7, 60.2, -10.1, -20.6, 30.9, kFlat, 90));
    scene.triangles.push_back(
        screenTriangle(300.3, 150.7, 420.2, 180.1, 290.6, 260.9, kGouraud, 10, 250, 120));
    scene.triangles.push_back(screenTriangle(10, 10, 100, 100, 55, 55, kFlat, 255));

    const Pixels gpu = fixture.draw(scene, 64);
    const Pixels cpu = reference(scene, 64);
    const std::size_t differing = differences(gpu, cpu);
    std::size_t glenz7 = 0, gouraudLevels = 0;
    std::vector<bool> seen(256, false);
    for (std::size_t i = 0; i < kPixels; ++i) {
        glenz7 += gpu[i] == (64 | 7);
        if (!seen[gpu[i]]) {
            seen[gpu[i]] = true;
            ++gouraudLevels;
        }
    }
    std::printf("raster: modes scene differs from the reference in %zu pixels; %zu distinct indices, %zu "
                "pixels under all three glenz faces\n",
                differing, gouraudLevels, glenz7);
    CHECK(differing == 0);
    CHECK(glenz7 > 100);        // the three faces overlap and OR their codes over the base
    CHECK(gouraudLevels > 200); // the 0..255 ramp shows (nearly) every index
    CHECK(gpu[0] == 90);        // the triangle reaching off the top-left corner covers pixel (0, 0)

    // The same triangle listed in the other winding draws the same pixels.
    RetroScene a, b;
    a.triangles.push_back(screenTriangle(33.3, 44.4, 280.8, 77.7, 150.5, 190.2, kGouraud, 5, 250, 128));
    b.triangles.push_back(screenTriangle(33.3, 44.4, 150.5, 190.2, 280.8, 77.7, kGouraud, 5, 128, 250));
    CHECK(fixture.draw(a) == fixture.draw(b));
}

// --- GPU: shared edges -------------------------------------------------------------------

// The distance from (px, py) to the segment a-b.
double segmentDistance(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax, dy = by - ay;
    const double t = std::clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// A fan of eight triangles round a centre at a random sub-pixel position, its rim at random radii, rotated
// 64 times: every pixel is covered by exactly one triangle inside the fan and by none outside.
void testFan(Fixture& fixture) {
    std::mt19937 random(1993);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    constexpr int kSpokes = 8;
    std::size_t inside = 0, outside = 0, edgeBand = 0;
    for (int rotation = 0; rotation < 64; ++rotation) {
        const double cx = 150 + 20 * unit(random), cy = 90 + 20 * unit(random);
        const double turn = 2 * std::numbers::pi * unit(random);
        std::vector<double> rimX(kSpokes), rimY(kSpokes);
        for (int i = 0; i < kSpokes; ++i) {
            const double angle = turn + 2 * std::numbers::pi * (i + 0.3 * unit(random)) / kSpokes;
            const double radius = 30 + 60 * unit(random);
            rimX[i] = cx + radius * std::cos(angle);
            rimY[i] = cy + radius * std::sin(angle) / evo::retro::kPixelAspect;
        }
        // Quantised once, shared by both triangles of every spoke, as the CPU transform shares vertices.
        const auto quantise = [](double v) { return evo::retro::toFixed(v) / 16.0; };
        RetroScene scene;
        for (int i = 0; i < kSpokes; ++i) {
            const int k = (i + 1) % kSpokes;
            scene.triangles.push_back(screenTriangle(quantise(cx), quantise(cy), quantise(rimX[i]),
                                                     quantise(rimY[i]), quantise(rimX[k]), quantise(rimY[k]),
                                                     evo::retro::kFlat, 1));
        }

        const std::vector<int> counts = coverageCounts(fixture, scene);
        // The reference agrees exactly, and so does the inside of the rim polygon away from its edges.
        std::vector<int> expected(kPixels, 0);
        for (const RetroTriangle& t : scene.triangles) {
            Pixels one(kPixels, 0);
            referenceTriangle(one, t);
            for (std::size_t p = 0; p < kPixels; ++p)
                expected[p] += one[p];
        }
        CHECK(counts == expected);
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                bool in = false;
                double nearest = 1e9;
                for (int i = 0; i < kSpokes; ++i) {
                    const int k = (i + 1) % kSpokes;
                    const double ax = quantise(rimX[i]), ay = quantise(rimY[i]);
                    const double bx = quantise(rimX[k]), by = quantise(rimY[k]);
                    if ((ay > py) != (by > py) && px < ax + (py - ay) * (bx - ax) / (by - ay))
                        in = !in;
                    nearest = std::min(nearest, segmentDistance(px, py, ax, ay, bx, by));
                }
                const int count = counts[static_cast<std::size_t>(y) * kWidth + x];
                if (nearest < 0.01) {
                    ++edgeBand;
                    CHECK(count <= 1);
                } else if (in) {
                    ++inside;
                    CHECK(count == 1);
                } else {
                    ++outside;
                    CHECK(count == 0);
                }
            }
        }
    }
    std::printf(
        "raster: 64 rotated fans: %zu pixels inside covered once, %zu outside untouched, %zu on the rim\n",
        inside, outside, edgeBand);
}

// A deterministic fan on the pixel grid: the centre exactly on a pixel centre and eight spokes along the axes
// and the diagonals, ending on pixel centres. Every horizontal spoke is an edge with dy == 0 (the top-left
// tie-break), every spoke runs through pixel centres, and all eight triangles share the centre pixel: each
// of those pixels must be drawn by exactly one triangle, inside exactly once, outside never.
void testGridFan(Fixture& fixture) {
    constexpr int kSpokes = 8;
    const double cx = 160.5, cy = 100.5;
    std::size_t inside = 0, outside = 0;
    for (const int reach : {1, 2, 7, 40, 90}) {
        double rimX[kSpokes], rimY[kSpokes];
        for (int i = 0; i < kSpokes; ++i) {
            constexpr int kDx[kSpokes] = {1, 1, 0, -1, -1, -1, 0, 1},
                          kDy[kSpokes] = {0, 1, 1, 1, 0, -1, -1, -1};
            rimX[i] = cx + reach * kDx[i];
            rimY[i] = cy + reach * kDy[i];
        }
        RetroScene scene;
        for (int i = 0; i < kSpokes; ++i) {
            const int k = (i + 1) % kSpokes;
            // Alternate windings: the rule must not depend on how a triangle lists its vertices.
            if (i % 2 == 0)
                scene.triangles.push_back(
                    screenTriangle(cx, cy, rimX[i], rimY[i], rimX[k], rimY[k], evo::retro::kFlat, 1));
            else
                scene.triangles.push_back(
                    screenTriangle(cx, cy, rimX[k], rimY[k], rimX[i], rimY[i], evo::retro::kFlat, 1));
        }
        const std::vector<int> counts = coverageCounts(fixture, scene);
        std::vector<int> expected(kPixels, 0);
        for (const RetroTriangle& t : scene.triangles) {
            Pixels one(kPixels, 0);
            referenceTriangle(one, t);
            for (std::size_t p = 0; p < kPixels; ++p)
                expected[p] += one[p];
        }
        CHECK(counts == expected);
        // The rim is the square of half-width `reach` round the centre pixel. A pixel centre on it belongs
        // to the fan only on the square's top and left sides (the top-left rule applied to the rim edges).
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                const int dx = x - 160, dy = y - 100;
                const bool in = dx >= -reach && dx < reach && dy >= -reach && dy < reach;
                const int count = counts[static_cast<std::size_t>(y) * kWidth + x];
                if (in) {
                    ++inside;
                    CHECK(count == 1);
                } else {
                    ++outside;
                    CHECK(count == 0);
                }
            }
        }
        CHECK(counts[100 * kWidth + 160] == 1); // the shared centre pixel: one owner
    }
    std::printf("raster: pixel-grid fans: %zu pixels inside covered once, %zu outside untouched\n", inside,
                outside);
}

// A fan of eight huge triangles in a plane tilted up to 40 degrees from facing the camera, which fills the
// view: its rim lies behind the camera and far off screen, so every triangle is cut by the near plane and the
// guard band. Under 64 random tilts and spins, every pixel is covered by exactly one piece.
void testClippedFan(Fixture& fixture) {
    std::mt19937 random(486);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const Camera camera = axisCamera(60);
    constexpr int kSpokes = 8;
    constexpr double kRim = 1e4;
    std::size_t pieces = 0;
    for (int rotation = 0; rotation < 64; ++rotation) {
        const double tilt = 40.0 * std::numbers::pi / 180 * std::sqrt(unit(random));
        const double heading = 2 * std::numbers::pi * unit(random);
        const double n[3] = {std::sin(tilt) * std::cos(heading), std::sin(tilt) * std::sin(heading),
                             std::cos(tilt)};
        // An orthonormal basis of the plane n.p = 1, whose nearest point to the eye is n itself.
        double u[3] = {n[1], -n[0], 0};
        const double length = std::hypot(u[0], u[1]);
        if (length < 1e-9) {
            u[0] = 1, u[1] = 0;
        } else {
            u[0] /= length, u[1] /= length;
        }
        const double w[3] = {n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]};
        const double spin = 2 * std::numbers::pi * unit(random);
        double rim[kSpokes][3];
        for (int i = 0; i < kSpokes; ++i) {
            const double a = spin + 2 * std::numbers::pi * i / kSpokes;
            for (int c = 0; c < 3; ++c)
                rim[i][c] = n[c] + kRim * (std::cos(a) * u[c] + std::sin(a) * w[c]);
        }
        RetroScene scene;
        const std::uint8_t index[3] = {1, 1, 1};
        for (int i = 0; i < kSpokes; ++i) {
            const int k = (i + 1) % kSpokes;
            // Alternate windings: the shared spokes are listed one way round in one triangle and the other
            // way in the next.
            const double world[3][3] = {
                {n[0], n[1], n[2]}, {rim[i][0], rim[i][1], rim[i][2]}, {rim[k][0], rim[k][1], rim[k][2]}};
            const double flipped[3][3] = {
                {rim[k][0], rim[k][1], rim[k][2]}, {rim[i][0], rim[i][1], rim[i][2]}, {n[0], n[1], n[2]}};
            evo::retro::addTriangle(scene, camera, i % 2 ? flipped : world, index, evo::retro::kFlat);
        }
        pieces += scene.triangles.size();
        const std::vector<int> counts = coverageCounts(fixture, scene);
        const auto [low, high] = std::minmax_element(counts.begin(), counts.end());
        if (*low != 1 || *high != 1)
            std::printf("raster: clipped fan %d: coverage %d..%d\n", rotation, *low, *high);
        CHECK(*low == 1 && *high == 1);
    }
    std::printf("raster: 64 clipped fans (%zu pieces) cover every pixel exactly once\n", pieces);
}

// --- GPU: far off screen -----------------------------------------------------------------

// A triangle with a vertex at x = 10^6 pixels draws exactly its on-screen part (up to the quantisation of
// the cut, within 0.1 pixel of its long edges), and quickly.
void testFarVertex(Fixture& fixture) {
    const Camera camera = axisCamera(90);
    const double world[3][3] = {{0, 0, 1}, {(1e6 - 160) / 120, 0.3, 1}, {-0.5, -0.5, 1}};
    const std::uint8_t index[3] = {200, 200, 200};
    RetroScene scene;
    evo::retro::addTriangle(scene, camera, world, index, evo::retro::kFlat);

    const auto start = std::chrono::steady_clock::now();
    const Pixels gpu = fixture.draw(scene);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    // The whole triangle in 64-bit integers.
    double sx[3], sy[3];
    RetroScene whole;
    RetroTriangle t{};
    for (int v = 0; v < 3; ++v) {
        const auto p = evo::retro::project(camera, world[v]);
        sx[v] = p.x, sy[v] = p.y;
        t.x[v] = evo::retro::toFixed(p.x), t.y[v] = evo::retro::toFixed(p.y);
        t.index[v] = 200;
    }
    whole.triangles.push_back(t);
    const Pixels cpu = reference(whole, 0);
    std::size_t covered = 0, nearEdge = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * kWidth + x;
            covered += gpu[at] != 0;
            if (gpu[at] == cpu[at])
                continue;
            const double px = x + 0.5, py = y + 0.5;
            const double distance = std::min(segmentDistance(px, py, sx[0], sy[0], sx[1], sy[1]),
                                             segmentDistance(px, py, sx[1], sy[1], sx[2], sy[2]));
            ++nearEdge;
            CHECK(distance < 0.1);
        }
    }
    std::printf("raster: vertex at x = 1e6: %zu pieces, %zu pixels drawn, %zu differ from the uncut triangle "
                "(all within 0.1 px of its edges), %.3f s\n",
                scene.triangles.size(), covered, nearEdge, seconds);
    CHECK(covered > 1000);
    CHECK(seconds < 2.0);
}

// --- GPU: points ------------------------------------------------------------------------------------------

void testPoints(Fixture& fixture) {
    RetroScene scene;
    // Each size at a sub-pixel position, and off every edge of the screen.
    scene.points.push_back(screenPoint(10.2, 10.7, 11, 1));
    scene.points.push_back(screenPoint(20.5, 10.5, 12, 2));
    scene.points.push_back(screenPoint(30.4, 10.6, 13, 3));
    scene.points.push_back(screenPoint(40.0, 10.0, 14, 4));
    scene.points.push_back(screenPoint(60.9, 10.1, 15, 7));
    scene.points.push_back(screenPoint(-0.5, -0.5, 21, 3));
    scene.points.push_back(screenPoint(319.9, 100.0, 22, 4));
    scene.points.push_back(screenPoint(160.0, 199.8, 23, 2));
    scene.points.push_back(screenPoint(-40.0, 100.0, 24, 4)); // wholly off: nothing
    // Thousands overlapping at random: the later point wins, whatever order the GPU runs them in.
    std::mt19937 random(320);
    std::uniform_real_distribution<double> x(-3, 323), y(-3, 203);
    std::uniform_int_distribution<int> index(1, 255), size(1, 4);
    for (int i = 0; i < 4000; ++i)
        scene.points.push_back(screenPoint(x(random), y(random), static_cast<std::uint8_t>(index(random)),
                                           static_cast<std::uint8_t>(size(random))));
    // Three on one spot last: 31 covers 99..102, 32 covers 101..102 and 33 the pixel (100, 100).
    scene.points.push_back(screenPoint(100.5, 100.5, 31, 4));
    scene.points.push_back(screenPoint(101.5, 101.5, 32, 2));
    scene.points.push_back(screenPoint(100.5, 100.5, 33, 1));
    // Under a triangle: points are drawn after the triangles.
    scene.triangles.push_back(screenTriangle(0, 0, 320, 0, 0, 200, evo::retro::kFlat, 5));

    const Pixels gpu = fixture.draw(scene, 2);
    const Pixels cpu = reference(scene, 2);
    std::printf("raster: %zu points differ from the reference in %zu pixels\n", scene.points.size(),
                differences(gpu, cpu));
    CHECK(gpu == cpu);
    const auto at = [&gpu](int x, int y) { return gpu[static_cast<std::size_t>(y) * kWidth + x]; };
    CHECK(at(99, 99) == 31 && at(100, 100) == 33 && at(101, 101) == 32 && at(102, 102) == 32);
    CHECK(at(102, 99) == 31 && at(99, 102) == 31);

    // Only points: nothing else changes.
    RetroScene one;
    one.points.push_back(screenPoint(5.5, 5.5, 77, 1));
    const Pixels single = fixture.draw(one, 3);
    CHECK(single[5 * kWidth + 5] == 77);
    CHECK(std::count(single.begin(), single.end(), 3) == static_cast<std::ptrdiff_t>(kPixels - 1));
}

// --- GPU: draws, slots and limits -------------------------------------------------------------------------

// Two scenes in one frame, over five frames that alternate between the ring's slots: each frame shows its
// own scenes (the slot's buffers are rewritten only once its previous frame is done).
void testDrawsAndSlots(Fixture& fixture) {
    for (int frame = 0; frame < 5; ++frame) {
        RetroScene first, second;
        first.triangles.push_back(screenTriangle(10 + frame, 10, 200, 30 + frame, 50, 180, evo::retro::kFlat,
                                                 static_cast<std::uint8_t>(40 + frame)));
        first.points.push_back(screenPoint(250 + frame, 50, 9, 3));
        second.triangles.push_back(
            screenTriangle(100, 50 + frame, 300, 60, 120, 190 - frame, evo::retro::kGlenz, 128));
        second.points.push_back(screenPoint(30, 150 + frame, 10, 2));
        const Pixels gpu = fixture.draw({&first, &second}, 1);
        Pixels cpu = reference(first, 1);
        for (const RetroTriangle& t : second.triangles)
            referenceTriangle(cpu, t);
        referencePoints(cpu, second.points);
        CHECK(gpu == cpu);
    }
}

void testLimits(Fixture& fixture) {
    RetroScene triangles, points, outside, farPoint;
    triangles.triangles.resize(evo::retro::kMaxTriangles + 1);
    points.points.resize(evo::retro::kMaxPoints + 1);
    outside.triangles.push_back(screenTriangle(0, 0, 900, 0, 0, 100, evo::retro::kFlat, 1));
    farPoint.points.push_back(screenPoint(-600, 10, 1, 1));
    evo::render::SceneRaster& raster = fixture.raster();

    CHECK(throws<std::logic_error>([&] { raster.draw(VK_NULL_HANDLE, fixture.image(), RetroScene{}); }));
    VkCommandBuffer cmd = fixture.ring().begin();
    CHECK(throws<std::length_error>([&] { raster.draw(cmd, fixture.image(), triangles); }));
    CHECK(throws<std::length_error>([&] { raster.draw(cmd, fixture.image(), points); }));
    CHECK(throws<std::invalid_argument>([&] { raster.draw(cmd, fixture.image(), outside); }));
    CHECK(throws<std::invalid_argument>([&] { raster.draw(cmd, fixture.image(), farPoint); }));
    fixture.ring().wait(fixture.ring().submit());

    // At the limits exactly, it draws.
    triangles.triangles.pop_back();
    points.points.pop_back();
    CHECK(fixture.draw({&triangles, &points}, 0) == Pixels(kPixels, 0)); // all-zero primitives draw index 0
}

// The CPU check SceneRaster::draw runs (and retro_registry_test runs on every retro frame of every scene):
// the same exceptions, the guard band inclusive, the limits allowed exactly.
void testCheckScene() {
    using evo::retro::checkScene;
    RetroScene triangles, points;
    triangles.triangles.resize(evo::retro::kMaxTriangles);
    points.points.resize(evo::retro::kMaxPoints);
    checkScene(triangles);
    checkScene(points);
    triangles.triangles.emplace_back();
    points.points.emplace_back();
    CHECK(throws<std::length_error>([&] { checkScene(triangles); }));
    CHECK(throws<std::length_error>([&] { checkScene(points); }));

    using evo::retro::kGuardMaxX;
    using evo::retro::kGuardMaxY;
    using evo::retro::kGuardMinX;
    using evo::retro::kGuardMinY;
    RetroScene edge;
    edge.triangles.push_back(screenTriangle(kGuardMinX, kGuardMinY, kGuardMaxX, kGuardMinY, kGuardMaxX,
                                            kGuardMaxY, evo::retro::kFlat, 1));
    edge.points.push_back(screenPoint(kGuardMinX, kGuardMaxY, 1, 1));
    checkScene(edge);
    for (int side = 0; side < 4; ++side) {
        const double nudge = 1.0 / 16;
        const double x = side == 0 ? kGuardMinX - nudge : side == 1 ? kGuardMaxX + nudge : 100;
        const double y = side == 2 ? kGuardMinY - nudge : side == 3 ? kGuardMaxY + nudge : 100;
        RetroScene triangle, point;
        triangle.triangles.push_back(screenTriangle(100, 100, x, y, 50, 120, evo::retro::kFlat, 1));
        point.points.push_back(screenPoint(x, y, 1, 1));
        CHECK(throws<std::invalid_argument>([&] { checkScene(triangle); }));
        CHECK(throws<std::invalid_argument>([&] { checkScene(point); }));
    }
}

// --- GPU: the retro stage draws an effect's scene ---------------------------------------------------------

// A test registry: Dots is a scene over a background, Tunnel the test pattern with the same scene over it,
// Glenz the test pattern alone, Lens a scene past the triangle limit; the rest is the test pattern.
void stageScene(RetroScene& scene, const evo::show::TimelineFrame&, std::uint32_t retroFrame) {
    const double shift = retroFrame % 7;
    scene.triangles.push_back(
        screenTriangle(20 + shift, 20, 250, 40 + shift, 80, 180, evo::retro::kGouraud, 10, 250, 128));
    scene.triangles.push_back(screenTriangle(150, 10, 310, 100, 200, 190 - shift, evo::retro::kGlenz, 1));
    scene.points.push_back(screenPoint(30 + shift, 150, 77, 3));
}

void tooManyTriangles(RetroScene& scene, const evo::show::TimelineFrame&, std::uint32_t) {
    scene.triangles.resize(evo::retro::kMaxTriangles + 1);
}

std::uint8_t stageBackground(const evo::show::TimelineFrame&, std::uint32_t) {
    return 6;
}

const evo::retro::EffectInfo& testRegistry(evo::show::Effect effect) {
    using evo::retro::EffectInfo;
    static constexpr EffectInfo kPattern{evo::retro::kTestPatternShader, 35, evo::retro::grayPalette};
    static constexpr EffectInfo kSceneOnly{nullptr,         35,        evo::retro::grayPalette, nullptr,
                                           stageBackground, stageScene};
    static constexpr EffectInfo kPatternScene{
        evo::retro::kTestPatternShader, 35, evo::retro::grayPalette, nullptr, nullptr, stageScene};
    static constexpr EffectInfo kTooMany{nullptr, 35,      evo::retro::grayPalette,
                                         nullptr, nullptr, tooManyTriangles};
    switch (effect) {
    case evo::show::Effect::Dots:
        return kSceneOnly;
    case evo::show::Effect::Tunnel:
        return kPatternScene;
    case evo::show::Effect::Lens:
        return kTooMany;
    default:
        return kPattern;
    }
}

class StageFixture {
public:
    explicit StageFixture(evo::gfx::Context& ctx)
        : ring_(ctx), text_(ctx, ring_), stage_(ctx, ring_, text_, testRegistry),
          buffer_(ctx, {kPixels, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "raster test stage readback"}) {}

    Pixels render(evo::show::Effect effect, std::uint32_t retroFrame, bool* rendered = nullptr) {
        evo::show::TimelineFrame frame;
        frame.chapter = 4;
        frame.effect = effect;
        frame.localSeconds = (retroFrame + 0.5) / 35.0;
        VkCommandBuffer cmd = ring_.begin();
        bool recorded = false;
        try {
            recorded = stage_.render(cmd, frame);
        } catch (...) {
            ring_.wait(ring_.submit());
            throw;
        }
        if (rendered)
            *rendered = recorded;
        evo::gfx::Image& index = stage_.indexImage();
        index.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kWidth, kHeight, 1};
        vkCmdCopyImageToBuffer(cmd, index.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_.handle(), 1,
                               &region);
        evo::gfx::bufferBarrier2(cmd, buffer_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                 VK_ACCESS_2_HOST_READ_BIT);
        ring_.wait(ring_.submit());
        buffer_.invalidate();
        const auto* data = static_cast<const std::uint8_t*>(buffer_.mapped());
        return {data, data + kPixels};
    }

private:
    evo::gfx::FrameRing ring_;
    evo::text::TextPass text_;
    evo::render::RetroStage stage_;
    evo::gfx::Buffer buffer_;
};

void testStage(evo::gfx::Context& ctx) {
    using evo::show::Effect;
    StageFixture fixture(ctx);
    const auto expectedScene = [](std::uint32_t retroFrame, Pixels under) {
        RetroScene scene;
        stageScene(scene, evo::show::TimelineFrame{}, retroFrame);
        for (const RetroTriangle& t : scene.triangles)
            referenceTriangle(under, t);
        referencePoints(under, scene.points);
        return under;
    };

    // A scene over the background, frame after frame (the slots alternate), and again after other frames.
    bool rendered = false;
    const Pixels at3 = fixture.render(Effect::Dots, 3, &rendered);
    CHECK(rendered);
    CHECK(at3 == expectedScene(3, Pixels(kPixels, 6)));
    const Pixels at4 = fixture.render(Effect::Dots, 4);
    CHECK(at4 == expectedScene(4, Pixels(kPixels, 6)));
    CHECK(at4 != at3);
    fixture.render(Effect::Dots, 4, &rendered);
    CHECK(!rendered); // the same retro frame is cached
    CHECK(fixture.render(Effect::Dots, 3) == at3);

    // Over an effect shader: the pattern of the retro frame, with the scene on top.
    const Pixels pattern = fixture.render(Effect::Glenz, 5);
    const Pixels withScene = fixture.render(Effect::Tunnel, 5);
    CHECK(withScene == expectedScene(5, pattern));
    CHECK(withScene != pattern);

    // Past the limit, the stage throws rather than drawing part of the scene.
    CHECK(throws<std::length_error>([&] { fixture.render(Effect::Lens, 0); }));
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testProjection();
        testAddTriangle();
        testAddPoint();
        testSort();
        testCheckScene();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            {
                Fixture fixture(ctx);
                testModes(fixture);
                testFan(fixture);
                testGridFan(fixture);
                testClippedFan(fixture);
                testFarVertex(fixture);
                testPoints(fixture);
                testDrawsAndSlots(fixture);
                testLimits(fixture);
            }
            testStage(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("raster_test: %d checks passed\n", checksPassed);
    return 0;
}
