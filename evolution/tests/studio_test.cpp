// The Studio (Task 10 of step 4): the path-traced room around a 1993 PC whose CRT shows the retro picture and
// lights the room. At 320x180 in Export with 4 spp unless a test says otherwise, with the validation layer
// on; fails on any message it reports. Captures go to <EVO_TEST_OUT_DIR>/step4.
//   (a) STUDIO NIGHT at 30 % and 80 %: lit but not blown out, the screen brighter than the room's mean, each
//       frame under 30 s;
//   (b) the CRT lights the room: a white retro picture against a black one brightens the screen and the desk
//       in front of it, and changes a wall patch the screen cannot see by under 2 % of white;
//   (c) Review Focus 5 in the window: a red picture on frame N, a green one on frame N + 1 -> the screen on
//       frame N + 1 is green (its emission bypasses the temporal history and the filter);
//   (d) studioCamera: finite, smooth within a shot, cutting exactly at the authored times;
//   (e) the scene: 101 key caps on one mesh, every transform finite, within the TLAS's capacity, the lights;
//   (f) sceneFor's mapping, and STUDIO NIGHT running the retro stage for the CRT;
//   (g) the DOS idle screen: C:\DEMO> with BOOT's 2 Hz cursor, a pure function of the retro frame;
//   (h) the CRT stays sharp in the window: every shot that shows the screen up close focuses on its plane
//       with a small aperture; at 1 spp the same frame shown twice barely changes in the screen, and the
//       30th window frame's screen matches a 64 spp export of the same time;
//   (i) the Studio's clampIndirect (2) reaches the path tracer: its lighting equals a trace at 2, not at 64,
//       the default of every other scene.
// With --look <dir> [spp] [width height] it only renders stills of every shot to look at.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "image/png.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/studio.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "render/retro_stage.hpp"
#include "retro/boot.hpp"
#include "retro/dos_idle.hpp"
#include "retro/effects.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace gfx = evo::gfx;
namespace modern = evo::modern;
namespace render = evo::render;
namespace show = evo::show;
using modern::Vec3;
using show::Effect;
using show::TimelineFrame;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

const std::filesystem::path kOutDir = std::filesystem::path(EVO_TEST_OUT_DIR) / "step4";

constexpr int kFinale = 9, kPortal = 10, kStudioNight = 11, kReturn = 19, kReboot = 20;

// A chapter at `fraction` of its length.
TimelineFrame chapterAt(int chapter, double fraction) {
    const double start = show::chapterStart(chapter);
    const double end = show::chapterStart(chapter + 1);
    return show::sampleTimeline(start + fraction * (end - start));
}

// A read-back picture: top-down RGB rows.
struct Picture {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgb;

    const std::uint8_t* at(std::uint32_t x, std::uint32_t y) const {
        return &rgb[(std::size_t{y} * width + x) * 3];
    }
    double luminance(std::uint32_t x, std::uint32_t y) const {
        const std::uint8_t* p = at(x, y);
        return (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) / 255.0;
    }
    double meanLuminance() const {
        double sum = 0;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
                sum += luminance(x, y);
        return sum / (double(width) * height);
    }
    void save(const std::filesystem::path& path) const {
        std::filesystem::create_directories(path.parent_path());
        evo::image::writePng(path, int(width), int(height), rgb.data());
        std::printf("  wrote %s\n", path.string().c_str());
    }
};

// A pixel rectangle, right and bottom exclusive.
struct Rect {
    int left, top, right, bottom;
};

struct Pixel {
    double x, y;
};
Pixel project(const modern::Camera& camera, std::uint32_t width, std::uint32_t height, Vec3 p) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const Vec3 v = p - camera.eye;
    const double z = modern::dot(v, b.forward);
    const double tanHalf = std::tan(camera.fovYDegrees * 3.14159265358979 / 360.0);
    const double aspect = double(width) / height;
    return {(modern::dot(v, b.right) / z / (tanHalf * aspect) + 1) * 0.5 * width,
            (1 - modern::dot(v, b.up) / z / tanHalf) * 0.5 * height};
}

