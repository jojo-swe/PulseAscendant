// The trace resolution (Task 4 of step 5): the 2026 side traces at max(16, round(output * traceScale)) per
// axis and the temporal upscaler brings it to the output size. Pinned here, at 160x90 with the validation
// layer on (fails on any message):
//   - the trace size;
//   - the path tracer's subpixel window: with a point-sized footprint the samples sit at the pixel centre
//   plus
//     the jitter (an emitter quad's edge pixels flip between lit and unlit), and the default window is the
//     step 4 box filter; refusals;
//   - Review Focus 3 below scale 1: the window's first frame after a reset at a trace scale equals an Export
//     frame at that scale and spp 4 byte for byte (both trace the whole pixel unjittered and upsample
//     spatially; the CLI's exports always trace at scale 1, where export_mode_test pins the same equality);
//   - Review Focus 1 below scale 1: after seeks (another chapter, back within a shot) the window's frames,
//     jittered and upscaled in time, equal a fresh renderer's from the same point byte for byte;
//   - a trace scale change resets the history (Resize) and gives a fresh renderer's frame; refusals;
//   - the GPU timers: Renderer::Stats::gpuMs names every pass of a traced frame, upscale only below scale 1.
// `trace_scale_test --look <dir> [width height]` writes window frames at scales 0.67 and 1 to look at.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene_gpu.hpp"
#include "post/upscale.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include "rt_test_scenes.hpp"
#include "test_env.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace evo;
using render::HistoryReset;
using render::Renderer;
using render::RenderMode;
using render::RenderSettings;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

template <typename Exception, typename F> bool throws(F&& f) {
    try {
        f();
    } catch (const Exception& error) {
        std::printf("  refused: %s\n", error.what());
        return true;
    }
    return false;
}

constexpr std::uint32_t kWidth = 160, kHeight = 90;
constexpr double kFrame = 1.0 / 60.0;
constexpr int kStudioNight = 11, kReturn = 19;

double chapterTime(int chapter, double fraction) {
    const double start = show::chapterStart(chapter);
    return start + fraction * (show::chapterStart(chapter + 1) - start);
}

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    float magnitude;
    if (exponent == 0)
        magnitude = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31)
        magnitude =
            mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else
        magnitude = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -magnitude : magnitude;
}

void testTraceSize() {
    const auto is = [](VkExtent2D e, std::uint32_t w, std::uint32_t h) {
        return e.width == w && e.height == h;
    };
    CHECK(is(render::traceSize(160, 90, 1.f), 160, 90));
    CHECK(is(render::traceSize(160, 90, 0.67f), 107, 60));
    CHECK(is(render::traceSize(2560, 1440, 0.67f), 1715, 965));
    CHECK(is(render::traceSize(3840, 2160, 0.5f), 1920, 1080));
    CHECK(is(render::traceSize(40, 30, 0.25f), 16, 16)); // at least 16
    CHECK(is(render::traceSize(12, 20, 0.25f), 12, 16)); // never above the output
    std::printf("trace size: max(16, round(output * scale)), at most the output\n");
}

// --- The path tracer's subpixel window -------------------------------------------------------------------

constexpr std::uint32_t kQuadSize = 40;

