// The Studio (Task 10 of step 4, polished by Task 14 of step 5): the path-traced room around a 1993 PC whose
// CRT shows the retro picture and lights the room. At 320x180 in Export with 4 spp unless a test says
// otherwise, with the validation layer on; fails on any message it reports. Captures go to
// <EVO_TEST_OUT_DIR>/step4.
//   (a) STUDIO NIGHT 0.6 s and 4 s in (its opening frame and the night wide): lit but not blown out, the
//       screen brighter than the room's mean, each frame under 30 s;
//   (b) the CRT is STUDIO NIGHT's key light (Task 14 (a)): a white retro picture against a black one
//   brightens
//       the screen, and the desk in front of it by at least 40 % (step 4: 10 % in RETURN), and changes a wall
//       patch the screen cannot see by under 0.002 in linear display light (step 4's bound
//       of 0.02 was written for encoded values);
//   (c) in the window: a red picture on frame N, a green one on frame N + 1 -> the screen on
//       frame N + 1 is green (its emission bypasses the temporal history and the filter);
//   (d) studioCamera: finite, smooth within a shot, cutting exactly at the authored times, on the four
//   lenses; (e) the scene: 101 key caps on one mesh, every transform finite, within the TLAS's capacity, the
//   lights; (f) sceneFor's mapping, and STUDIO NIGHT running the retro stage for the CRT; (g) the DOS idle
//   screen: C:\DEMO> with BOOT's 2 Hz cursor, a pure function of the retro frame; (h) the CRT stays sharp in
//   the window (RETURN's close-up): every shot that shows the screen focuses on its plane
//       with a small aperture; at 1 spp the same frame shown twice barely changes in the screen, and the
//       30th window frame's screen matches a 32 spp export of the same time (64 until Task 14 of step 5,
//       which halved it for the time budget: the screen differs from either by 0.003, the bound is 0.025);
//   (i) the Studio's clampIndirect (0.5; step 4: 2) reaches the path tracer: its lighting equals a trace at
//       0.5, not at 16, the default of every other scene (since Task 6 of step 5);
// Task 14 of step 5 (its letters in brackets):
//   (j) [b] the light rig: the lamp's material emits nothing in STUDIO NIGHT and does in RETURN and REBOOT;
//       in every chapter the frame's material table carries studioRig's levels and every light's emission is
//       its material's (the screen's (s, s, s));
//   (k) [c] speckle: the STUDIO NIGHT wide shot at 160x90 and 32 spp has under 1 % of its pixels more than 10
//       levels above their 3x3 neighbourhood's median (step 4: 3-5 % at 192 spp);
//   (l) [d] the chrome sphere reflects the screen: with an all-red picture its projected disc holds at least
//       20 red-dominant pixels;
//   (m) [e] the power LEDs are camera-only emitters: the lighting of the desk beside the case's LED is byte
//       for byte the same with the LEDs on and off;
//   (n) [g] the moonbeam: in the STUDIO NIGHT wide shot the floor where the moon's ray through the window's
//       centre lands is at least twice as bright as the floor beside the beam.
// [f] is (h) and renderer_test's Studio resize case, unchanged.
// Step 6, the Portal crossings and the Studio's camera:
//   (o) FLYBY: the screen spans at least 98 % of the frame's width on D, the camera is 1.2 m out by A + 1 s;
//       PORTAL crosses the screen's plane inward on D and outward on the bar two bars on (each within a
//       frame, inside the picture's rectangle), and turns at least 175 degrees over the span centred on its
//       accent, fastest within a beat of it; STUDIO NIGHT passes the window's pane on its accent (within a
//       frame, clear of the glazing bars); REBOOT reaches the glass on D (within a frame);
//   (p) the moves: no straight constant-speed stretch inside a move (the eye's second difference never stays
//       0 for a quarter second while it moves), lenses only 24, 35 and 50 mm; every shot change on a tracked
//       bar line (within 1 ms) and outside the two beats before its chapter's accent;
//   (q) the handheld: within its limits over the whole show, 0 within 0.5 s of every glass, its spectrum over
//       60 s peaking within 0.4 - 1.1 Hz on every channel;
//   (r) behind the screen: PORTAL 0.6 s after its inward crossing shows at least 200 star pixels at 480x270;
//       at 191.0 (facing the back of the glass) the room shows through it (the frame's mean above 0.03);
//       checkChapterPictures passes for the Studio's six chapters.
// With --look <dir> [spp] [width height] it only renders stills of every shot to look at; with --only
// <letters> it runs just those checks.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "image/png.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene_gpu.hpp"
#include "modern/studio.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "render/retro_stage.hpp"
#include "retro/boot.hpp"
#include "retro/dos_idle.hpp"
#include "retro/effects.hpp"
#include "scene_fixture.hpp"
#include "show/era.hpp"
#include "show/music_curves.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

constexpr int kFlyby = 1, kFinale = 9, kPortal = 10, kStudioNight = 11, kReturn = 19, kReboot = 20;

// STUDIO NIGHT `seconds` into the chapter.
TimelineFrame nightAt(double seconds) {
    return show::sampleTimeline(show::chapterStart(kStudioNight) + seconds);
}

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

