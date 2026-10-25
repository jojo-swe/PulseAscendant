// The Ship scene (step 5, Task 16: the starship's crossing in WORMHOLE), with synchronization validation:
//   (m) the map: WORMHOLE shows the Ship, FLYBY does not (its 2026 side is the Studio's pull-out);
//   (k) the card: "1 KM RINGS" is the rings' spacing, "VOLUMETRIC" the plumes' grid (2 x 96^3 voxels at full
//       detail), "MOTION BLUR" the crossing below; 262,144 stars at full detail, scaled by the detail;
//   (s) the CPU sweep over every 1/60 s (cameras, shots, capacities, transforms, lights);
//   (f) the flight: the ship banks with FLYBY's 1993 attitude; every shot starts on a bar line; the ship
//       passes the crossing camera on a downbeat; from D a ring passes the ship on every beat;
//   (e) the engines: Planck's law at 6500 K is white, the plumes run from white-blue at the exit through
//       orange to dark, the afterburner lights on the cut into the tunnel; the seams pulse on the kick and
//       the strobe flashes on the beat;
//   (p) the standard chapter pictures (20 %, the accent, 80 %; the accent twice, byte for byte);
//   (c) the crossing in export at spp 32 (8 shutter slices, as the default 64 spp export has): with
//       the 1/120 s shutter the ship's silhouette spreads at least 2 px further along its screen velocity
//       than without; the volume layer behind the nozzles holds the plumes (transmittance below 0.95,
//       radiance above 0).
// `scene_ship_test --look <dir> [spp] [width height]` renders stills instead (EVO_LOOK_ONLY, EVO_LOOK_AT and
// EVO_LOOK_DETAIL narrow them).

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/ship.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "retro/flyby_flight.hpp"
#include "show/timeline.hpp"

#include "scene_fixture.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace gfx = evo::gfx;
namespace modern = evo::modern;
namespace render = evo::render;
namespace retro = evo::retro;
namespace show = evo::show;
using modern::SceneId;
using modern::Vec3;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

int shipChapter() {
    const std::vector<int> chapters = evo::test::chaptersOf(SceneId::Ship);
    if (chapters.size() != 1)
        throw std::runtime_error("the Ship should show in exactly one chapter");
    return chapters.front();
}

show::TimelineFrame forced2026(double seconds) {
    show::TimelineFrame frame = show::sampleTimeline(seconds);
    frame.era = 1.f;
    frame.wipe = 1.f;
    frame.eraCut = 0.f;
    return frame;
}

show::TimelineFrame at(double local) {
    return forced2026(show::chapterStart(shipChapter()) + local);
}

// --- CPU
// ------------------------------------------------------------------------------------------------------

void testMap() {
    const int chapter = shipChapter();
    CHECK(show::sampleTimeline(show::chapterStart(chapter)).effect == show::Effect::Wormhole);
    CHECK(modern::sceneFor(show::Effect::Flyby) != SceneId::Ship);
    std::printf("(m) WORMHOLE (chapter %d) shows the Ship; FLYBY does not\n", chapter);
}

