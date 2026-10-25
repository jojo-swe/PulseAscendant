// The Ship scene (FLYBY's 2026 side, step 5 Task 16), with synchronization validation:
//   - the card's numbers: 262,144 stars, two plumes of 96^3 voxels, about 40,000 triangles, at full detail;
//   - the CPU sweep over every 1/60 s (cameras, shots, capacities, transforms, lights);
//   - the 1993 flight: the ship's attitude is flybyAttitude, and from 1.2 s before the accent (the portal
//     opens a second before it) the camera relative to the ship is flybyView's, scaled to metres;
//   - the per-frame materials: the panel lines brighten with the kick, the strobe flashes on the beat;
//   - the standard chapter pictures (20 %, the accent, 80 %; the accent twice, byte for byte);
//   - the crossing in export at spp 8: with the 1/120 s shutter the ship's silhouette spreads at least 2 px
//     further along its screen velocity than without; the volume layer behind the nozzles holds the plumes
//     (transmittance below 0.95, radiance above 0).

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

int flybyChapter() {
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

// --- The card and the flight (CPU) ----------------------------------------------------------------------------

void testCard() {
    const modern::ShipStats full = modern::shipStats({1.f});
    CHECK(full.stars == 262144 && modern::kShipStars == 262144);
    CHECK(full.plumeVoxels == 2u * 96u * 96u * 96u);
    CHECK(full.triangles >= 35000 && full.triangles <= 50000);
    CHECK(std::string(modern::kShipCardLine).find("262 144 STARS") == 0);
    const modern::ShipStats small = modern::shipStats({1.f / 256});
    CHECK(small.stars == 1024 && small.triangles == full.triangles);
    std::printf("card: %u stars, %u limb splats, %u plume voxels, %u triangles in %u meshes; \"%s\"\n", full.stars,
                full.limbSplats, full.plumeVoxels, full.triangles, full.meshes, modern::kShipCardLine);
}

void testSweep() {
    evo::test::SweepLimits limits;
    limits.maxCameraStep = 2.5f; // the crossing camera stands still in the world: 100 m/s past the ship
    const evo::test::SweepStats stats = evo::test::checkSceneSweep(SceneId::Ship, limits);
    CHECK(stats.shots == 3);
    CHECK(stats.frames > 1000);
}

// The 1993 flight: the attitude everywhere, the camera from 1.2 s before the accent to the accent.
void testFlightMatches1993() {
    const int chapter = flybyChapter();
    const double start = show::chapterStart(chapter);
    const show::TimelineFrame head = show::sampleTimeline(start);
    int matched = 0;
    for (double local = 0; local < head.chapterDuration; local += 1.0 / 60) {
        const modern::ShipPose pose = modern::shipPose(local);
        const retro::FlybyAttitude a = retro::flybyAttitude(local);
        for (int i = 0; i < 3; ++i) {
            CHECK(std::abs(pose.rows[i].x - a.m[i][0]) < 1e-6 && std::abs(pose.rows[i].y - a.m[i][1]) < 1e-6 &&
                  std::abs(pose.rows[i].z - a.m[i][2]) < 1e-6);
        }
        if (local < head.accentSeconds - 1.2 || local >= head.accentSeconds)
            continue;
        const show::TimelineFrame frame = show::sampleTimeline(start + local);
        const modern::Camera world = modern::shipWorldCamera(frame);
        const retro::FlybyView view = retro::flybyView(frame.chapterDuration, frame.localSeconds * 35.0);
        const Vec3 eye = (world.eye - pose.position) / modern::kShipScale;
        const Vec3 target = (world.target - pose.position) / modern::kShipScale;
        const float error = std::max(length(eye - Vec3{float(view.eye[0]), float(view.eye[1]), float(view.eye[2])}),
                                     length(target - Vec3{float(view.target[0]), float(view.target[1]),
                                                          float(view.target[2])}));
        if (!(error < 2e-4f))
            throw std::runtime_error("at " + std::to_string(local) + " s the camera is " + std::to_string(error) +
                                     " units off the 1993 camera");
        CHECK(world.fovYDegrees == float(retro::kFlybyFovY) && world.up.y == 1.f);
        ++matched;
    }
    CHECK(matched >= 60);
    std::printf("flight: attitude = 1993 over the chapter; camera = 1993's on %d frames before the accent\n",
                matched);
}

void testMusicMaterials() {
    const int chapter = flybyChapter();
    const modern::Ship ship({1.f / 256});
    show::TimelineFrame f = show::sampleTimeline(show::chapterStart(chapter) + 5.0);
    f.kick = 0.f;
    f.beatPhase = 0.5f;
    const auto quiet = ship.materials(f);
    f.kick = 1.f;
    f.beatPhase = 0.02f;
    const auto loud = ship.materials(f);
    CHECK(loud[ship.panelMaterial()].emissionScale > 3.f * quiet[ship.panelMaterial()].emissionScale);
    CHECK(quiet[ship.strobeMaterial()].emissionScale == 0.f && loud[ship.strobeMaterial()].emissionScale > 0.f);
    // A frame carries the per-frame table.
    CHECK(ship.frame(f).materials.size() == ship.staticScene().materials.size());
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
    gfx::Buffer buffer(ctx, {VkDeviceSize(extent.width) * extent.height * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                             "ship test readback"});
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle(), 1, &region);
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

// One export frame of the show's Ship at spp 8, with or without the shutter: its HDR picture and volume layer.
Pictures renderExport(gfx::Context& ctx, double seconds, double shutter) {
    gfx::FrameRing ring(ctx);
    render::RenderSettings s{kW, kH};
    s.hud = false;
    s.mode = render::RenderMode::Export;
    s.spp = 8;
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

// The extent along `dir` of the bright region (luminance above `threshold`) 4-connected to the pixel nearest
// `seed` that is above it.
float spread(const Plane& p, Pixel seed, float dx, float dy, float threshold) {
    const int w = int(p.width), h = int(p.height);
    int sx = -1, sy = -1;
    float best = 1e9f;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (p.luminance(x, y) > threshold) {
                const float d = std::hypot(x + 0.5f - seed.x, y + 0.5f - seed.y);
                if (d < best)
                    best = d, sx = x, sy = y;
            }
    if (sx < 0 || best > 6.f)
        throw std::runtime_error("no bright ship near its projected centre");
    std::vector<char> seen(std::size_t(w) * h, 0);
    std::vector<std::pair<int, int>> stack{{sx, sy}};
    seen[std::size_t(sy) * w + sx] = 1;
    float lo = 1e9f, hi = -1e9f;
    while (!stack.empty()) {
        const auto [x, y] = stack.back();
        stack.pop_back();
        const float along = (x + 0.5f) * dx + (y + 0.5f) * dy;
        lo = std::min(lo, along);
        hi = std::max(hi, along);
        for (const auto [nx, ny] : {std::pair{x + 1, y}, std::pair{x - 1, y}, std::pair{x, y + 1}, std::pair{x, y - 1}}) {
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || seen[std::size_t(ny) * w + nx])
                continue;
            if (p.luminance(nx, ny) > threshold) {
                seen[std::size_t(ny) * w + nx] = 1;
                stack.push_back({nx, ny});
            }
        }
    }
    return hi - lo;
}

void testCrossing(gfx::Context& ctx) {
    const int chapter = flybyChapter();
    const double start = show::chapterStart(chapter);
    const show::TimelineFrame head = show::sampleTimeline(start);
    // A moment just before the ship passes closest, its whole silhouette in the picture.
    const double local = head.accentSeconds + modern::kShipPassSeconds - 0.06;
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
    std::printf("crossing at %.3f s: centre (%.1f, %.1f), %.1f px per 1/120 s\n", local, centre.x, centre.y, px120);

    const Pictures sharp = renderExport(ctx, seconds, 0.0);
    const Pictures blurred = renderExport(ctx, seconds, 1.0 / 120);
    // Above the black of space, well below the sunlit hull.
    const float threshold = 0.01f;
    const float a = spread(sharp.hdr, centre, vx / speed, vy / speed, threshold);
    const float b = spread(blurred.hdr, centre, vx / speed, vy / speed, threshold);
    std::printf("crossing: silhouette spread %.1f px sharp, %.1f px with the 1/120 s shutter\n", a, b);
    CHECK(b >= a + 2.f);

    // The plumes: behind each projected nozzle, the volume layer holds the fire.
    int checked = 0;
    for (const Vec3 nozzle : modern::shipNozzles()) {
        const Pixel p = project(camera, nozzle + Vec3{0, 0, -0.6f * modern::kShipScale});
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

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testCard();
        testSweep();
        testFlightMatches1993();
        testMusicMaterials();
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("SKIP sync validation: unavailable\n");
            evo::test::checkChapterPictures(ctx, SceneId::Ship);
            idle(ctx);
            testCrossing(ctx);
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