// The emitter quad scene's emission image traced with a subpixel window, as floats (R per pixel).
std::vector<float> quadEmission(gfx::Context& ctx, const modern::TraceSettings& settings) {
    const test::TestScene scene = test::emitterQuadScene();
    modern::SceneGpu gpu(ctx, scene.statics);
    gfx::Buffer readback(ctx, {VkDeviceSize{kQuadSize} * kQuadSize * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               true, "emission readback"});
    gfx::FrameRing ring(ctx);
    modern::PathTracer tracer(ctx, ring, kQuadSize, kQuadSize,
                              modern::tracerCapacity(scene.statics.capacity));
    VkCommandBuffer cmd = ring.begin();
    tracer.trace(cmd, gpu, scene.frame, std::nullopt, settings, nullptr);
    gfx::Image& emission = tracer.gbuffer().emission;
    emission.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {kQuadSize, kQuadSize, 1};
    vkCmdCopyImageToBuffer(cmd, emission.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle(), 1,
                           &region);
    gfx::bufferBarrier2(cmd, readback.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    ring.wait(ring.submit());
    vkQueueWaitIdle(ctx.queue());
    readback.invalidate();
    std::vector<float> red(std::size_t{kQuadSize} * kQuadSize);
    const auto* bits = static_cast<const std::uint16_t*>(readback.mapped());
    for (std::size_t i = 0; i < red.size(); ++i)
        red[i] = halfToFloat(bits[i * 4]);
    return red;
}

// The quad (1 x 1 at 2 m, 40 degrees) spans x in [6.26, 33.74] in a 40-pixel picture: pixels 6 and 33 are
// its edge pixels, lit by the samples that fall inside it.
void testSubpixelWindow(gfx::Context& ctx) {
    modern::TraceSettings settings;
    settings.spp = 4;
    settings.frameSeed = 3;
    const std::uint32_t row = kQuadSize / 2;
    const auto at = [&](const std::vector<float>& e, std::uint32_t x) {
        return e[std::size_t{row} * kQuadSize + x];
    };

    const std::vector<float> box = quadEmission(ctx, settings); // the default: the whole pixel
    // Lit: the quad's emission 2; unlit: the sky behind it (the emission image holds a primary miss's sky).
    const auto lit = [](float e) { return e == 2.f; };
    const auto unlit = [](float e) { return e < 0.5f; };
    CHECK(lit(at(box, 20)) && unlit(at(box, 3)));

    settings.pixelFootprint = {0.01f, 0.01f};
    settings.pixelJitter = {-0.45f, 0.f}; // samples at x + 0.05
    const std::vector<float> left = quadEmission(ctx, settings);
    CHECK(unlit(at(left, 6)) && lit(at(left, 33)));
    settings.pixelJitter = {0.45f, 0.f}; // samples at x + 0.95
    const std::vector<float> right = quadEmission(ctx, settings);
    CHECK(lit(at(right, 6)) && unlit(at(right, 33)));
    std::printf(
        "subpixel window: edge pixels 6 and 33 sampled at x + 0.05 (%.2f, %.2f) and x + 0.95 (%.2f, %.2f)\n",
        at(left, 6), at(left, 33), at(right, 6), at(right, 33));

    // Refusals: a footprint outside (0, 1], a window reaching out of the pixel.
    const test::TestScene scene = test::emitterQuadScene();
    modern::SceneGpu gpu(ctx, scene.statics);
    gfx::FrameRing ring(ctx);
    modern::PathTracer tracer(ctx, ring, 16, 16, modern::tracerCapacity(scene.statics.capacity));
    const auto refused = [&](std::array<float, 2> jitter, std::array<float, 2> footprint) {
        modern::TraceSettings bad;
        bad.pixelJitter = jitter;
        bad.pixelFootprint = footprint;
        VkCommandBuffer cmd = ring.begin();
        const bool threw = throws<std::invalid_argument>(
            [&] { tracer.trace(cmd, gpu, scene.frame, std::nullopt, bad, nullptr); });
        ring.submit();
        return threw;
    };
    CHECK(refused({0.f, 0.f}, {0.f, 1.f}));
    CHECK(refused({0.f, 0.f}, {1.5f, 1.f}));
    CHECK(refused({0.3f, 0.f}, {0.5f, 0.5f}));
    CHECK(refused({0.f, std::numeric_limits<float>::quiet_NaN()}, {0.5f, 0.5f}));
    ring.waitAll();
    vkQueueWaitIdle(ctx.queue());
}

// --- The renderer ----------------------------------------------------------------------------------------

RenderSettings settings(RenderMode mode, float scale, std::uint32_t spp = 1) {
    RenderSettings s{kWidth, kHeight};
    s.mode = mode;
    s.spp = spp;
    s.maxBounces = 4; // equal bounces in both modes
    s.traceScale = scale;
    return s;
}

struct Frame {
    std::vector<std::uint8_t> rgb;
    Renderer::Stats stats;
};

class Harness {
public:
    Harness(gfx::Context& ctx, const RenderSettings& s)
        : readback_(ctx, kWidth, kHeight), ring_(ctx), renderer_(ctx, ring_, s) {}

    Frame render(double seconds) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, show::sampleTimeline(seconds));
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return {readback_.rgb(), renderer_.lastStats()};
    }
    std::vector<Frame> play(double start, int count) {
        std::vector<Frame> frames;
        for (int i = 0; i < count; ++i)
            frames.push_back(render(start + i * kFrame));
        return frames;
    }
    Renderer& renderer() { return renderer_; }

private:
    gfx::Readback readback_;
    gfx::FrameRing ring_;
    Renderer renderer_;
};