void testCard() {
    const show::TimelineFrame f = at(3.0);
    const std::string card = f.nowCard;
    CHECK(card == "2026 \xC2\xB7 WORMHOLE \xC2\xB7 1 KM RINGS \xC2\xB7 VOLUMETRIC \xC2\xB7 MOTION BLUR");
    CHECK(modern::kWormholeRingSpacing == 1000.f); // "1 KM RINGS"
    const modern::Ship full({1.f});
    const modern::ShipStats& stats = full.stats();
    CHECK(stats.stars == 262144u && modern::kShipStars == 262144u);
    CHECK(stats.plumeGrid[0] == 192u && stats.plumeGrid[1] == 96u && stats.plumeGrid[2] == 96u);
    CHECK(stats.plumeVoxels == 2u * 96u * 96u * 96u && stats.plumeVoxels == modern::kShipPlumeVoxels);
    std::printf("(k) triangles %u\n", stats.triangles);
    CHECK(stats.triangles >= 35000u && stats.triangles <= 60000u);
    const modern::SceneFrame frame = full.frame(f); // "VOLUMETRIC": the plumes' grid
    CHECK(frame.medium && frame.medium->grid && frame.medium->grid->size == stats.plumeGrid);
    CHECK(frame.splats && frame.splats->count == stats.splats);
    CHECK(full.staticScene().capacity.splats >= stats.splats);
    const modern::ShipStats small = modern::shipStats({1.f / 256});
    CHECK(small.stars == 1024u && small.triangles == stats.triangles);
    CHECK(small.plumeGrid[0] >= 16u && small.plumeGrid[1] >= 16u && small.plumeGrid[0] < 192u);
    std::printf("(k) \"%s\": %u stars, %u air splats, plumes %ux%ux%u = %u voxels, %u triangles\n",
                card.c_str(), stats.stars, stats.airSplats, stats.plumeGrid[0], stats.plumeGrid[1],
                stats.plumeGrid[2], stats.plumeVoxels, stats.triangles);
}

void testSweep() {
    evo::test::SweepLimits limits;
    limits.maxCameraStep = 8.f; // the crossing camera stands in the world while the ship flies at 300 m/s
    const evo::test::SweepStats stats = evo::test::checkSceneSweep(SceneId::Ship, limits);
    CHECK(stats.shots == 5);
    CHECK(stats.frames > 1500);
    std::printf("(s) %u frames, %u shots, largest camera step %.2f m\n", stats.frames, stats.shots,
                stats.maxCameraStep);
}

bool onDownbeat(const show::TimelineFrame& head, double local) {
    const show::BeatGridView grid = show::beatGrid(head.track);
    const double start = head.trackSeconds - head.localSeconds;
    const int beat = grid.beatAt(start + local + 1e-6);
    return beat >= 0 && grid.bar(beat) == 0 && std::abs(grid.beatTime(beat) - (start + local)) < 1e-6;
}

void testFlight() {
    const show::TimelineFrame head = at(0.0);
    const modern::ShipTimes t = modern::shipTimes(head);
    std::printf("(f) pass %.3f, tunnel %.3f, D %.3f, accent %.3f, release %.3f, white-out %.3f, end %.3f\n",
                t.pass, t.tunnel, t.drop, t.accent, t.release, t.whiteout, t.duration);
    CHECK(t.crossing < t.pass - 1.0 && t.pass < t.tunnel && t.tunnel < t.drop && t.drop <= t.accent &&
          t.accent < t.release && t.release < t.whiteout && t.whiteout < t.duration);
    for (const double cut : {t.crossing, t.pass, t.tunnel, t.drop, t.release, t.whiteout})
        CHECK(onDownbeat(head, cut));
    const auto shots = modern::shipShots(head);
    CHECK(shots.size() == 5);
    // The attitude is the 1993 ship's.
    for (double local = 0; local < t.duration; local += 0.25) {
        const modern::ShipPose pose = modern::shipPose(at(local));
        const retro::FlybyAttitude a = retro::flybyAttitude(local);
        for (int i = 0; i < 3; ++i)
            CHECK(std::abs(pose.rows[i].x - a.m[i][0]) < 1e-6 &&
                  std::abs(pose.rows[i].y - a.m[i][1]) < 1e-6 && std::abs(pose.rows[i].z - a.m[i][2]) < 1e-6);
    }
    // On the pass's downbeat the ship (the render frame's origin) is at the crossing camera's centre.
    {
        const modern::Camera c = modern::shipCamera(at(t.pass));
        const Vec3 d = normalize(c.target - c.eye), toShip = normalize(Vec3{0, 0, 0} - c.eye);
        CHECK(c.shot == 1 && dot(d, toShip) > std::cos(0.5f * 3.14159265f / 180.f));
    }
    // From D a ring passes the ship on every beat.
    const show::BeatGridView grid = show::beatGrid(head.track);
    const double start = head.trackSeconds - head.localSeconds;
    int rings = 0;
    for (int b = grid.beatAt(start + t.drop + 1e-6); b < grid.count(); ++b) {
        const double local = grid.beatTime(b) - start;
        if (local >= t.duration - 0.01)
            break;
        const double z = modern::shipDistance(at(local));
        if (!(std::abs(z - modern::shipRingZ(head, rings)) < 1e-3))
            throw std::runtime_error("beat " + std::to_string(b) + " at " + std::to_string(local) +
                                     " s: ship at " + std::to_string(z) + ", ring " + std::to_string(rings) +
                                     " at " + std::to_string(modern::shipRingZ(head, rings)));
        ++rings;
    }
    CHECK(rings >= 25);
    // The flight never runs backwards and is about one ring per beat at D.
    double last = -1;
    for (double local = 0; local < t.duration; local += 1.0 / 60) {
        const double z = modern::shipDistance(at(local));
        CHECK(z >= last);
        last = z;
    }
    std::printf("(f) the 1993 attitude throughout; %d rings on the beats from D; flight monotonic\n", rings);
}