// The screen's projected rectangle shrunk by `inset` (a fraction of its size on each side).
Rect screenRect(const modern::Camera& camera, std::uint32_t width, std::uint32_t height, double inset) {
    double l = 1e9, t = 1e9, r = -1e9, b = -1e9;
    for (const Vec3& corner : modern::studioScreenCorners()) {
        const Pixel p = project(camera, width, height, corner);
        l = std::min(l, p.x);
        r = std::max(r, p.x);
        t = std::min(t, p.y);
        b = std::max(b, p.y);
    }
    const double dx = (r - l) * inset, dy = (b - t) * inset;
    return {int(std::ceil(l + dx)), int(std::ceil(t + dy)), int(std::floor(r - dx)), int(std::floor(b - dy))};
}

// The mean of one channel (0-2) or the luminance (3) over a rectangle, in [0, 1].
double meanOver(const Picture& p, Rect r, int channel = 3) {
    double sum = 0;
    int count = 0;
    for (int y = std::max(r.top, 0); y < std::min(r.bottom, int(p.height)); ++y)
        for (int x = std::max(r.left, 0); x < std::min(r.right, int(p.width)); ++x) {
            sum += channel == 3 ? p.luminance(x, y) : p.at(x, y)[channel] / 255.0;
            ++count;
        }
    if (count == 0)
        throw std::runtime_error("empty rectangle");
    return sum / count;
}

// The rectangle of size `size` pixels around where a world point lands.
Rect around(const modern::Camera& camera, std::uint32_t width, std::uint32_t height, Vec3 p, int size) {
    const Pixel c = project(camera, width, height, p);
    return {int(c.x) - size / 2, int(c.y) - size / 2, int(c.x) + (size + 1) / 2, int(c.y) + (size + 1) / 2};
}