void testWindowEqualsExport(gfx::Context& ctx) {
    const double t = chapterTime(kStudioNight, 0.4);
    Harness exported(ctx, settings(RenderMode::Export, 0.67f, 4));
    const Frame reference = exported.render(t);
    Harness window(ctx, settings(RenderMode::Interactive, 0.67f));
    const Frame first = window.render(t);
    CHECK(first.stats.spp == 4 && first.stats.historyReset == HistoryReset::FirstFrame);
    CHECK(window.renderer().modern().tracer()->width() == 107 &&
          window.renderer().modern().tracer()->height() == 60);
    CHECK(first.rgb == reference.rgb);
    // A native trace differs: the scale is real.
    Harness native(ctx, settings(RenderMode::Export, 1.f, 4));
    CHECK(native.render(t).rgb != reference.rgb);
    // The window jitters from its second frame on, with the subpixel window of its frames since the reset;
    // the export and a native trace never do.
    CHECK(first.stats.traceJitter == (std::array<float, 2>{0.f, 0.f}));
    for (std::uint32_t k = 1; k < 4; ++k) {
        const Frame next = window.render(t + k * kFrame);
        CHECK(next.stats.framesSinceReset == k);
        CHECK(next.stats.traceJitter == post::subpixelWindow(107, 60, kWidth, kHeight, k, true).jitter);
        CHECK(next.stats.traceJitter != (std::array<float, 2>{0.f, 0.f}));
        CHECK(exported.render(t + k * kFrame).stats.traceJitter == (std::array<float, 2>{0.f, 0.f}));
    }
    Harness nativeWindow(ctx, settings(RenderMode::Interactive, 1.f));
    for (std::uint32_t k = 0; k < 3; ++k)
        CHECK(nativeWindow.render(t + k * kFrame).stats.traceJitter == (std::array<float, 2>{0.f, 0.f}));
    std::printf("window at scale 0.67: first frame equals an export at scale 0.67, spp 4\n");
}

void checkSame(const std::vector<Frame>& a, const std::vector<Frame>& b, HistoryReset firstReset) {
    CHECK(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].stats.historyReset == (i == 0 ? firstReset : b[i].stats.historyReset));
        CHECK(a[i].stats.framesSinceReset == b[i].stats.framesSinceReset);
        CHECK(a[i].rgb == b[i].rgb);
    }
}

void testSeeks(gfx::Context& ctx) {
    const double night = chapterTime(kStudioNight, 0.4), elsewhere = chapterTime(kReturn, 0.5);
    std::vector<Frame> nightRef, returnRef;
    {
        Harness fresh(ctx, settings(RenderMode::Interactive, 0.67f));
        nightRef = fresh.play(night, 5);
    }
    {
        Harness fresh(ctx, settings(RenderMode::Interactive, 0.67f));
        returnRef = fresh.play(elsewhere, 4);
    }
    // The references accumulate (jittered, upscaled in time): consecutive frames differ.
    CHECK(nightRef[2].rgb != nightRef[3].rgb);

    Harness seeker(ctx, settings(RenderMode::Interactive, 0.67f));
    seeker.play(night + 30 * kFrame, 3);
    checkSame(seeker.play(elsewhere, 4), returnRef, HistoryReset::Chapter);
    checkSame(seeker.play(night, 5), nightRef, HistoryReset::Chapter);
    checkSame(seeker.play(night, 5), nightRef, HistoryReset::TimeJump); // back within the shot
    std::printf("window at scale 0.67: seeks equal a fresh renderer's frames\n");
}

void testScaleChange(gfx::Context& ctx) {
    const double t = chapterTime(kStudioNight, 0.4);
    Harness window(ctx, settings(RenderMode::Interactive, 1.f));
    window.play(t, 3);
    window.renderer().setTraceScale(1.f); // unchanged: no reset
    CHECK(window.render(t + 3 * kFrame).stats.historyReset == HistoryReset::None);
    window.renderer().setTraceScale(0.5f);
    CHECK(window.renderer().traceScale() == 0.5f);
    const Frame changed = window.render(t + 4 * kFrame);
    CHECK(changed.stats.historyReset == HistoryReset::Resize && changed.stats.framesSinceReset == 0);
    CHECK(window.renderer().modern().tracer()->width() == 80 &&
          window.renderer().modern().tracer()->height() == 45);
    Harness fresh(ctx, settings(RenderMode::Interactive, 0.5f));
    CHECK(changed.rgb == fresh.render(t + 4 * kFrame).rgb);

    for (const float bad : {0.f, -0.5f, 1.01f, std::numeric_limits<float>::quiet_NaN()}) {
        CHECK(throws<std::invalid_argument>([&] { window.renderer().setTraceScale(bad); }));
        CHECK(throws<std::invalid_argument>([&] {
            Renderer(ctx, *std::make_unique<gfx::FrameRing>(ctx), settings(RenderMode::Interactive, bad));
        }));
    }
    std::printf("trace scale change: Resize, a fresh renderer's frame\n");
}