void testEngines() {
    const Vec3 white = modern::shipBlackbody(6500.f);
    CHECK(std::abs(white.x - 1.f) < 1e-5f && std::abs(white.y - 1.f) < 1e-5f &&
          std::abs(white.z - 1.f) < 1e-5f);
    const Vec3 hot = modern::shipBlackbody(9000.f), ember = modern::shipBlackbody(1900.f);
    CHECK(hot.z > hot.y && hot.y > hot.x);
    CHECK(ember.x > ember.y && ember.y > ember.z);
    // Along the axis at cruise: white-blue at the exit, orange half way, dark at the end.
    const float cruise = 0.7f, length = modern::shipPlumeLength(cruise);
    const float exitK = modern::shipPlumeTemperature(0.f, 0.f, cruise);
    const float midK = modern::shipPlumeTemperature(0.5f * length, 0.f, cruise);
    const float endK = modern::shipPlumeTemperature(length, 0.f, cruise);
    std::printf("(e) plume at cruise: %.0f K at the exit, %.0f K half way, %.0f K at the end\n", exitK, midK,
                endK);
    CHECK(exitK > 7000.f && midK > 1700.f && midK < 3500.f && endK < 1500.f);
    const Vec3 a = modern::shipPlumeEmission(exitK), b = modern::shipPlumeEmission(midK),
               c = modern::shipPlumeEmission(endK);
    CHECK(a.z / a.x > 0.95f && b.x > b.y && b.y > b.z);
    const auto lum = [](Vec3 v) { return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z; };
    CHECK(lum(a) > lum(b) && lum(b) > lum(c));
    // Shock diamonds only under the afterburner.
    const float spacing = 0.75f + 0.45f * 1.2f;
    CHECK(modern::shipPlumeTemperature(spacing, 0.f, 1.2f) >
          modern::shipPlumeTemperature(1.5f * spacing, 0.f, 1.2f));
    // The throttle: cruising, the afterburner lights on the cut into the tunnel and holds.
    const modern::ShipTimes t = modern::shipTimes(at(0.0));
    show::TimelineFrame f = at(t.tunnel - 0.5);
    f.bass = 0.f;
    CHECK(std::abs(modern::shipThrottle(f) - 0.7f) < 1e-6f);
    f = at(t.tunnel + 0.01);
    f.bass = 0.f;
    CHECK(modern::shipThrottle(f) > 1.4f);
    f = at(t.drop - 0.2);
    f.bass = 0.f;
    CHECK(std::abs(modern::shipThrottle(f) - 1.f) < 0.02f);
    // The seams pulse on the kick; the strobe flashes on the beat; the throats follow the throttle.
    const modern::Ship ship({1.f / 256});
    show::TimelineFrame q = at(t.drop + 1.0);
    q.kick = 0.f;
    q.beatPhase = 0.5f;
    const auto quiet = ship.materials(q);
    q.kick = 1.f;
    q.beatPhase = 0.02f;
    const auto loud = ship.materials(q);
    const modern::ShipAsset& asset = ship.asset();
    CHECK(loud[asset.glowMaterial()].emissionScale > 3.f * quiet[asset.glowMaterial()].emissionScale);
    CHECK(quiet[asset.strobeMaterial()].emissionScale == 0.f &&
          loud[asset.strobeMaterial()].emissionScale > 0.f);
    CHECK(loud[ship.ringMaterial()].emissionScale > quiet[ship.ringMaterial()].emissionScale);
    CHECK(ship.frame(q).materials.size() == ship.staticScene().materials.size());
    f = at(t.tunnel + 0.01);
    show::TimelineFrame g = at(t.tunnel - 0.5);
    CHECK(ship.materials(f)[asset.throatMaterial()].emissionScale >
          ship.materials(g)[asset.throatMaterial()].emissionScale);
    std::printf("(e) Planck white at 6500 K; the ramp white-blue, orange, dark; afterburner on the cut\n");
}

