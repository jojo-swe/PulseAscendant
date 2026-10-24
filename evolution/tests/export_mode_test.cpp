// Export mode (Task 11 of step 4): what the renderer hands the modern stage and what comes out, at 160x90 in
// STUDIO NIGHT at 40 %. Review Focus 6 (step 5: Review Focus 3): with equal bounces, the window's first
// frame after a history reset equals an export frame at spp max(--spp, 4) byte for byte (the window traces 4
// spp on a reset frame and 2 on the next, kResetSpp; one tonemap, display encoding exactly once, the export
// seed on the first frame after a reset). Export purity: a frame is a pure function of (settings, show time),
// the same in a fresh renderer as after other frames; the seed moves with the show time, frameSeed =
// pcg(llround(showSeconds * 48000)) in exports, plus 0x9E3779B9 per frame since the last reset in the window.
// Runs with the validation layer on and fails on any message it reports.
//
// STUDIO NIGHT path traces the Studio (Task 10): the test fails unless every Studio chapter maps to it and
// the frames really run the modern stage and, for the CRT, the retro stage. The bounce limit reaches the
// tracer: exports at 1 and 6 bounces differ, and so do the Export and Interactive defaults (6 and 4).

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/studio.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include "test_env.hpp"

#include <algorithm>
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
    CHECK(exported.stats().modernRendered && exported.stats().retroRendered); // the Studio and its CRT
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

// Review Focus 6 (step 5: 3): the window's first frame after a reset equals an export frame at equal bounces
// and spp max(--spp, 4), on the very first frame and after a seek. The window reports the samples it traced:
// 4, 2, then --spp (Task 7 of step 5); an export always --spp.
void testWindowMatchesExport(evo::gfx::Context& ctx) {
    const TimelineFrame t = studioNight(0.4);
    for (const std::uint32_t spp : {1u, 2u, 5u}) { // 5: two trace dispatches
        Harness exported(ctx, settings(RenderMode::Export, std::max(spp, 4u), 4));
        const std::vector<std::uint8_t> reference = exported.render(t);
        CHECK(exported.stats().spp == std::max(spp, 4u));

        Harness window(ctx, settings(RenderMode::Interactive, spp, 4));
        CHECK(window.render(t) == reference);
        CHECK(window.stats().historyReset == HistoryReset::FirstFrame);
        CHECK(window.stats().spp == std::max(spp, 4u));
        if (spp == 1) {
            window.render(studioNight(0.4, kFrame));
            CHECK(window.stats().historyReset == HistoryReset::None && window.stats().spp == 2);
            window.render(studioNight(0.4, 2 * kFrame));
            CHECK(window.stats().spp == 1);
            window.render(evo::show::sampleTimeline(1.0)); // BOOT, 1993 only: nothing traced
            CHECK(!window.stats().modernRendered && window.stats().spp == 0);
        }

        Harness seeking(ctx, settings(RenderMode::Interactive, spp, 4));
        seeking.render(studioNight(0.1));
        seeking.render(studioNight(0.1, kFrame));
        CHECK(seeking.render(t) == reference);
        // A seek within the chapter: TimeJump, or Shot if the Studio's cameras cut in between.
        const HistoryReset reset = seeking.stats().historyReset;
        CHECK(reset == HistoryReset::TimeJump || reset == HistoryReset::Shot);
    }
}

// Export purity: T in two fresh renderers, and after first rendering T - 1/60, gives the same bytes; T + 1/60
// does not (the seed and the scene move). At spp 2, 5 (two trace dispatches) and 16 (no a-trous filter).
void testExportPurity(evo::gfx::Context& ctx) {
    const TimelineFrame t = studioNight(0.4);
    // T - 1/60 is in the same camera shot, so the tracker sees no reset in between.
    CHECK(evo::modern::studioCamera(studioNight(0.4, -kFrame)).shot == evo::modern::studioCamera(t).shot);
    for (const std::uint32_t spp : {2u, 5u, 16u}) {
        std::vector<std::uint8_t> first, second, afterPrevious, later;
        {
            Harness a(ctx, settings(RenderMode::Export, spp, 0));
            first = a.render(t);
        }
        {
            Harness b(ctx, settings(RenderMode::Export, spp, 0));
            second = b.render(t);
        }
        {
            Harness c(ctx, settings(RenderMode::Export, spp, 0));
            c.render(studioNight(0.4, -kFrame));
            afterPrevious = c.render(t);
            CHECK(c.stats().historyReset ==
                  HistoryReset::None); // the tracker counted on; the frame did not care
            later = c.render(studioNight(0.4, kFrame));
        }
        CHECK(first == second);
        CHECK(first == afterPrevious);
        CHECK(first != later);
    }
}

// The bounce limit reaches the path tracer: an export at 1 bounce differs from one at 6, and the Export
// default (6) from the Interactive one (4) on the same frame (the window's first frame equals an export at
// equal bounces, so only the limit can tell them apart).
void testBounces(evo::gfx::Context& ctx) {
    const TimelineFrame t = studioNight(0.4);
    Harness one(ctx, settings(RenderMode::Export, 2, 1));
    Harness six(ctx, settings(RenderMode::Export, 2, 6));
    Harness exportDefault(ctx, settings(RenderMode::Export, 2, 0));
    Harness windowDefault(ctx, settings(RenderMode::Interactive, 2, 0));
    CHECK(exportDefault.renderer().maxBounces() == 6 && windowDefault.renderer().maxBounces() == 4);
    const std::vector<std::uint8_t> a = one.render(t), b = six.render(t);
    CHECK(a != b);
    CHECK(exportDefault.render(t) == b);
    CHECK(windowDefault.render(t) != b);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        using evo::show::Effect;
        for (const Effect effect :
             {Effect::Finale, Effect::Portal, Effect::StudioNight, Effect::Return, Effect::Reboot})
            CHECK(evo::modern::sceneFor(effect) == evo::modern::SceneId::Studio);
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testSeeds(ctx);
            testWindowMatchesExport(ctx);
            testExportPurity(ctx);
            testBounces(ctx);
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