// A retro picture as the retro stage makes it: 320 x 200 R8G8B8A8_SRGB with every mip level, of one colour.
gfx::Image makeRetro(gfx::Context& ctx, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    gfx::Image image(
        ctx, {320, 200, VK_FORMAT_R8G8B8A8_SRGB,
              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
              gfx::mipLevelsFor(320, 200), "test retro picture"});
    gfx::Buffer staging(ctx, {320 * 200 * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "test retro staging"});
    auto* bytes = static_cast<std::uint8_t*>(staging.mapped());
    for (std::size_t i = 0; i < 320 * 200; ++i) {
        bytes[4 * i] = r;
        bytes[4 * i + 1] = g;
        bytes[4 * i + 2] = b;
        bytes[4 * i + 3] = 255;
    }
    staging.flush();
    ctx.immediate([&](VkCommandBuffer cmd) {
        image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {320, 200, 1};
        vkCmdCopyBufferToImage(cmd, staging.handle(), image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        image.generateMips(cmd, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    return image;
}

// The retro stage and the modern stage as the renderer runs them, without the compositor, reading back the
// 2026 picture. The ring is declared first: the stages wait for it when they go.
class Stages {
public:
    Stages(gfx::Context& ctx, std::uint32_t width, std::uint32_t height)
        : ring_(ctx), text_(ctx, ring_), retro_(ctx, ring_, text_), modern_(ctx, ring_, width, height),
          readback_(ctx, width, height) {}

    // One frame: the retro stage (or `retro` instead, when given), then the modern stage.
    Picture render(const TimelineFrame& frame, const render::HistoryState& state,
                   gfx::Image* retro = nullptr) {
        VkCommandBuffer cmd = ring_.begin();
        gfx::Image* screen = retro;
        if (screen == nullptr) {
            retro_.render(cmd, frame);
            screen = &retro_.image();
        }
        modern_.render(cmd, frame, state, screen);
        readback_.record(cmd, modern_.image());
        ring_.wait(ring_.submit());
        return {readback_.width(), readback_.height(), readback_.rgb()};
    }

    render::ModernStage& modern() { return modern_; }

    // One trace of `scene` with the stage's own path tracer (built by an earlier render), nothing else.
    void trace(const modern::SceneFrame& scene, const modern::TraceSettings& settings, gfx::Image& retro) {
        VkCommandBuffer cmd = ring_.begin();
        modern_.studioTracer()->trace(cmd, scene, std::nullopt, settings, retro);
        ring_.wait(ring_.submit());
    }

    // The path tracer's lighting image (RGBA32F), raw bytes. Between frames.
    std::vector<std::uint8_t> lightingBytes(gfx::Context& ctx) {
        gfx::Image& image = modern_.studioTracer()->gbuffer().lighting;
        const VkExtent2D extent = image.extent();
        gfx::Buffer buffer(ctx, {VkDeviceSize(extent.width) * extent.height * 16,
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "studio test lighting readback"});
        ctx.immediate([&](VkCommandBuffer cmd) {
            image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                             VK_ACCESS_2_TRANSFER_READ_BIT);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle(),
                                   1, &region);
        });
        buffer.invalidate();
        const auto* bytes = static_cast<const std::uint8_t*>(buffer.mapped());
        return {bytes, bytes + buffer.size()};
    }

private:
    gfx::FrameRing ring_;
    evo::text::TextPass text_;
    render::RetroStage retro_;
    render::ModernStage modern_;
    gfx::Readback readback_;
};

render::HistoryState exportState(const TimelineFrame& frame, std::uint32_t spp) {
    return {render::HistoryReset::FirstFrame, 0,
            render::RenderMode::Export,       spp,
            render::kExportMaxBounces,        modern::frameSeedFor(frame.showSeconds, 0)};
}

render::HistoryState interactiveState(const TimelineFrame& frame, render::HistoryReset reset,
                                      std::uint32_t framesSinceReset) {
    return {reset,
            framesSinceReset,
            render::RenderMode::Interactive,
            1,
            render::kInteractiveMaxBounces,
            modern::frameSeedFor(frame.showSeconds, framesSinceReset)};
}

constexpr std::uint32_t kW = 320, kH = 180;

// --- (a)
// -----------------------------------------------------------------------------------------------------

void testStudioNightLooks(gfx::Context& ctx) {
    Stages stages(ctx, kW, kH);
    for (const double fraction : {0.3, 0.8}) {
        const auto start = std::chrono::steady_clock::now();
        const TimelineFrame frame = chapterAt(kStudioNight, fraction);
        CHECK(frame.effect == Effect::StudioNight);
        const Picture p = stages.render(frame, exportState(frame, 4));
        const double seconds = secondsSince(start);
        std::size_t clipped = 0;
        for (std::uint32_t y = 0; y < kH; ++y)
            for (std::uint32_t x = 0; x < kW; ++x) {
                const std::uint8_t* px = p.at(x, y);
                clipped += px[0] == 255 || px[1] == 255 || px[2] == 255;
            }
        const modern::Camera camera = modern::studioCamera(frame);
        const double mean = p.meanLuminance();
        const double screen = meanOver(p, screenRect(camera, kW, kH, 0.1));
        const double clippedShare = double(clipped) / (double(kW) * kH);
        std::printf(
            "(a) STUDIO NIGHT at %.0f %%: mean luminance %.3f, screen %.3f, %.2f %% clipped, %.1f s\n",
            fraction * 100, mean, screen, clippedShare * 100, seconds);
        p.save(kOutDir / ("studio_night_" + std::to_string(int(fraction * 100)) + ".png"));
        CHECK(mean > 0.05);
        CHECK(clippedShare < 0.02);
        CHECK(screen > mean);
        CHECK(seconds < 30);
    }
}

// --- (b)
// -----------------------------------------------------------------------------------------------------

void testCrtLightsTheRoom(gfx::Context& ctx) {
    gfx::Image white = makeRetro(ctx, 255, 255, 255);
    gfx::Image black = makeRetro(ctx, 0, 0, 0);
    Stages stages(ctx, kW, kH);
    const TimelineFrame frame = chapterAt(kReturn, 0.2);
    const modern::Camera camera = modern::studioCamera(frame);
    const Picture lit = stages.render(frame, exportState(frame, 4), &white);
    const Picture dark = stages.render(frame, exportState(frame, 4), &black);
    lit.save(kOutDir / "studio_crt_white.png");
    dark.save(kOutDir / "studio_crt_black.png");
    // The desk right of the keyboard, in front of the monitor; the back wall right of the window, which
    // the screen (facing +z, in front of it) cannot see.
    const Rect screen = screenRect(camera, kW, kH, 0.1);
    const Rect desk = around(camera, kW, kH, {0.215f, 0.75f, -0.245f}, 4);
    const Rect wall = around(camera, kW, kH, {-0.45f, 1.75f, -0.95f}, 6);
    const double screenLit = meanOver(lit, screen), screenDark = meanOver(dark, screen);
    const double deskLit = meanOver(lit, desk), deskDark = meanOver(dark, desk);
    const double wallLit = meanOver(lit, wall), wallDark = meanOver(dark, wall);
    std::printf("(b) white vs black picture: screen %.3f / %.3f, desk %.3f / %.3f, wall %.4f / %.4f\n",
                screenLit, screenDark, deskLit, deskDark, wallLit, wallDark);
    CHECK(screenLit >= 1.1 * screenDark && screenLit > screenDark + 0.1);
    CHECK(deskLit >= 1.1 * deskDark);
    // The screen's light reaches the wall only after a bounce: a dark patch, so its change is bounded on the
    // display scale (2 % of white), not relative to its own dim value.
    CHECK(std::abs(wallLit - wallDark) < 0.02);
    CHECK(deskLit - deskDark > 2 * std::abs(wallLit - wallDark));
}

// --- (c)
// -----------------------------------------------------------------------------------------------------

void testScreenFollowsPicture(gfx::Context& ctx) {
    // Dim colours, which the tonemap keeps apart (bright saturated green comes out with a lot of red: ACES
    // mixes the channels in its highlights).
    gfx::Image red = makeRetro(ctx, 40, 0, 0);
    gfx::Image green = makeRetro(ctx, 0, 40, 0);
    gfx::Image black = makeRetro(ctx, 0, 0, 0);
    const TimelineFrame first = chapterAt(kStudioNight, 0.8);
    const TimelineFrame second = show::sampleTimeline(first.showSeconds + 1.0 / 60);
    // Frame N + 1 showing `now` after a history of `before` on frames N - 3 .. N.
    const auto after = [&](gfx::Image& before, gfx::Image& now) {
        Stages stages(ctx, kW, kH);
        CHECK(stages.modern().shotFor(first) == stages.modern().shotFor(second));
        for (std::uint32_t i = 0; i < 4; ++i)
            stages.render(
                first,
                interactiveState(first,
                                 i == 0 ? render::HistoryReset::FirstFrame : render::HistoryReset::None, i),
                &before);
        return stages.render(second, interactiveState(second, render::HistoryReset::None, 4), &now);
    };
    const Picture p = after(red, green);
    const Picture allGreen = after(green, green);
    const Picture allBlack = after(black, black); // the room's own light in the screen's glass
    p.save(kOutDir / "studio_screen_follows.png");
    const Rect screen = screenRect(modern::studioCamera(second), kW, kH, 0.15);
    // The picture's own part of the screen: above the same frame with a black picture all along.
    const double r = meanOver(p, screen, 0) - meanOver(allBlack, screen, 0);
    const double g = meanOver(p, screen, 1) - meanOver(allBlack, screen, 1);
    double difference = 0;
    for (int c = 0; c < 3; ++c)
        difference = std::max(difference, std::abs(meanOver(p, screen, c) - meanOver(allGreen, screen, c)));
    std::printf("(c) the frame after a red history with a green picture: screen red %.3f, green %.3f; "
                "%.4f from a green history\n",
                r, g, difference);
    CHECK(g > 0.1 && g >= 5 * std::max(r, 0.0));
    CHECK(difference < 0.01); // only the coat's reflection of the room may lag
}

// --- (d)
// -----------------------------------------------------------------------------------------------------

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

Vec3 screenCentre() {
    Vec3 sum{0, 0, 0};
    for (const Vec3& corner : modern::studioScreenCorners())
        sum = sum + corner;
    return sum * 0.25f;
}

// Every Studio shot but STUDIO NIGHT's wide dolly is about the screen.
bool screenShot(Effect effect, std::uint32_t shot) {
    return !(effect == Effect::StudioNight && shot == 0);
}

void testCameras() {
    int samples = 0;
    for (const int chapter : {kFinale, kPortal, kStudioNight, kReturn, kReboot}) {
        const TimelineFrame head = show::sampleTimeline(show::chapterStart(chapter));
        CHECK(modern::sceneFor(head.effect) == modern::SceneId::Studio);
        const std::vector<modern::StudioShot> shots = modern::studioShots(head);
        CHECK(shots.size() == (head.effect == Effect::StudioNight ? 2u : 1u));
        CHECK(shots.front().start == 0.f && std::abs(shots.back().end - head.chapterDuration) < 1e-4);
        std::optional<modern::Camera> previous;
        const double duration = show::chapterStart(chapter + 1) - show::chapterStart(chapter);
        for (double local = 0; local < duration; local += 1.0 / 60) {
            const TimelineFrame frame = show::sampleTimeline(show::chapterStart(chapter) + local);
            if (frame.chapter != chapter)
                break;
            const modern::Camera c = modern::studioCamera(frame);
            CHECK(finite(c.eye) && finite(c.target) && std::isfinite(c.fovYDegrees) && c.focusDistance > 0);
            CHECK(modern::length(c.target - c.eye) > 0.05f);
            std::uint32_t expectedShot = 0;
            for (std::size_t i = 1; i < shots.size(); ++i)
                expectedShot += static_cast<float>(frame.localSeconds) >= shots[i].start ? 1 : 0;
            CHECK(c.shot == expectedShot);
            if (screenShot(frame.effect, c.shot)) {
                // In focus on the screen's plane (the thin lens focuses on a plane at focusDistance along
                // the view), with a small aperture: the CRT's picture stays sharp.
                const modern::CameraBasis b = modern::cameraBasis(c);
                CHECK(std::abs(c.focusDistance - modern::dot(screenCentre() - c.eye, b.forward)) < 1e-4f);
                CHECK(c.aperture <= 0.004f);
            }
            if (previous && previous->shot == c.shot) {
                CHECK(modern::length(c.eye - previous->eye) < 0.05f);
                CHECK(modern::length(c.target - previous->target) < 0.05f);
            }
            previous = c;
            ++samples;
        }
    }
    // STUDIO NIGHT cuts exactly at its accent.
    const TimelineFrame night = show::sampleTimeline(show::chapterStart(kStudioNight));
    const double cut = show::chapterStart(kStudioNight) + night.accentSeconds;
    CHECK(modern::studioCamera(show::sampleTimeline(cut - 1e-3)).shot == 0);
    CHECK(modern::studioCamera(show::sampleTimeline(cut + 1e-3)).shot == 1);
    std::printf("(d) cameras: %d frames finite and smooth, STUDIO NIGHT cuts at %.2f s\n", samples,
                night.accentSeconds);
}

// --- (e)
// -----------------------------------------------------------------------------------------------------

float determinant(const evo::rt::Mat34& m) {
    const Vec3 r0{m.m[0][0], m.m[0][1], m.m[0][2]}, r1{m.m[1][0], m.m[1][1], m.m[1][2]},
        r2{m.m[2][0], m.m[2][1], m.m[2][2]};
    return modern::dot(r0, modern::cross(r1, r2));
}

void testScene() {
    const modern::Studio studio;
    const TimelineFrame frame = chapterAt(kStudioNight, 0.5);
    const modern::SceneFrame f = studio.frame(frame);
    const modern::SceneStatic& statics = studio.staticScene();
    int keys = 0;
    for (const modern::SceneInstance& instance : f.instances) {
        keys += instance.mesh == modern::Studio::kKeyCapMesh;
        CHECK(instance.mesh < statics.meshes.size() && instance.material < statics.materials.size());
        for (const auto& row : instance.transform.m)
            for (float v : row)
                CHECK(std::isfinite(v));
        CHECK(determinant(instance.transform) > 0.f);
    }
    CHECK(keys == int(modern::Studio::kKeyCount));
    CHECK(f.instances.size() <= modern::PathTracer::kMaxInstances);
    CHECK(f.lights.size() == modern::Studio::kLightCount &&
          f.lights.size() <= modern::PathTracer::kMaxLights);
    // Each light: its area, its kind, and (but the window) the one instance whose surface it is, with the
    // same emission as its material.
    int portals = 0;
    for (std::uint32_t i = 0; i < f.lights.size(); ++i) {
        const evo::rt::RectLight& l = f.lights[i];
        const Vec3 u{l.edgeU[0], l.edgeU[1], l.edgeU[2]}, v{l.edgeV[0], l.edgeV[1], l.edgeV[2]};
        CHECK(std::abs(l.area - modern::length(modern::cross(u, v))) <= 1e-6f * l.area);
        CHECK(l.selectWeight > 0.f);
        int surfaces = 0;
        for (const modern::SceneInstance& instance : f.instances) {
            if (instance.light != i)
                continue;
            ++surfaces;
            const evo::rt::Material& m = statics.materials[instance.material];
            CHECK(m.materialClass == std::uint32_t(evo::rt::MaterialClass::Emissive));
            for (int c = 0; c < 3; ++c)
                CHECK(std::abs(l.emission[c] - (l.kind == std::uint32_t(evo::rt::LightKind::RetroScreen)
                                                    ? m.emissionScale
                                                    : m.emission[c] * m.emissionScale)) < 1e-4f);
        }
        portals += l.kind == std::uint32_t(evo::rt::LightKind::Portal);
        CHECK(surfaces == (l.kind == std::uint32_t(evo::rt::LightKind::Portal) ? 0 : 1));
    }
    CHECK(portals == 1);
    CHECK(f.lights[modern::Studio::kScreenLight].kind == std::uint32_t(evo::rt::LightKind::RetroScreen));
    CHECK(f.lights[modern::Studio::kLampLight].kind == std::uint32_t(evo::rt::LightKind::Emitter));
    CHECK(f.lights[modern::Studio::kWindowLight].kind == std::uint32_t(evo::rt::LightKind::Portal));
    // The screen light's rectangle is studioScreenCorners().
    const auto corners = modern::studioScreenCorners();
    const evo::rt::RectLight& screen = f.lights[modern::Studio::kScreenLight];
    const Vec3 corner{screen.corner[0], screen.corner[1], screen.corner[2]};
    CHECK(modern::length(corner - corners[3]) < 1e-5f); // bottom left
    std::printf("(e) scene: %zu instances (%d key caps), %zu meshes, %zu materials, %zu lights\n",
                f.instances.size(), keys, statics.meshes.size(), statics.materials.size(), f.lights.size());
}

// --- (f)
// -----------------------------------------------------------------------------------------------------

void testRegistry(gfx::Context& ctx) {
    for (std::size_t i = 0; i < show::kEffectCount; ++i) {
        const auto effect = static_cast<Effect>(i);
        const bool studio = effect == Effect::Finale || effect == Effect::Portal ||
                            effect == Effect::StudioNight || effect == Effect::Return ||
                            effect == Effect::Reboot;
        CHECK(modern::sceneFor(effect) == (studio ? modern::SceneId::Studio : modern::SceneId::Placeholder));
        CHECK((modern::makeScene(modern::sceneFor(effect)) != nullptr) == studio);
    }
    // STUDIO NIGHT is 2026 only, yet the renderer runs the retro stage for the CRT.
    gfx::FrameRing ring(ctx);
    render::Renderer renderer(ctx, ring, {160, 90});
    const TimelineFrame frame = chapterAt(kStudioNight, 0.3);
    VkCommandBuffer cmd = ring.begin();
    renderer.render(cmd, frame);
    ring.wait(ring.submit());
    CHECK(renderer.lastStats().retroRendered && renderer.lastStats().modernRendered);
    std::printf("(f) registry: 5 Studio chapters; STUDIO NIGHT renders retro frame %u for the CRT\n",
                renderer.lastStats().retroFrame);
}

// --- (g)
// -----------------------------------------------------------------------------------------------------

void testDosIdle() {
    namespace retro = evo::retro;
    for (const Effect effect : {Effect::StudioNight, Effect::Return}) {
        const retro::EffectInfo& info = retro::effectFor(effect);
        CHECK(info.text == &retro::dosIdleText && info.shader == nullptr);
        CHECK(info.retroRate == retro::kBootRetroRate);
    }
    const TimelineFrame a = chapterAt(kStudioNight, 0.2), b = chapterAt(kReturn, 0.6);
    int toggles = 0;
    bool last = true;
    for (std::uint32_t f = 0; f < 35 * 4; ++f) {
        const std::vector<std::string> screen = retro::dosIdleScreen(a, f);
        CHECK(screen.back() == retro::kDosIdlePrompt);
        for (const std::string& row : screen)
            CHECK(row.size() <= retro::kBootColumns);
        const std::vector<evo::text::Item> items = retro::dosIdleText(a, f);
        const bool cursor = items.back().text == "_";
        CHECK(cursor == retro::promptCursorOn(f));
        if (cursor) // in the cell right after the prompt, on its row
            CHECK(items.back().x ==
                      float(retro::kBootMargin + 8 * std::string(retro::kDosIdlePrompt).size()) &&
                  items.back().y == float(retro::kBootMargin + retro::kBootLineHeight * (screen.size() - 1)));
        toggles += cursor != last;
        last = cursor;
        // Pure in the retro frame: another time, another chapter, the same picture.
        const std::vector<evo::text::Item> again = retro::dosIdleText(b, f);
        CHECK(again.size() == items.size());
        for (std::size_t i = 0; i < items.size(); ++i)
            CHECK(again[i].text == items[i].text && again[i].x == items[i].x && again[i].y == items[i].y);
    }
    CHECK(toggles == 15); // 2 Hz over 4 s from on: 8 times off, 7 times on again
    std::printf("(g) DOS idle: %s with a 2 Hz cursor\n", retro::kDosIdlePrompt);
}

// --- (h)
// -----------------------------------------------------------------------------------------------------

// The mean absolute difference of two pictures over a rectangle, all channels, in [0, 1].
double meanAbsDifference(const Picture& a, const Picture& b, Rect r) {
    double sum = 0;
    int count = 0;
    for (int y = std::max(r.top, 0); y < std::min(r.bottom, int(a.height)); ++y)
        for (int x = std::max(r.left, 0); x < std::min(r.right, int(a.width)); ++x)
            for (int c = 0; c < 3; ++c) {
                sum += std::abs(int(a.at(x, y)[c]) - int(b.at(x, y)[c])) / 255.0;
                ++count;
            }
    return sum / count;
}

// The local contrast over a rectangle: the mean absolute luminance step to the right and down neighbours.
double localContrast(const Picture& p, Rect r) {
    double sum = 0;
    int count = 0;
    for (int y = std::max(r.top, 0); y + 1 < std::min(r.bottom, int(p.height)); ++y)
        for (int x = std::max(r.left, 0); x + 1 < std::min(r.right, int(p.width)); ++x) {
            sum += std::abs(p.luminance(x + 1, y) - p.luminance(x, y)) +
                   std::abs(p.luminance(x, y + 1) - p.luminance(x, y));
            ++count;
        }
    return sum / count;
}

// The window's frames 1 .. count of a run ending at `last`, 1 spp with the temporal history, as the window
// plays them (a reset on the first frame and at a shot cut); returns the last.
Picture playWindow(Stages& stages, const TimelineFrame& last, std::uint32_t count) {
    Picture p;
    std::uint32_t sinceReset = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const TimelineFrame f = show::sampleTimeline(last.showSeconds - (count - 1 - i) / 60.0);
        const bool reset =
            i == 0 || modern::studioCamera(f).shot !=
                          modern::studioCamera(show::sampleTimeline(f.showSeconds - 1 / 60.0)).shot;
        sinceReset = reset ? 0 : sinceReset + 1;
        p = stages.render(
            f, interactiveState(f, reset ? render::HistoryReset::FirstFrame : render::HistoryReset::None,
                                sinceReset));
    }
    return p;
}

void testWindowScreenSharp(gfx::Context& ctx) {
    // STUDIO NIGHT's orbit around the monitor, the DOS prompt on the screen.
    const TimelineFrame still = chapterAt(kStudioNight, 0.8);
    const modern::Camera camera = modern::studioCamera(still);
    CHECK(camera.shot == 1);
    Stages window(ctx, kW, kH);
    const Picture frame30 = playWindow(window, still, 30);
    // The same frame again (a repeated frame, dt = 0: the history goes on, a new seed): the camera and the
    // retro frame are unchanged, so only sampling noise can change the screen.
    const Picture frame31 = window.render(still, interactiveState(still, render::HistoryReset::None, 30));
    Stages exporter(ctx, kW, kH);
    const Picture reference = exporter.render(still, exportState(still, 64));
    frame30.save(kOutDir / "studio_window_orbit_30.png");
    reference.save(kOutDir / "studio_window_orbit_export.png");
    const Rect screen = screenRect(camera, kW, kH, 0.1);
    const double flicker = meanAbsDifference(frame30, frame31, screen);
    const double error = meanAbsDifference(frame30, reference, screen);
    std::printf(
        "(h) window orbit screen: frame to frame %.4f, from a 64 spp export %.4f, local contrast %.3f "
        "(export %.3f)\n",
        flicker, error, localContrast(frame30, screen), localContrast(reference, screen));
    // Mean absolute differences over the screen on the display scale (1 = 255 levels). A jittered single
    // tap at 1 spp changed it by about 0.035 a frame and stayed 0.045 from the export.
    CHECK(flicker < 0.01);
    CHECK(error < 0.025);
}

// --- (i)
// -----------------------------------------------------------------------------------------------------

void testClampIndirect(gfx::Context& ctx) {
    CHECK(modern::SceneFrame{}.clampIndirect == 64.f && modern::TraceSettings{}.clampIndirect == 64.f);
    const modern::Studio studio;
    // FINALE's pull-out passes the chrome and glass spheres and the lamp: plenty of caustic paths above 2.
    const TimelineFrame frame = chapterAt(kFinale, 0.5);
    const modern::SceneFrame scene = studio.frame(frame);
    CHECK(scene.clampIndirect == 2.f);
    gfx::Image white = makeRetro(ctx, 255, 255, 255);
    Stages stages(ctx, 160, 90);
    const render::HistoryState state = exportState(frame, 4);
    stages.render(frame, state, &white);
    const std::vector<std::uint8_t> staged = stages.lightingBytes(ctx);
    modern::TraceSettings settings{state.spp, state.maxBounces, state.frameSeed};
    settings.clampIndirect = 2.f;
    stages.trace(scene, settings, white);
    const std::vector<std::uint8_t> clamped = stages.lightingBytes(ctx);
    settings.clampIndirect = 64.f;
    stages.trace(scene, settings, white);
    const std::vector<std::uint8_t> loose = stages.lightingBytes(ctx);
    CHECK(staged == clamped);
    CHECK(clamped != loose);
    std::printf("(i) the Studio traces with clampIndirect 2 (64 changes its lighting)\n");
}

// --- --look ------------------------------------------------------------------------------------------------

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp, std::uint32_t width,
                std::uint32_t height, const std::string& only) {
    struct Still {
        const char* name;
        int chapter;
        double fraction;
    };
    const std::array<Still, 9> stills = {{{"finale_start", kFinale, 0.05},
                                          {"finale_end", kFinale, 0.92},
                                          {"portal_mid", kPortal, 0.5},
                                          {"night_wide", kStudioNight, 0.08},
                                          {"night_dolly", kStudioNight, 0.3},
                                          {"night_orbit", kStudioNight, 0.8},
                                          {"return_mid", kReturn, 0.5},
                                          {"return_end", kReturn, 0.95},
                                          {"reboot", kReboot, 0.5}}};
    Stages stages(ctx, width, height);
    for (const Still& still : stills) {
        if (!only.empty() && std::string(still.name).find(only) == std::string::npos)
            continue;
        const auto start = std::chrono::steady_clock::now();
        const TimelineFrame frame = chapterAt(still.chapter, still.fraction);
        if (spp == 0) {
            // The window: 1 spp per frame, temporal history; frames 1 and 30 of a run ending at the still.
            for (std::uint32_t i = 0; i < 30; ++i) {
                const TimelineFrame f = show::sampleTimeline(frame.showSeconds - (29 - i) / 60.0);
                const bool reset =
                    i == 0 || modern::studioCamera(f).shot !=
                                  modern::studioCamera(show::sampleTimeline(f.showSeconds - 1 / 60.0)).shot;
                const Picture p = stages.render(
                    f, interactiveState(
                           f, reset ? render::HistoryReset::FirstFrame : render::HistoryReset::None, i));
                if (i == 0 || i == 29)
                    p.save(dir /
                           (std::string("window_") + still.name + (i == 0 ? "_first" : "_30") + ".png"));
            }
            continue;
        }
        const Picture p = stages.render(frame, exportState(frame, spp));
        p.save(dir / (std::string("studio_") + still.name + ".png"));
        std::printf("  %s: %.1f s, mean luminance %.3f\n", still.name, secondsSince(start),
                    p.meanLuminance());
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
    try {
        auto ctx = gfx::Context::create({.validation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the pictures are checked\n");
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            const std::uint32_t spp = argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 64u;
            const std::uint32_t w = argc >= 6 ? std::uint32_t(std::stoul(argv[4])) : 640u;
            const std::uint32_t h = argc >= 6 ? std::uint32_t(std::stoul(argv[5])) : 360u;
            renderLook(ctx, argv[2], spp, w, h, argc >= 7 ? argv[6] : "");
            CHECK(ctx.validationMessageCount() == 0);
            return 0;
        }
        testCameras();
        testScene();
        testDosIdle();
        testRegistry(ctx);
        testStudioNightLooks(ctx);
        testCrtLightsTheRoom(ctx);
        testScreenFollowsPicture(ctx);
        testWindowScreenSharp(ctx);
        testClampIndirect(ctx);
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("studio_test: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