// --- The crossing (GPU) ------------------------------------------------------------------------------------

constexpr std::uint32_t kW = 160, kH = 90;

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    float value;
    if (exponent == 0)
        value = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31)
        value = mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else
        value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -value : value;
}

struct Plane {
    std::uint32_t width = 0, height = 0;
    std::vector<float> rgba;
    float at(int x, int y, int c) const { return rgba[(std::size_t(y) * width + std::size_t(x)) * 4 + c]; }
    float luminance(int x, int y) const {
        return 0.2126f * at(x, y, 0) + 0.7152f * at(x, y, 1) + 0.0722f * at(x, y, 2);
    }
};

// Copies an RGBA16F image into a host buffer within the frame being recorded.
gfx::Buffer recordRead(gfx::Context& ctx, VkCommandBuffer cmd, gfx::Image& image) {
    if (image.format() != VK_FORMAT_R16G16B16A16_SFLOAT)
        throw std::logic_error("recordRead: expected RGBA16F");
    const VkExtent2D extent = image.extent();
    gfx::Buffer buffer(ctx, {VkDeviceSize(extent.width) * extent.height * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             true, "ship test readback"});
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle(), 1,
                           &region);
    gfx::bufferBarrier2(cmd, buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    return buffer;
}

Plane finishRead(const gfx::Buffer& buffer, VkExtent2D extent) {
    buffer.invalidate();
    Plane plane;
    plane.width = extent.width;
    plane.height = extent.height;
    const std::size_t values = std::size_t(extent.width) * extent.height * 4;
    plane.rgba.resize(values);
    const auto* bytes = static_cast<const std::uint8_t*>(buffer.mapped());
    for (std::size_t i = 0; i < values; ++i) {
        std::uint16_t h;
        std::memcpy(&h, bytes + 2 * i, 2);
        plane.rgba[i] = halfToFloat(h);
    }
    return plane;
}

struct Pictures {
    Plane hdr, volume;
};