void testGpuTimes(gfx::Context& ctx) {
    const double t = chapterTime(kStudioNight, 0.4);
    for (const float scale : {0.5f, 1.f}) {
        Harness window(ctx, settings(RenderMode::Interactive, scale));
        Frame frame;
        for (int i = 0; i < 4; ++i)
            frame = window.render(t + i * kFrame);
        std::map<std::string, double> ms(frame.stats.gpuMs.begin(), frame.stats.gpuMs.end());
        std::printf("GPU ms at scale %.1f:", scale);
        for (const auto& [name, value] : frame.stats.gpuMs)
            std::printf(" %s %.2f", name.c_str(), value);
        std::printf("\n");
        CHECK(!frame.stats.gpuMs.empty()); // lavapipe has timestamps
        CHECK(frame.stats.gpuMs.front().first == "total");
        for (const char* name : {"retro", "tlas", "trace", "denoise", "tonemap", "compositor"})
            CHECK(ms.count(name) == 1);
        CHECK(ms.count("upscale") == (scale < 1.f ? 1u : 0u));
        for (const auto& [name, value] : frame.stats.gpuMs)
            CHECK(std::isfinite(value) && value >= 0.0 && value <= ms["total"]);
    }
}

// --look <dir> [width height]: window frames to look at, at trace scales 0.67 and 1: the first frame after a
// seek (the spatial upsample) and the 24th (accumulated), in STUDIO NIGHT's dolly and RETURN's push-in, plus
// the 64 spp export of the same time.
void look(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t width, std::uint32_t height) {
    std::filesystem::create_directories(dir);
    for (const auto& [name, t] : {std::pair{"studio_night", chapterTime(kStudioNight, 0.4)},
                                  std::pair{"return", chapterTime(kReturn, 0.5)}}) {
        for (const float scale : {0.67f, 1.f}) {
            RenderSettings s{width, height};
            s.mode = RenderMode::Interactive;
            s.traceScale = scale;
            gfx::Readback readback(ctx, width, height);
            gfx::FrameRing ring(ctx);
            Renderer renderer(ctx, ring, s);
            for (int i = 0; i < 24; ++i) {
                VkCommandBuffer cmd = ring.begin();
                renderer.render(cmd, show::sampleTimeline(t + i * kFrame));
                readback.record(cmd, renderer.output());
                ring.wait(ring.submit());
                if (i == 0 || i == 23) {
                    const std::string file = std::string(name) + "_scale" + (scale < 1.f ? "067" : "100") +
                                             "_frame" + std::to_string(i + 1) + ".png";
                    image::writePng(dir / file, static_cast<int>(width), static_cast<int>(height),
                                    readback.rgb().data());
                    std::printf("wrote %s\n", (dir / file).string().c_str());
                }
            }
        }
        RenderSettings e{width, height};
        e.spp = 64;
        gfx::Readback readback(ctx, width, height);
        gfx::FrameRing ring(ctx);
        Renderer renderer(ctx, ring, e);
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, show::sampleTimeline(t + 23 * kFrame));
        readback.record(cmd, renderer.output());
        ring.wait(ring.submit());
        image::writePng(dir / (std::string(name) + "_export64.png"), static_cast<int>(width),
                        static_cast<int>(height), readback.rgb().data());
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--look") {
        test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        auto ctx = gfx::Context::create({});
        const std::uint32_t w = argc >= 5 ? static_cast<std::uint32_t>(std::stoul(argv[3])) : 480;
        const std::uint32_t h = argc >= 5 ? static_cast<std::uint32_t>(std::stoul(argv[4])) : 270;
        look(ctx, argv[2], w, h);
        return 0;
    }
    const auto start = std::chrono::steady_clock::now();
    try {
        test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testTraceSize();
        {
            auto ctx = gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pictures are checked\n");
            testSubpixelWindow(ctx);
            testWindowEqualsExport(ctx);
            testSeeks(ctx);
            testScaleChange(ctx);
            testGpuTimes(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("trace_scale_test: %d checks passed in %.1f s\n", checksPassed, seconds);
    return 0;
}