// The mean Rec. 709 luminance of the display's linear light (the sRGB EOTF of the bytes) over a rectangle, in
// [0, 1]: how much light a region shows, independent of how steep the encoding is near black.
double linearOver(const Picture& p, Rect r) {
    const auto decode = [](std::uint8_t byte) {
        const double v = byte / 255.0;
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    double sum = 0;
    int count = 0;
    for (int y = std::max(r.top, 0); y < std::min(r.bottom, int(p.height)); ++y)
        for (int x = std::max(r.left, 0); x < std::min(r.right, int(p.width)); ++x) {
            const std::uint8_t* px = p.at(x, y);
            sum += 0.2126 * decode(px[0]) + 0.7152 * decode(px[1]) + 0.0722 * decode(px[2]);
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
        modern_.tracer()->trace(cmd, *modern_.sceneGpu(modern::SceneId::Studio), scene, std::nullopt,
                                settings, &retro);
        ring_.wait(ring_.submit());
    }

    // The path tracer's lighting image (RGBA32F), raw bytes. Between frames.
    std::vector<std::uint8_t> lightingBytes(gfx::Context& ctx) {
        gfx::Image& image = modern_.tracer()->gbuffer().lighting;
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
    return {
        reset,
        framesSinceReset,
        render::RenderMode::Interactive,
        render::frameSpp(render::RenderMode::Interactive, 1, framesSinceReset), // as the Renderer: 4, 2, 1
        render::kInteractiveMaxBounces,
        modern::frameSeedFor(frame.showSeconds, framesSinceReset)};
}

constexpr std::uint32_t kW = 320, kH = 180;

// --- (a)
// -----------------------------------------------------------------------------------------------------

void testStudioNightLooks(gfx::Context& ctx) {
    Stages stages(ctx, kW, kH);
    stages.render(nightAt(0.6), exportState(nightAt(0.6), 1)); // builds the scene and the pipelines, untimed
    for (const double at : {0.6, 4.0}) {
        const auto start = std::chrono::steady_clock::now();
        const TimelineFrame frame = nightAt(at);
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
        std::printf("(a) STUDIO NIGHT at %.1f s: mean luminance %.3f, screen %.3f, %.2f %% clipped, %.1f s\n",
                    at, mean, screen, clippedShare * 100, seconds);
        p.save(kOutDir / ("studio_night_" + std::to_string(int(at * 10)) + ".png"));
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
    // STUDIO NIGHT's dolly: the lamp is off, the CRT is the key light.
    const TimelineFrame frame = nightAt(0.6);
    CHECK(modern::studioCamera(frame).shot == 0);
    const modern::Camera camera = modern::studioCamera(frame);
    const Picture lit = stages.render(frame, exportState(frame, 4), &white);
    const Picture dark = stages.render(frame, exportState(frame, 4), &black);
    lit.save(kOutDir / "studio_crt_white.png");
    dark.save(kOutDir / "studio_crt_black.png");
    // The desk between the keyboard and the case, in front of the monitor; the back wall right of the
    // window, behind the lamp's arm, which the screen (facing +z, in front of it) cannot see.
    const Rect screen = screenRect(camera, kW, kH, 0.1);
    const Rect desk = around(camera, kW, kH, {0.1f, 0.75f, -0.27f}, 3);
    const Rect wall = around(camera, kW, kH, {-0.5f, 1.0f, -0.95f}, 6);
    // Measured in the display's linear light: PBR Neutral lifts dim coloured surfaces (its toe acts on the
    // smallest channel only), where the encoding is steepest, so encoded values would understate the desk's
    // gain and overstate the wall's.
    const double screenLit = linearOver(lit, screen), screenDark = linearOver(dark, screen);
    const double deskLit = linearOver(lit, desk), deskDark = linearOver(dark, desk);
    const double wallLit = linearOver(lit, wall), wallDark = linearOver(dark, wall);
    std::printf("(b) white vs black picture (linear): screen %.3f / %.3f, desk %.4f / %.4f (x%.2f), wall "
                "%.4f / %.4f\n",
                screenLit, screenDark, deskLit, deskDark, deskLit / deskDark, wallLit, wallDark);
    CHECK(screenLit >= 1.1 * screenDark && screenLit > screenDark + 0.1);
    CHECK(deskLit >= 1.4 * deskDark);
    // The screen's light reaches the wall only after a bounce: a dark patch, so its change is bounded on the
    // display's linear scale, not relative to its own dim value.
    CHECK(std::abs(wallLit - wallDark) < 0.002);
    CHECK(deskLit - deskDark > 2 * std::abs(wallLit - wallDark));
}

// --- (c)
// -----------------------------------------------------------------------------------------------------

void testScreenFollowsPicture(gfx::Context& ctx) {
    // Dim colours, which the tonemap keeps apart (bright saturated green comes out with red and blue lifted:
    // PBR Neutral desaturates its highlights towards white).
    gfx::Image red = makeRetro(ctx, 40, 0, 0);
    gfx::Image green = makeRetro(ctx, 0, 40, 0);
    gfx::Image black = makeRetro(ctx, 0, 0, 0);
    const TimelineFrame first = nightAt(0.6);
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

// Whether a camera has the screen's centre in its frame, in front of it.
bool seesScreen(const modern::Camera& c) {
    const modern::CameraBasis b = modern::cameraBasis(c);
    const Vec3 v = screenCentre() - c.eye;
    const float z = modern::dot(v, b.forward);
    if (z <= 0.05f)
        return false;
    const float tanHalf = std::tan(c.fovYDegrees * 3.14159265f / 360.f);
    return std::abs(modern::dot(v, b.up) / z) < tanHalf &&
           std::abs(modern::dot(v, b.right) / z) < tanHalf * 16.f / 9.f;
}

// The shots that are about the screen (all but STUDIO NIGHT's dolly and RETURN's crane), wherever they see
// it.
bool screenShot(Effect effect, std::uint32_t shot) {
    return effect != Effect::StudioNight && !(effect == Effect::Return && shot == 0) &&
           !(effect == Effect::Portal && shot == 1);
}

constexpr std::array<int, 6> kStudioChapters = {kFlyby, kFinale, kPortal, kStudioNight, kReturn, kReboot};

std::size_t expectedShots(Effect effect) {
    return effect == Effect::Portal ? 3u : effect == Effect::Return ? 2u : 1u;
}

void testCameras() {
    int samples = 0;
    const std::array<float, 3> lenses = {modern::lensFovY(modern::kLens24), modern::lensFovY(modern::kLens35),
                                         modern::lensFovY(modern::kLens50)};
    for (const int chapter : kStudioChapters) {
        const TimelineFrame head = show::sampleTimeline(show::chapterStart(chapter));
        CHECK(modern::sceneFor(head.effect) == modern::SceneId::Studio);
        const std::vector<modern::StudioShot> shots = modern::studioShots(head);
        CHECK(shots.size() == expectedShots(head.effect));
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
            CHECK(std::find(lenses.begin(), lenses.end(), c.fovYDegrees) != lenses.end());
            std::uint32_t expectedShot = 0;
            for (std::size_t i = 1; i < shots.size(); ++i)
                expectedShot += static_cast<float>(frame.localSeconds) >= shots[i].start ? 1 : 0;
            CHECK(c.shot == expectedShot);
            if (screenShot(frame.effect, c.shot) && seesScreen(c)) {
                // In focus on the screen's plane (the thin lens focuses on a plane at focusDistance along
                // the view), with a small aperture: the CRT's picture stays sharp.
                const modern::CameraBasis b = modern::cameraBasis(c);
                CHECK(std::abs(c.focusDistance - modern::dot(screenCentre() - c.eye, b.forward)) < 1e-4f);
                CHECK(c.aperture <= 0.004f);
            }
            if (previous && previous->shot == c.shot) {
                CHECK(previous->fovYDegrees == c.fovYDegrees); // no zooms within a shot
                CHECK(modern::length(c.eye - previous->eye) < 0.05f);
                CHECK(modern::length(c.target - previous->target) < 0.05f);
            }
            previous = c;
            ++samples;
        }
    }
    std::printf("(d) cameras: %d frames finite, smooth and on the four lenses\n", samples);
}

// --- (o) ---------------------------------------------------------------------------------------------------

constexpr double kFrame = 1.0 / 60;

modern::Camera cameraAt(double showSeconds) {
    return modern::studioCamera(show::sampleTimeline(showSeconds));
}

show::TransitionMarks marksOf(int chapter) {
    return show::transitionMarks(show::sampleTimeline(show::chapterStart(chapter)));
}

// The tracked bar line `bars` bars after a time on a bar line.
double barsAfter(double showSeconds, int bars) {
    const show::TimelineFrame f = show::sampleTimeline(showSeconds);
    const show::BeatGridView grid = show::beatGrid(f.track);
    const double offset = show::trackStart(f.track);
    return grid.beatTime(grid.beatAt(showSeconds - offset + 1e-9) + 4 * bars) + offset;
}

// The times within [from, to) at which the eye passes the plane z = planeZ, to the frame.
std::vector<double> crossings(double from, double to, float planeZ, std::vector<Vec3>* where = nullptr) {
    std::vector<double> out;
    float last = cameraAt(from).eye.z - planeZ;
    for (double t = from + kFrame; t < to; t += kFrame) {
        const Vec3 eye = cameraAt(t).eye;
        const float now = eye.z - planeZ;
        if ((last < 0.f) != (now < 0.f)) {
            // The frame on the far side of the plane.
            out.push_back(t);
            if (where != nullptr)
                where->push_back(eye);
        }
        last = now;
    }
    return out;
}

// The camera's heading in degrees, unwrapped along a run of times.
std::vector<double> headings(double from, double to) {
    std::vector<double> out;
    double offset = 0, last = 0;
    for (double t = from; t <= to + 1e-9; t += kFrame) {
        const modern::CameraBasis b = modern::cameraBasis(cameraAt(t));
        double h = std::atan2(b.forward.x, -b.forward.z) * 180.0 / 3.14159265358979 + offset;
        if (!out.empty()) {
            while (h - last > 180)
                h -= 360, offset -= 360;
            while (h - last < -180)
                h += 360, offset += 360;
        }
        out.push_back(h);
        last = h;
    }
    return out;
}

void testCrossings() {
    const float screenZ = modern::studioScreenCorners()[0].z;
    const std::array<Vec3, 4> corners = modern::studioScreenCorners();
    const auto inPicture = [&](Vec3 p) {
        return p.x > corners[0].x && p.x < corners[1].x && p.y < corners[0].y && p.y > corners[2].y;
    };

    // FLYBY: the picture fills the frame on D; 1.2 m out by A + 1 s.
    const show::TransitionMarks flyby = marksOf(kFlyby);
    const modern::Camera onGlass = cameraAt(flyby.downbeat);
    const Pixel left = project(onGlass, 1600, 900, corners[0]),
                right = project(onGlass, 1600, 900, corners[1]);
    const double span = (right.x - left.x) / 1600.0;
    CHECK(span >= 0.98);
    const double out = cameraAt(flyby.accent + 1.0).eye.z - screenZ;
    CHECK(std::abs(out - 1.2) < 0.01);
    CHECK(crossings(show::chapterStart(kFlyby), show::chapterStart(kFlyby + 1), screenZ).empty());

    // PORTAL: in on D, out on the bar two bars on, inside the picture both times.
    const show::TransitionMarks portal = marksOf(kPortal);
    const double outward = barsAfter(portal.downbeat, 2);
    std::vector<Vec3> where;
    const std::vector<double> times =
        crossings(show::chapterStart(kPortal), show::chapterStart(kPortal + 1), screenZ, &where);
    CHECK(times.size() == 2);
    CHECK(times[0] >= portal.downbeat - 1e-9 && times[0] - portal.downbeat <= kFrame + 1e-9);
    CHECK(times[1] >= outward - 1e-9 && times[1] - outward <= kFrame + 1e-9);
    CHECK(inPicture(where[0]) && inPicture(where[1]));
    for (const double t : {portal.downbeat + kFrame, portal.accent, outward - kFrame})
        CHECK(modern::studioBehindScreen(cameraAt(t)));
    CHECK(!modern::studioBehindScreen(cameraAt(portal.downbeat - kFrame)) &&
          !modern::studioBehindScreen(cameraAt(outward)));
    // The turn over the span centred on the accent that starts on the glass, fastest within a beat of it.
    const double reach = portal.accent - portal.downbeat;
    const std::vector<double> h = headings(portal.accent - reach, portal.accent + reach);
    const double turned = std::abs(h.back() - h.front());
    CHECK(turned >= 175);
    double fastest = 0, when = 0;
    for (std::size_t i = 1; i < h.size(); ++i)
        if (std::abs(h[i] - h[i - 1]) > fastest) {
            fastest = std::abs(h[i] - h[i - 1]);
            when = portal.accent - reach + double(i) * kFrame;
        }
    const double beat = portal.bar / 4;
    CHECK(std::abs(when - portal.accent) <= beat);

    // STUDIO NIGHT: through the window's pane on the accent, clear of the glazing bars (the opening x in
    // [-1.85, -0.65], y in [1, 2]; the mullion at x = -1.37, the transom at y = 1.62, studio.cpp).
    const show::TransitionMarks night = marksOf(kStudioNight);
    where.clear();
    const std::vector<double> pane =
        crossings(show::chapterStart(kStudioNight), show::chapterStart(kStudioNight + 1), -1.0f, &where);
    CHECK(pane.size() == 1);
    CHECK(std::abs(pane[0] - night.accent) <= kFrame + 1e-9);
    const Vec3 p = where[0];
    CHECK(p.x > -1.85f && p.x < -0.65f && p.y > 1.f && p.y < 2.f);
    CHECK(std::abs(p.x + 1.37f) > 0.05f && std::abs(p.y - 1.62f) > 0.05f);

    // REBOOT: on the glass on D.
    const show::TransitionMarks reboot = marksOf(kReboot);
    const std::vector<double> glass =
        crossings(show::chapterStart(kReboot), show::chapterStart(kReboot + 1), screenZ);
    CHECK(!glass.empty() && std::abs(glass[0] - reboot.downbeat) <= kFrame + 1e-9);

    std::printf("(o) FLYBY spans %.3f of the frame on D, %.3f m out at A + 1 s; PORTAL in at %.3f, out at "
                "%.3f, turning %.0f degrees, fastest at %.3f (accent %.3f); STUDIO NIGHT's pane at %.3f; "
                "REBOOT's glass at %.3f\n",
                span, out, times[0], times[1], turned, when, portal.accent, pane[0], glass[0]);
}

// --- (p) ---------------------------------------------------------------------------------------------------

void testMoves() {
    int frames = 0;
    for (const int chapter : kStudioChapters) {
        const double start = show::chapterStart(chapter), end = show::chapterStart(chapter + 1);
        const TimelineFrame head = show::sampleTimeline(start);
        const show::TransitionMarks marks = show::transitionMarks(head);
        // Shot changes: on tracked bar lines, outside the two beats before the accent.
        const show::BeatGridView grid = show::beatGrid(head.track);
        const double offset = show::trackStart(head.track);
        const std::vector<modern::StudioShot> shots = modern::studioShots(head);
        for (std::size_t i = 1; i < shots.size(); ++i) {
            const double at = start + shots[i].start;
            const int beat = grid.beatAt(at - offset + 0.002);
            CHECK(beat >= 0 && grid.bar(beat) == 0 && std::abs(grid.beatTime(beat) + offset - at) < 0.001);
            const double beatLength = marks.bar / 4;
            CHECK(!(at > marks.accent - 2 * beatLength && at <= marks.accent));
        }
        // No straight, constant-speed stretch while the eye moves (handheld aside: its weight is 0 in these
        // checks' moves or adds curvature).
        int straight = 0;
        Vec3 a = cameraAt(start).eye, b = cameraAt(start + kFrame).eye;
        for (double t = start + 2 * kFrame; t < end; t += kFrame) {
            const modern::Camera cc = cameraAt(t);
            const Vec3 c = cc.eye;
            const bool sameShot = cameraAt(t - 2 * kFrame).shot == cc.shot;
            const bool moving = modern::length(c - b) > 1e-4f;
            const bool linear = modern::length(c - b * 2.f + a) < 1e-7f;
            straight = sameShot && moving && linear ? straight + 1 : 0;
            CHECK(straight < 15);
            a = b;
            b = c;
            ++frames;
        }
    }
    std::printf("(p) moves: %d frames eased (no straight stretch), shot changes on bar lines\n", frames);
}

// --- (q) ---------------------------------------------------------------------------------------------------

void testHandheld() {
    using modern::StudioHandheld;
    const std::vector<double> glass = modern::studioGlassTimes();
    CHECK(glass.size() == 5);
    double mostDegrees = 0, mostMetres = 0;
    for (double t = 0; t < show::showDuration(); t += kFrame / 2) {
        const StudioHandheld hh = modern::studioHandheld(t);
        const double degrees = std::sqrt(hh.yaw * hh.yaw + hh.pitch * hh.pitch + hh.roll * hh.roll);
        mostDegrees = std::max(mostDegrees, degrees);
        mostMetres = std::max(mostMetres, double(modern::length(hh.offset)));
        // Off near a glass.
        for (const double g : glass)
            if (std::abs(t - g) <= 0.5)
                CHECK(degrees == 0 && modern::length(hh.offset) == 0.f);
        // The same time, the same jitter.
        const StudioHandheld again = modern::studioHandheld(t);
        CHECK(again.yaw == hh.yaw && again.offset.x == hh.offset.x);
    }
    CHECK(mostDegrees <= modern::kHandheldDegrees + 1e-6 && mostMetres <= modern::kHandheldMetres + 1e-9);
    CHECK(mostDegrees > 0.5 * modern::kHandheldDegrees && mostMetres > 0.5 * modern::kHandheldMetres);
    // The spectrum of each channel over 60 s away from any glass (REBOOT's D is the last one): the strongest
    // frequency (DFT bins every 1/60 Hz up to 5 Hz, the mean removed) lies within 0.4 - 1.1 Hz.
    const double from = glass.back() + 2.0;
    constexpr int n = 3600;
    std::array<std::vector<double>, 6> channels;
    for (int i = 0; i < n; ++i) {
        const StudioHandheld hh = modern::studioHandheld(from + i * kFrame);
        const std::array<double, 6> v = {hh.offset.x, hh.offset.y, hh.offset.z, hh.yaw, hh.pitch, hh.roll};
        for (int c = 0; c < 6; ++c)
            channels[c].push_back(v[c]);
    }
    for (auto& x : channels) {
        double mean = 0;
        for (const double v : x)
            mean += v / n;
        double best = 0, peak = 0;
        for (int k = 1; k <= 300; ++k) {
            const double f = k / 60.0;
            double re = 0, im = 0;
            for (int i = 0; i < n; ++i) {
                const double phase = 2 * 3.14159265358979 * f * i * kFrame;
                re += (x[i] - mean) * std::cos(phase);
                im += (x[i] - mean) * std::sin(phase);
            }
            const double power = re * re + im * im;
            if (power > best) {
                best = power;
                peak = f;
            }
        }
        CHECK(peak >= 0.4 && peak <= 1.1);
    }
    std::printf(
        "(q) handheld: at most %.4f degrees and %.5f m, 0 near the %zu glass times, spectra peaking in "
        "0.4 - 1.1 Hz\n",
        mostDegrees, mostMetres, glass.size());
}

// --- (r) ---------------------------------------------------------------------------------------------------

void testBehindScreen(gfx::Context& ctx) {
    constexpr std::uint32_t w = 480, h = 270;
    Stages stages(ctx, w, h);
    const show::TransitionMarks portal = marksOf(kPortal);
    const TimelineFrame inside = show::sampleTimeline(portal.downbeat + 0.6);
    CHECK(modern::studioBehindScreen(modern::studioCamera(inside)));
    const Picture stars = stages.render(inside, exportState(inside, 16));
    stars.save(kOutDir / "studio_portal_stars.png");
    std::size_t lit = 0;
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            lit += stars.luminance(x, y) > 0.25;
    const TimelineFrame back = show::sampleTimeline(191.0);
    CHECK(modern::studioBehindScreen(modern::studioCamera(back)));
    const Picture room = stages.render(back, exportState(back, 16));
    room.save(kOutDir / "studio_portal_room_through_glass.png");
    std::printf("(r) behind the screen: %zu star pixels 0.6 s after the glass; the room through the glass's "
                "back at 191.0: mean %.3f\n",
                lit, room.meanLuminance());
    CHECK(lit >= 200);
    CHECK(room.meanLuminance() > 0.03);
    evo::test::checkChapterPictures(ctx, modern::SceneId::Studio);
    std::printf("(r) checkChapterPictures: the Studio's 6 chapters pass\n");
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
    CHECK(f.materials.size() == statics.materials.size()); // the chapter's light rig
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
    CHECK(f.instances.size() <= statics.capacity.instances);
    CHECK(f.lights.size() == modern::Studio::kLightCount && f.lights.size() <= statics.capacity.lights);
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
            const evo::rt::Material& m = f.materials[instance.material];
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
        const bool studio = effect == Effect::Flyby || effect == Effect::Finale || effect == Effect::Portal ||
                            effect == Effect::StudioNight || effect == Effect::Return ||
                            effect == Effect::Reboot;
        CHECK((modern::sceneFor(effect) == modern::SceneId::Studio) == studio);
        // Every effect shows the placeholder (no scene object) or a scene that has one.
        CHECK((modern::makeScene(modern::sceneFor(effect), {1.f / 256}) != nullptr) ==
              (modern::sceneFor(effect) != modern::SceneId::Placeholder));
    }
    // STUDIO NIGHT is 2026 only, yet the renderer runs the retro stage for the CRT.
    gfx::FrameRing ring(ctx);
    render::Renderer renderer(ctx, ring, {160, 90});
    const TimelineFrame frame = nightAt(4.0);
    VkCommandBuffer cmd = ring.begin();
    renderer.render(cmd, frame);
    ring.wait(ring.submit());
    CHECK(renderer.lastStats().retroRendered && renderer.lastStats().modernRendered);
    std::printf("(f) registry: 6 Studio chapters; STUDIO NIGHT renders retro frame %u for the CRT\n",
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
    // RETURN's close-up of the screen, the DOS prompt on it.
    const TimelineFrame still = chapterAt(kReturn, 0.8);
    const modern::Camera camera = modern::studioCamera(still);
    CHECK(camera.shot == 1);
    Stages window(ctx, kW, kH);
    const Picture frame30 = playWindow(window, still, 30);
    // The same frame again (a repeated frame, dt = 0: the history goes on, a new seed): the camera and the
    // retro frame are unchanged, so only sampling noise can change the screen.
    const Picture frame31 = window.render(still, interactiveState(still, render::HistoryReset::None, 30));
    Stages exporter(ctx, kW, kH);
    const Picture reference = exporter.render(still, exportState(still, 32));
    frame30.save(kOutDir / "studio_window_closeup_30.png");
    reference.save(kOutDir / "studio_window_closeup_export.png");
    const Rect screen = screenRect(camera, kW, kH, 0.1);
    const double flicker = meanAbsDifference(frame30, frame31, screen);
    const double error = meanAbsDifference(frame30, reference, screen);
    std::printf(
        "(h) window close-up screen: frame to frame %.4f, from a 32 spp export %.4f, local contrast %.3f "
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
    CHECK(modern::SceneFrame{}.clampIndirect == 16.f && modern::TraceSettings{}.clampIndirect == 16.f);
    const modern::Studio studio;
    // FINALE's pull-out passes the chrome and glass spheres and the lamp: plenty of caustic paths above 0.5.
    const TimelineFrame frame = chapterAt(kFinale, 0.5);
    const modern::SceneFrame scene = studio.frame(frame);
    CHECK(scene.clampIndirect == 0.5f);
    gfx::Image white = makeRetro(ctx, 255, 255, 255);
    Stages stages(ctx, 160, 90);
    const render::HistoryState state = exportState(frame, 4);
    stages.render(frame, state, &white);
    const std::vector<std::uint8_t> staged = stages.lightingBytes(ctx);
    modern::TraceSettings settings{state.spp, state.maxBounces, state.frameSeed};
    settings.clampIndirect = 0.5f;
    stages.trace(scene, settings, white);
    const std::vector<std::uint8_t> clamped = stages.lightingBytes(ctx);
    settings.clampIndirect = 16.f;
    stages.trace(scene, settings, white);
    const std::vector<std::uint8_t> loose = stages.lightingBytes(ctx);
    CHECK(staged == clamped);
    CHECK(clamped != loose);
    std::printf("(i) the Studio traces with clampIndirect 0.5 (16 changes its lighting)\n");
}

// --- (j) - (n): Task 14 of step 5
// ----------------------------------------------------------------------------------

void testRig() {
    const modern::Studio studio;
    const modern::SceneStatic& statics = studio.staticScene();
    namespace rt = evo::rt;
    for (const int chapter : {kFlyby, kFinale, kPortal, kStudioNight, kReturn, kReboot})
        for (const double fraction : {0.1, 0.5, 0.6, 0.9}) {
            const TimelineFrame t = chapterAt(chapter, fraction);
            const modern::SceneFrame f = studio.frame(t);
            const modern::StudioRig rig = modern::studioRigAt(t);
            modern::checkSceneFrame(statics, f);
            CHECK(f.materials.size() == statics.materials.size());
            const rt::Material& lamp = f.materials[modern::Studio::kLampMaterial];
            const rt::Material& screen = f.materials[modern::Studio::kScreenMaterial];
            CHECK(screen.emissionScale == rig.screen);
            CHECK(lamp.emissionScale == modern::Studio::kLampIntensity * rig.lamp);
            const rt::RectLight& lampLight = f.lights[modern::Studio::kLampLight];
            const rt::RectLight& screenLight = f.lights[modern::Studio::kScreenLight];
            double lampEmission = 0;
            for (int c = 0; c < 3; ++c) {
                CHECK(std::abs(lampLight.emission[c] - lamp.emission[c] * lamp.emissionScale) < 1e-4f);
                CHECK(screenLight.emission[c] == rig.screen);
                lampEmission += lampLight.emission[c];
            }
            if (t.effect == Effect::StudioNight || t.effect == Effect::Flyby)
                CHECK(lampEmission == 0 && lamp.emissionScale == 0.f);
            if (t.effect == Effect::Return || t.effect == Effect::Reboot)
                CHECK(lampEmission > 0);
            // The rig changes the lamp and the screen only; the LEDs are camera-only emitters.
            CHECK(f.materials[modern::Studio::kScreenBackMaterial].emissionScale ==
                  modern::Studio::kScreenBackLevel * rig.screen);
            CHECK(f.exposureEV == modern::studioExposureAt(t));
            if (t.effect != Effect::Portal && t.effect != Effect::Flyby &&
                !(t.effect == Effect::Return && fraction > 0.9))
                CHECK(f.exposureEV == modern::studioExposureEV(t.effect) &&
                      rig.screen == modern::studioRig(t.effect).screen);
            for (std::size_t i = 0; i < f.materials.size(); ++i)
                if (i != modern::Studio::kLampMaterial && i != modern::Studio::kScreenMaterial &&
                    i != modern::Studio::kScreenBackMaterial)
                    CHECK(std::memcmp(&f.materials[i], &statics.materials[i], sizeof(rt::Material)) == 0);
            CHECK((f.materials[modern::Studio::kLedMaterial].flags & rt::kMaterialCameraOnlyEmission) != 0);
        }
    std::printf("(j) the light rig: NIGHT lamp %.1f screen %.1f, FINALE %.1f / %.1f, RETURN %.1f / %.1f\n",
                modern::studioRig(Effect::StudioNight).lamp, modern::studioRig(Effect::StudioNight).screen,
                modern::studioRig(Effect::Finale).lamp, modern::studioRig(Effect::Finale).screen,
                modern::studioRig(Effect::Return).lamp, modern::studioRig(Effect::Return).screen);
}

// Whether each pixel lies more than `levels` (of 255, on the luminance of the bytes) above the median of its
// 3x3 neighbourhood (the border never does).
std::vector<bool> speckles(const Picture& p, double levels) {
    std::vector<bool> out(std::size_t{p.width} * p.height, false);
    for (std::uint32_t y = 1; y + 1 < p.height; ++y)
        for (std::uint32_t x = 1; x + 1 < p.width; ++x) {
            std::array<double, 9> n{};
            int k = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    n[k++] = p.luminance(x + dx, y + dy) * 255.0;
            std::nth_element(n.begin(), n.begin() + 4, n.end());
            out[std::size_t{y} * p.width + x] = p.luminance(x, y) * 255.0 > n[4] + levels;
        }
    return out;
}

void testSpeckle(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    Stages stages(ctx, 160, 90);
    const TimelineFrame frame = nightAt(5.0); // the dolly on from the night wide
    CHECK(modern::studioCamera(frame).shot == 0);
    // Two renders with different seeds: a pixel above its neighbours in both is a detail of the picture (an
    // edge, a star, the screen's text: about 1.1 % of this frame at 256 spp), one above them in a single
    // render is noise.
    render::HistoryState state = exportState(frame, 32);
    const Picture a = stages.render(frame, state);
    state.frameSeed = modern::frameSeedFor(frame.showSeconds, 1);
    const Picture b = stages.render(frame, state);
    a.save(kOutDir / "studio_night_speckle.png");
    const std::vector<bool> inA = speckles(a, 10), inB = speckles(b, 10);
    std::size_t both = 0, noise = 0;
    for (std::size_t i = 0; i < inA.size(); ++i) {
        both += inA[i] && inB[i];
        noise += inA[i] && !inB[i];
    }
    const double share = double(noise) / double(inA.size());
    std::printf("(k) STUDIO NIGHT wide at 32 spp: %.3f %% of the pixels speckle, %.3f %% are details in both "
                "seeds (%.1f s)\n",
                100 * share, 100 * double(both) / double(inA.size()), secondsSince(start));
    CHECK(share < 0.01);
}

// Where the chrome sphere's centre lands, and its radius, in pixels.
struct Disc {
    double x, y, radius;
};
Disc chromeDisc(const modern::Camera& camera, std::uint32_t width, std::uint32_t height) {
    const float r = modern::Studio::kChromeRadius;
    const Vec3 centre = modern::Studio::kChromeAt + Vec3{0.f, 0.012f + r - 0.006f, 0.f}; // studio.cpp's stand
    const Pixel c = project(camera, width, height, centre);
    const double z = modern::dot(centre - camera.eye, modern::cameraBasis(camera).forward);
    const double focal = 0.5 * height / std::tan(camera.fovYDegrees * 3.14159265358979 / 360.0);
    return {c.x, c.y, r * focal / z};
}

void testChromeReflectsScreen(gfx::Context& ctx) {
    gfx::Image red = makeRetro(ctx, 255, 0, 0);
    gfx::Image black = makeRetro(ctx, 0, 0, 0);
    Stages stages(ctx, kW, kH);
    // FINALE's pull-out passes the sphere in front of the monitor's right.
    const TimelineFrame frame = chapterAt(kFinale, 0.55);
    const Disc disc = chromeDisc(modern::studioCamera(frame), kW, kH);
    CHECK(disc.x - disc.radius > 0 && disc.x + disc.radius < kW && disc.y - disc.radius > 0 &&
          disc.y + disc.radius < kH);
    const auto redPixels = [&](const Picture& p) {
        int count = 0;
        for (std::uint32_t y = 0; y < kH; ++y)
            for (std::uint32_t x = 0; x < kW; ++x) {
                const double dx = x + 0.5 - disc.x, dy = y + 0.5 - disc.y;
                const std::uint8_t* px = p.at(x, y);
                if (dx * dx + dy * dy < 0.81 * disc.radius * disc.radius && px[0] >= 40 &&
                    px[0] >= 2 * px[1] && px[0] >= 2 * px[2])
                    ++count;
            }
        return count;
    };
    const Picture withRed = stages.render(frame, exportState(frame, 2), &red);
    const Picture withBlack = stages.render(frame, exportState(frame, 2), &black);
    withRed.save(kOutDir / "studio_chrome_red.png");
    const int lit = redPixels(withRed), unlit = redPixels(withBlack);
    std::printf(
        "(l) the chrome sphere (radius %.1f px at %.0f,%.0f): %d red-dominant pixels with a red picture, "
        "%d with a black one\n",
        disc.radius, disc.x, disc.y, lit, unlit);
    CHECK(lit >= 20 && unlit < lit / 4);
}

void testLedCameraOnly(gfx::Context& ctx) {
    gfx::Image white = makeRetro(ctx, 255, 255, 255);
    const modern::Studio studio;
    const TimelineFrame frame = chapterAt(kFinale, 0.5);
    constexpr std::uint32_t w = kW / 2, h = kH / 2;
    Stages stages(ctx, w, h);
    // 16 spp: from FINALE's wide (1.5 m out) the LEDs are a few pixels, which 4 spp of BSDF rays may miss.
    const render::HistoryState state = exportState(frame, 16);
    stages.render(frame, state, &white); // builds the Studio
    modern::TraceSettings settings{state.spp, state.maxBounces, state.frameSeed};
    modern::SceneFrame on = studio.frame(frame);
    settings.clampIndirect = on.clampIndirect;
    modern::SceneFrame off = on;
    off.materials[modern::Studio::kLedMaterial].emissionScale = 0.f;
    stages.trace(on, settings, white);
    const std::vector<std::uint8_t> lightingOn = stages.lightingBytes(ctx);
    stages.trace(off, settings, white);
    const std::vector<std::uint8_t> lightingOff = stages.lightingBytes(ctx);
    // Without the flag the LEDs would light the room (BSDF rays after a rough vertex find them).
    modern::SceneFrame lighting = on;
    lighting.materials[modern::Studio::kLedMaterial].flags = 0;
    stages.trace(lighting, settings, white);
    const bool lightsWithoutFlag = stages.lightingBytes(ctx) != lightingOn;
    // The desk right in front of the case's power LED.
    const modern::Camera& camera = on.camera;
    const Rect desk = around(camera, w, h, {-0.106f, 0.751f, -0.3f}, 2);
    CHECK(desk.left >= 0 && desk.top >= 0 && desk.right <= int(w) && desk.bottom <= int(h));
    std::size_t deskDiffers = 0, differs = 0;
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = (std::size_t{y} * w + x) * 16;
            const bool same =
                std::equal(lightingOn.begin() + i, lightingOn.begin() + i + 16, lightingOff.begin() + i);
            differs += !same;
            deskDiffers += !same && int(x) >= desk.left && int(x) < desk.right && int(y) >= desk.top &&
                           int(y) < desk.bottom;
        }
    std::printf(
        "(m) LEDs on vs off: the desk beside the case's LED is identical; %zu pixels differ in all (the "
        "LEDs seen through delta chains); without the flag they would light the room: %s\n",
        differs, lightsWithoutFlag ? "yes" : "no");
    CHECK(deskDiffers == 0);
    CHECK(lightsWithoutFlag);
}

void testMoonbeam(gfx::Context& ctx) {
    constexpr std::uint32_t w = kW / 2, h = kH / 2;
    Stages stages(ctx, w, h);
    const TimelineFrame frame = nightAt(4.0);
    const modern::Camera camera = modern::studioCamera(frame);
    CHECK(camera.shot == 0);
    // The moon's ray through the window's centre, down to the floor; the floor 1 m to its right is outside
    // the beam.
    const Vec3 window{-1.25f, 1.5f, -0.95f};
    const Vec3 down = modern::Studio::kMoonDirection * -1.f;
    const Vec3 beam = window + down * (window.y / -down.y);
    const Vec3 beside = beam + Vec3{1.f, 0.f, 0.f};
    const Picture p = stages.render(frame, exportState(frame, 4));
    p.save(kOutDir / "studio_moonbeam.png");
    const double lit = linearOver(p, around(camera, w, h, beam, 2));
    const double unlit = linearOver(p, around(camera, w, h, beside, 2));
    std::printf("(n) the moonbeam on the floor at (%.2f, %.2f): %.4f against %.4f beside it (linear)\n",
                beam.x, beam.z, lit, unlit);
    CHECK(lit >= 2 * unlit && lit > 0.01);
}

// --- --look ------------------------------------------------------------------------------------------------

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp, std::uint32_t width,
                std::uint32_t height, const std::string& only) {
    struct Still {
        const char* name;
        int chapter;
        double fraction;
    };
    const std::array<Still, 10> stills = {{{"finale_start", kFinale, 0.05},
                                           {"finale_mid", kFinale, 0.5},
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
            // The window: 1 spp per frame (4 and 2 on the first two after a reset), temporal history; frames
            // 1, 2, 3, 6 and 30 of a run ending at the still.
            for (std::uint32_t i = 0; i < 30; ++i) {
                const TimelineFrame f = show::sampleTimeline(frame.showSeconds - (29 - i) / 60.0);
                const bool reset =
                    i == 0 || modern::studioCamera(f).shot !=
                                  modern::studioCamera(show::sampleTimeline(f.showSeconds - 1 / 60.0)).shot;
                const Picture p = stages.render(
                    f, interactiveState(
                           f, reset ? render::HistoryReset::FirstFrame : render::HistoryReset::None, i));
                if (i <= 2 || i == 5 || i == 29)
                    p.save(dir /
                           (std::string("window_") + still.name + "_" + std::to_string(i + 1) + ".png"));
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
        // --only <letters> runs just those checks (for example --only kn).
        const std::string only = argc >= 3 && std::string(argv[1]) == "--only" ? argv[2] : "";
        const auto run = [&](char letter) { return only.empty() || only.find(letter) != std::string::npos; };
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            const std::uint32_t spp = argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 64u;
            const std::uint32_t w = argc >= 6 ? std::uint32_t(std::stoul(argv[4])) : 640u;
            const std::uint32_t h = argc >= 6 ? std::uint32_t(std::stoul(argv[5])) : 360u;
            renderLook(ctx, argv[2], spp, w, h, argc >= 7 ? argv[6] : "");
            CHECK(ctx.validationMessageCount() == 0);
            return 0;
        }
        if (run('d'))
            testCameras();
        if (run('o'))
            testCrossings();
        if (run('p'))
            testMoves();
        if (run('q'))
            testHandheld();
        if (run('e'))
            testScene();
        if (run('g'))
            testDosIdle();
        if (run('f'))
            testRegistry(ctx);
        if (run('a'))
            testStudioNightLooks(ctx);
        if (run('b'))
            testCrtLightsTheRoom(ctx);
        if (run('c'))
            testScreenFollowsPicture(ctx);
        if (run('h'))
            testWindowScreenSharp(ctx);
        if (run('i'))
            testClampIndirect(ctx);
        if (run('j'))
            testRig();
        if (run('k'))
            testSpeckle(ctx);
        if (run('l'))
            testChromeReflectsScreen(ctx);
        if (run('m'))
            testLedCameraOnly(ctx);
        if (run('n'))
            testMoonbeam(ctx);
        if (run('r'))
            testBehindScreen(ctx);
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("studio_test: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