// One export frame of the show's Ship at spp 8, with or without the shutter: its HDR picture and volume
// layer.
Pictures renderExport(gfx::Context& ctx, double seconds, double shutter) {
    gfx::FrameRing ring(ctx);
    render::RenderSettings s{kW, kH};
    s.hud = false;
    s.mode = render::RenderMode::Export;
    s.spp = 32; // 8 shutter slices, as an export at the default 64 spp has
    s.detail = {1.f / 256};
    s.shutterSeconds = shutter;
    Pictures out;
    {
        render::Renderer renderer(ctx, ring, s);
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, forced2026(seconds));
        gfx::Image& hdr = renderer.modern().hdr();
        gfx::Image& volume = renderer.modern().tracer()->gbuffer().volume;
        const gfx::Buffer hdrRead = recordRead(ctx, cmd, hdr);
        const gfx::Buffer volumeRead = recordRead(ctx, cmd, volume);
        ring.wait(ring.submit());
        idle(ctx);
        out.hdr = finishRead(hdrRead, hdr.extent());
        out.volume = finishRead(volumeRead, volume.extent());
    }
    idle(ctx);
    return out;
}

struct Pixel {
    float x, y;
    bool visible;
};

Pixel project(const modern::Camera& c, Vec3 p) {
    const modern::CameraBasis b = modern::cameraBasis(c);
    const Vec3 v = p - c.eye;
    const float z = dot(v, b.forward);
    const float t = std::tan(0.5f * c.fovYDegrees * 3.14159265f / 180.f);
    const float aspect = float(kW) / float(kH);
    if (!(z > 1e-3f))
        return {0, 0, false};
    const float nx = dot(v, b.right) / (z * t * aspect), ny = dot(v, b.up) / (z * t);
    const float x = (nx + 1.f) * 0.5f * kW, y = (1.f - ny) * 0.5f * kH;
    return {x, y, x >= 0 && y >= 0 && x < kW && y < kH};
}

// A window round the ship in the picture: the bounding box of its projected extremes (nose, tail, wing tips,
// nacelles, fin tips, canopy, belly) with a margin. The plumes leave it through its edge, so they are cut the
// same with and without the shutter, and the planet below stays out of it.
struct Window {
    int x0, y0, x1, y1;
};

Window shipWindow(const modern::Camera& c, int margin) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    const auto add = [&](Vec3 p) {
        const Pixel q = project(c, p);
        x0 = std::min(x0, q.x), y0 = std::min(y0, q.y), x1 = std::max(x1, q.x), y1 = std::max(y1, q.y);
    };
    add({0.f, 0.f, 10.5f});
    add({0.f, 0.f, -8.4f});
    add({0.f, 1.6f, 1.5f});
    add({0.f, -0.9f, 0.f});
    for (const float side : {1.f, -1.f}) {
        add({7.45f * side, 0.4f, -6.3f});
        add({2.96f * side, -0.52f, -8.4f});
        add({2.96f * side, -0.52f, -1.3f});
        add({1.75f * side, 2.4f, -7.5f});
    }
    return {std::max(0, int(x0) - margin), std::max(0, int(y0) - margin),
            std::min(int(kW) - 1, int(x1) + margin), std::min(int(kH) - 1, int(y1) + margin)};
}

// The ship's pixels: the region 4-connected, within the window, to the pixel nearest `seed` with luminance
// above `threshold`.
std::vector<std::pair<int, int>> shipPixels(const Plane& p, const Window& win, Pixel seed, float threshold) {
    const int w = int(p.width);
    int sx = -1, sy = -1;
    float best = 1e9f;
    for (int y = win.y0; y <= win.y1; ++y)
        for (int x = win.x0; x <= win.x1; ++x)
            if (p.luminance(x, y) > threshold) {
                const float d = std::hypot(x + 0.5f - seed.x, y + 0.5f - seed.y);
                if (d < best)
                    best = d, sx = x, sy = y;
            }
    if (sx < 0 || best > 6.f)
        throw std::runtime_error("no bright ship near its projected centre");
    std::vector<char> seen(std::size_t(w) * p.height, 0);
    std::vector<std::pair<int, int>> stack{{sx, sy}}, out;
    seen[std::size_t(sy) * w + sx] = 1;
    while (!stack.empty()) {
        const auto [x, y] = stack.back();
        stack.pop_back();
        out.push_back({x, y});
        for (const auto [nx, ny] :
             {std::pair{x + 1, y}, std::pair{x - 1, y}, std::pair{x, y + 1}, std::pair{x, y - 1}}) {
            if (nx < win.x0 || ny < win.y0 || nx > win.x1 || ny > win.y1 || seen[std::size_t(ny) * w + nx])
                continue;
            if (p.luminance(nx, ny) > threshold) {
                seen[std::size_t(ny) * w + nx] = 1;
                stack.push_back({nx, ny});
            }
        }
    }
    return out;
}

