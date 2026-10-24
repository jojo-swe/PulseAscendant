// Export mode (Task 11 of step 4): what the renderer hands the modern stage and what comes out, at 160x90 in
// STUDIO NIGHT at 40 %. Review Focus 6: with equal spp and bounces, the window's first frame after a history
// reset equals an export frame byte for byte (one tonemap, display encoding exactly once, the export seed on
// the first frame after a reset). Export purity: a frame is a pure function of (settings, show time), the
// same in a fresh renderer as after other frames; the seed moves with the show time, frameSeed =
// pcg(llround(showSeconds * 48000)) in exports, plus 0x9E3779B9 per frame since the last reset in the window.
// Runs with the validation layer on and fails on any message it reports.
//
// The byte comparisons hold for any scene, so they pin the path-traced Studio once modern::sceneFor maps
// STUDIO NIGHT to it (Task 10); before that the chapter renders the placeholder and the test says so.

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include "test_env.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::render::HistoryReset;
using evo::render::Renderer;
using evo::render::RenderMode;
using evo::render::RenderSettings;
using evo::show::TimelineFrame;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr std::uint32_t kWidth = 160, kHeight = 90;
constexpr int kStudioNight = 11;
constexpr double kFrame = 1.0 / 60.0;

// STUDIO NIGHT at `fraction` of its length, plus `offset` seconds.
TimelineFrame studioNight(double fraction, double offset = 0.0) {
    const double start = evo::show::chapterStart(kStudioNight);
    const double end = evo::show::chapterStart(kStudioNight + 1);
    const TimelineFrame frame = evo::show::sampleTimeline(start + fraction * (end - start) + offset);
    CHECK(frame.effect == evo::show::Effect::StudioNight && frame.era == 1.f);
    return frame;
}

RenderSettings settings(RenderMode mode, std::uint32_t spp, std::uint32_t maxBounces) {
    RenderSettings s{kWidth, kHeight}; // CRT and HUD on, as the exports have them
    s.mode = mode;
    s.spp = spp;
    s.maxBounces = maxBounces;
    return s;
}

// One renderer with its own ring and readback; render() returns the output's RGB bytes.
class Harness {
public:
    Harness(evo::gfx::Context& ctx, const RenderSettings& s)
        : readback_(ctx, s.width, s.height), ring_(ctx), renderer_(ctx, ring_, s) {}

    std::vector<std::uint8_t> render(const TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return readback_.rgb();
    }
    Renderer::Stats stats() const { return renderer_.lastStats(); }
    const Renderer& renderer() const { return renderer_; }

private:
    evo::gfx::Readback readback_;
    evo::gfx::FrameRing ring_;
    Renderer renderer_; // last: its destructor waits for the ring
};

// The seed the renderer hands the modern stage: the export seed of the show time in Export, whatever came
// before; in the window the export seed on a reset, then moved by every frame since.
void testSeeds(evo::gfx::Context& ctx) {
    const TimelineFrame t = studioNight(0.4);
    const TimelineFrame next = studioNight(0.4, kFrame);
    using evo::modern::frameSeedFor;

    Harness exported(ctx, settings(RenderMode::Export, 2, 0));
    CHECK(exported.renderer().spp() == 2 && exported.renderer().maxBounces() == 6);
    exported.render(t);
    CHECK(exported.stats().modernRendered);
    CHECK(exported.stats().frameSeed == frameSeedFor(t.showSeconds, 0));
    exported.render(next); // the history tracker counts on, the export seed does not
    CHECK(exported.stats().framesSinceReset == 1);
    CHECK(exported.stats().frameSeed == frameSeedFor(next.showSeconds, 0));
    CHECK(exported.stats().frameSeed != frameSeedFor(t.showSeconds, 0)); // the seed moves with the time

    Harness window(ctx, settings(RenderMode::Interactive, 1, 0));
    CHECK(window.renderer().spp() == 1 && window.renderer().maxBounces() == 4);
    window.render(t);
    CHECK(window.stats().historyReset == HistoryReset::FirstFrame);
    CHECK(window.stats().frameSeed == frameSeedFor(t.showSeconds, 0)); // the export seed after a reset
    window.render(next);
    CHECK(window.stats().historyReset == HistoryReset::None && window.stats().framesSinceReset == 1);
    CHECK(window.stats().frameSeed == frameSeedFor(next.showSeconds, 1));
    CHECK(window.stats().frameSeed != frameSeedFor(next.showSeconds, 0));
    window.render(studioNight(0.8)); // a seek (past the accent's shot cut, too): the export seed again
    CHECK(window.stats().historyReset != HistoryReset::None);
    CHECK(window.stats().frameSeed == frameSeedFor(studioNight(0.8).showSeconds, 0));

    // A frame without the modern stage hands it nothing: 0.
    window.render(evo::show::sampleTimeline(1.0)); // BOOT, 1993 only
    CHECK(!window.stats().modernRendered && window.stats().frameSeed == 0);
}

// Review Focus 6: the window's first frame after a reset equals an export frame at equal spp and bounces,
// on the very first frame and after a seek.
void testWindowMatchesExport(evo::gfx::Context& ctx) {
    const TimelineFrame t = studioNight(0.4);
    for (const std::uint32_t spp : {1u, 2u}) {
        Harness exported(ctx, settings(RenderMode::Export, spp, 4));
        const std::vector<std::uint8_t> reference = exported.render(t);

        Harness window(ctx, settings(RenderMode::Interactive, spp, 4));
        CHECK(window.render(t) == reference);
        CHECK(window.stats().historyReset == HistoryReset::FirstFrame);

        Harness seeking(ctx, settings(RenderMode::Interactive, spp, 4));
        seeking.render(studioNight(0.1));
        seeking.render(studioNight(0.1, kFrame));
        CHECK(seeking.render(t) == reference);
        CHECK(seeking.stats().historyReset == HistoryReset::TimeJump); // 0.1 and 0.4: the same shot
    }
}

// Export purity at spp 2: T in two fresh renderers, and after first rendering T - 1/60, gives the same bytes;
// T + 1/60 does not (the seed and the scene move).
void testExportPurity(evo::gfx::Context& ctx) {
    const TimelineFrame t = studioNight(0.4);
    std::vector<std::uint8_t> first, second, afterPrevious, later;
    {
        Harness a(ctx, settings(RenderMode::Export, 2, 0));
        first = a.render(t);
    }
    {
        Harness b(ctx, settings(RenderMode::Export, 2, 0));
        second = b.render(t);
    }
    {
        Harness c(ctx, settings(RenderMode::Export, 2, 0));
        c.render(studioNight(0.4, -kFrame));
        afterPrevious = c.render(t);
        CHECK(c.stats().historyReset == HistoryReset::None); // the tracker counted on; the frame did not care
        later = c.render(studioNight(0.4, kFrame));
    }
    CHECK(first == second);
    CHECK(first == afterPrevious);
    CHECK(first != later);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        const bool studio =
            evo::modern::sceneFor(evo::show::Effect::StudioNight) == evo::modern::SceneId::Studio;
        if (!studio)
            std::printf("note: STUDIO NIGHT renders the placeholder (no path-traced Studio yet), so the byte "
                        "comparisons hold trivially\n");
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testSeeds(ctx);
            testWindowMatchesExport(ctx);
            testExportPurity(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("export_mode_test: %d checks passed\n", checksPassed);
    return 0;
}