// The silhouette's extent along (dx, dy): from its last pixel to its first. The plumes leave the window, so
// the trailing end is the window's edge either way and the difference is the leading edge's smear.
float extent(const std::vector<std::pair<int, int>>& pixels, float dx, float dy) {
    float lo = 1e9f, hi = -1e9f;
    for (const auto [x, y] : pixels) {
        const float along = (x + 0.5f) * dx + (y + 0.5f) * dy;
        lo = std::min(lo, along);
        hi = std::max(hi, along);
    }
    return hi - lo;
}

void testCrossing(gfx::Context& ctx) {
    const double start = show::chapterStart(shipChapter());
    // The ship nearest the locked-off crossing camera, where it crosses the picture fastest.
    const double local = modern::shipTimes(at(0.0)).pass - 0.02;
    const double seconds = start + local;
    const show::TimelineFrame frame = forced2026(seconds);
    const modern::Camera camera = modern::shipCamera(frame);
    CHECK(camera.shot == 1);
    // The ship's screen velocity: its centre (the render frame's origin) projected a little before and after.
    const Pixel before = project(modern::shipCamera(forced2026(seconds - 0.004)), {0, 0, 0});
    const Pixel after = project(modern::shipCamera(forced2026(seconds + 0.004)), {0, 0, 0});
    const Pixel centre = project(camera, {0, 0, 0});
    CHECK(centre.visible);
    const float vx = after.x - before.x, vy = after.y - before.y, speed = std::hypot(vx, vy);
    CHECK(speed > 0.f);
    const float px120 = speed / 0.008f / 120.f; // pixels the centre moves in 1/120 s
    std::printf("crossing at %.3f s: centre (%.1f, %.1f), %.1f px per 1/120 s\n", local, centre.x, centre.y,
                px120);

    const Pictures sharp = renderExport(ctx, seconds, 0.0);
    const Pictures blurred = renderExport(ctx, seconds, 1.0 / 120);
    // Above the black of space and the stars, well below the plumes and the sunlit hull.
    const float threshold = 0.006f;
    const Window win = shipWindow(camera, 10);
    const float dx = vx / speed, dy = vy / speed;
    const auto sharpShip = shipPixels(sharp.hdr, win, centre, threshold);
    const auto blurredShip = shipPixels(blurred.hdr, win, centre, threshold);
    const float a = extent(sharpShip, dx, dy), b = extent(blurredShip, dx, dy);
    std::printf(
        "crossing: window (%d, %d)-(%d, %d); silhouette %.1f px along the velocity sharp, %.1f px with the "
        "1/120 s shutter\n",
        win.x0, win.y0, win.x1, win.y1, a, b);
    CHECK(b >= a + 2.f);

    // The plumes: behind each projected nozzle, the volume layer holds the fire.
    int checked = 0;
    for (const Vec3 nozzle : modern::shipNozzles()) {
        const Pixel p = project(camera, nozzle + Vec3{0, 0, -1.0f});
        if (!p.visible)
            continue;
        const int x = int(p.x), y = int(p.y);
        const float t = sharp.volume.at(x, y, 3);
        const float l = sharp.volume.luminance(x, y);
        std::printf("plume behind a nozzle at (%d, %d): T %.3f, L %.4f\n", x, y, t, l);
        CHECK(t < 0.95f);
        CHECK(l > 0.f);
        ++checked;
    }
    CHECK(checked >= 1);
}

// --- Stills to look at
// ---------------------------------------------------------------------------------------

// `scene_ship_test --look <dir> [spp] [width height]`: stills of FLYBY in export (with the shutter of 60
// fps).
void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp, std::uint32_t w,
                std::uint32_t h) {
    const double start = show::chapterStart(shipChapter());
    const modern::ShipTimes t = modern::shipTimes(at(0.0));
    struct Shot {
        double local;
        const char* name;
    };
    std::vector<Shot> shots = {
        {2.5, "ship_1_gate"},
        {t.pass - 0.02, "ship_2_crossing"},
        {t.pass + 1.2, "ship_3_follow"},
        {t.tunnel - 0.6, "ship_4_gate"},
        {t.tunnel + 2.5, "ship_5_tunnel"},
        {t.accent + 1.255, "ship_6_riding"},
        {t.release - 1.5, "ship_7_riding"},
        {t.whiteout + 2.6, "ship_8_whiteout"},
    };
    if (const char* only = std::getenv("EVO_LOOK_ONLY")) {
        std::vector<Shot> kept;
        for (const Shot& s : shots)
            if (std::string(s.name).find(only) != std::string::npos)
                kept.push_back(s);
        shots = kept;
    }
    if (const char* at = std::getenv("EVO_LOOK_AT"))
        shots = {{std::stod(at), "ship_at"}};
    gfx::FrameRing ring(ctx);
    render::RenderSettings s{w, h};
    s.hud = false;
    s.mode = render::RenderMode::Export;
    s.spp = spp;
    s.detail = {1.f};
    if (const char* d = std::getenv("EVO_LOOK_DETAIL"))
        s.detail = {std::stof(d)};
    s.shutterSeconds = 0.5 / 60;
    render::Renderer renderer(ctx, ring, s);
    gfx::Readback readback(ctx, w, h);
    std::vector<double> times;
    for (const Shot& shot : shots)
        times.push_back(start + shot.local);
    renderer.prepare(times);
    std::filesystem::create_directories(dir);
    for (std::size_t i = 0; i < shots.size(); ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, forced2026(times[i]));
        readback.record(cmd, renderer.output());
        ring.wait(ring.submit());
        const std::vector<std::uint8_t> rgb = readback.rgb();
        double mean = 0;
        for (std::size_t p = 0; p < rgb.size(); p += 3)
            mean += (0.2126 * rgb[p] + 0.7152 * rgb[p + 1] + 0.0722 * rgb[p + 2]) / 255.0;
        mean /= double(rgb.size() / 3);
        const std::filesystem::path path = dir / (std::string(shots[i].name) + ".png");
        evo::image::writePng(path, int(w), int(h), rgb.data());
        std::printf("look: %s (%.2f s, mean %.3f) in %.1f s\n", path.string().c_str(), shots[i].local, mean,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    idle(ctx);
}

} // namespace

int main(int argc, char** argv) {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            auto ctx = gfx::Context::create({.validation = true});
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 16u,
                       argc >= 6 ? std::uint32_t(std::stoul(argv[4])) : 480u,
                       argc >= 6 ? std::uint32_t(std::stoul(argv[5])) : 270u);
            return ctx.validationMessageCount() == 0 ? 0 : 1;
        }
        const auto timed = [](const char* name, auto&& run) {
            const auto t0 = std::chrono::steady_clock::now();
            run();
            std::printf("  [%s %.1f s]\n", name,
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        };
        timed("m", testMap);
        timed("k", testCard);
        timed("s", testSweep);
        timed("f", testFlight);
        timed("e", testEngines);
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("SKIP sync validation: unavailable\n");
            timed("p", [&] { evo::test::checkChapterPictures(ctx, SceneId::Ship); });
            idle(ctx);
            timed("c", [&] { testCrossing(ctx); });
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("scene_ship_test: %d checks passed\n", checksPassed);
    return 0;
}
