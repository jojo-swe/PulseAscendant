// The stage plan and the history tracker, on the CPU: which stages a frame runs and may show, per mode, and
// when the modern stage's temporal history resets; the samples a frame traces (more on the window's reset
// frames) and the denoiser settings per mode and spp (the firefly filter and a lighter a-trous in exports
// from 16 spp).

#include "modern/modern_scene.hpp"
#include "render/stage_plan.hpp"
#include "show/director.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace {

using evo::render::HistoryKey;
using evo::render::HistoryReset;
using evo::render::HistoryTracker;
using evo::render::planStages;
using evo::render::RenderMode;
using evo::render::StagePlan;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::show::Transition;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr RenderMode kModes[] = {RenderMode::Interactive, RenderMode::Export};

TimelineFrame frameOf(int chapter, Effect effect, Transition transition, float era, float wipe = 0.f) {
    TimelineFrame frame;
    frame.chapter = chapter;
    frame.effect = effect;
    frame.transition = transition;
    frame.era = era;
    frame.wipe = wipe;
    frame.localSeconds = 1.0;
    return frame;
}

bool planIs(const StagePlan& plan, bool retroVisible, bool modernVisible, bool retro, bool modern) {
    return plan.retroVisible == retroVisible && plan.modernVisible == modernVisible && plan.retro == retro &&
           plan.modern == modern;
}

void testBoot() {
    const TimelineFrame boot = frameOf(0, Effect::Boot, Transition::Cut, 0.f);
    for (RenderMode mode : kModes) {
        CHECK(planIs(planStages(boot, mode, false), true, false, true, false));
        CHECK(planIs(planStages(boot, mode, true), true, false, true, false));
    }
}

void testStudioNight() {
    const TimelineFrame night = frameOf(11, Effect::StudioNight, Transition::Cut, 1.f);
    for (RenderMode mode : kModes) {
        CHECK(planIs(planStages(night, mode, false), false, true, false, true));
        // The retro stage runs for the modern stage's sake only, never shown.
        CHECK(planIs(planStages(night, mode, true), false, true, true, true));
    }
}

void testBeatSwap() {
    const TimelineFrame then = frameOf(2, Effect::Dots, Transition::BeatSwap, 0.f);
    CHECK(planIs(planStages(then, RenderMode::Export, false), true, false, true, false));
    // The window keeps the hidden 2026 side warm.
    CHECK(planIs(planStages(then, RenderMode::Interactive, false), true, false, true, true));

    const TimelineFrame now = frameOf(2, Effect::Dots, Transition::BeatSwap, 1.f);
    CHECK(planIs(planStages(now, RenderMode::Export, false), false, true, false, true));
    CHECK(planIs(planStages(now, RenderMode::Interactive, false), false, true, false, true));
    // era 0.5 is the 2026 side already, as in the compositor.
    CHECK(planIs(planStages(frameOf(2, Effect::Dots, Transition::BeatSwap, .5f), RenderMode::Export, false),
                 false, true, false, true));
}

void testWipe() {
    const TimelineFrame half = frameOf(3, Effect::Plasma, Transition::Wipe, .5f, .5f);
    for (RenderMode mode : kModes)
        CHECK(planIs(planStages(half, mode, false), true, true, true, true));
    // Before and after the wipe one side only; the window still runs the hidden 2026 side.
    const TimelineFrame before = frameOf(3, Effect::Plasma, Transition::Wipe, 0.f, 0.f);
    CHECK(planIs(planStages(before, RenderMode::Export, false), true, false, true, false));
    CHECK(planIs(planStages(before, RenderMode::Interactive, false), true, false, true, true));
    const TimelineFrame after = frameOf(3, Effect::Plasma, Transition::Wipe, 1.f, 1.f);
    for (RenderMode mode : kModes)
        CHECK(planIs(planStages(after, mode, false), false, true, false, true));
}

void testPortal() {
    const TimelineFrame mid = frameOf(1, Effect::Flyby, Transition::Portal, .3f);
    for (RenderMode mode : kModes)
        CHECK(planIs(planStages(mid, mode, false), true, true, true, true));
    const TimelineFrame then = frameOf(1, Effect::Flyby, Transition::Portal, 0.f);
    CHECK(planIs(planStages(then, RenderMode::Export, false), true, false, true, false));
    CHECK(planIs(planStages(then, RenderMode::Interactive, false), true, false, true, true));
    const TimelineFrame now = frameOf(1, Effect::Flyby, Transition::Portal, 1.f);
    for (RenderMode mode : kModes)
        CHECK(planIs(planStages(now, mode, false), false, true, false, true));
}

// EVO_FORCE_ERA=1 sets era = wipe = 1 on any chapter, BOOT included; EVO_FORCE_ERA=0 sets era = wipe = 0,
// a 2026-only chapter included.
void testForcedEra() {
    const TimelineFrame forced = frameOf(0, Effect::Boot, Transition::Cut, 1.f, 1.f);
    for (RenderMode mode : kModes)
        CHECK(planIs(planStages(forced, mode, false), false, true, false, true));

    // STUDIO NIGHT (Cut) held in 1993: retro only in both modes; a Cut never keeps 2026 warm.
    const TimelineFrame night = frameOf(11, Effect::StudioNight, Transition::Cut, 0.f, 0.f);
    for (RenderMode mode : kModes) {
        CHECK(planIs(planStages(night, mode, false), true, false, true, false));
        CHECK(planIs(planStages(night, mode, true), true, false, true, false));
    }

    // Portal and Wipe chapters held in either era.
    for (const Transition transition : {Transition::Portal, Transition::Wipe, Transition::BeatSwap}) {
        const TimelineFrame then = frameOf(1, Effect::Flyby, transition, 0.f, 0.f);
        CHECK(planIs(planStages(then, RenderMode::Export, false), true, false, true, false));
        CHECK(planIs(planStages(then, RenderMode::Interactive, false), true, false, true, true));
        const TimelineFrame now = frameOf(1, Effect::Flyby, transition, 1.f, 1.f);
        for (RenderMode mode : kModes) {
            CHECK(planIs(planStages(now, mode, false), false, true, false, true));
            CHECK(planIs(planStages(now, mode, true), false, true, true, true));
        }
    }
}

// What composite.comp reads, modelled independently of planStages: the weights the pixel centre x + 0.5 of
// a width-pixel output gives the 1993 screen and the 2026 image (as in the shader: FINALE the 2026 image
// whatever the era but on the thank-you card's frames, which select by era like a BeatSwap, Wipe by column with a 4-pixel refractive edge that shows the 1993 screen, Portal a mix by
// era, Cut and BeatSwap a select at era 0.5).
struct Weights {
    float then, now;
};

Weights compositeWeights(const TimelineFrame& frame, std::uint32_t x, std::uint32_t width) {
    if (frame.effect == Effect::Finale && !evo::show::thankYouCard(frame))
        return {0.f, 1.f};
    const float centre = static_cast<float>(x) + .5f;
    const float edge = frame.wipe * static_cast<float>(width);
    switch (frame.transition) {
    case Transition::Wipe: {
        const float fromEdge = centre - edge;
        if (frame.wipe > 0.f && frame.wipe < 1.f && fromEdge >= -2.f && fromEdge < 2.f)
            return {1.03f, 0.f}; // the glass edge: the 1993 side refracted and brightened
        return centre < edge ? Weights{0.f, 1.f} : Weights{1.f, 0.f};
    }
    case Transition::Portal: {
        const float t = std::clamp(frame.era, 0.f, 1.f);
        return {1.f - t, t};
    }
    case Transition::Cut:
    case Transition::BeatSwap:
        break;
    }
    return frame.era >= .5f ? Weights{0.f, 1.f} : Weights{1.f, 0.f};
}

// Every frame of the show at 1/60 s, as played and with either era forced: the plan never leaves out an
// image the compositor reads at any column of a 160-, 1280- or 3840-pixel-wide output, and a stage the
// compositor may show always runs.
void testPlanCoversCompositeAcrossTheShow() {
    std::size_t frames = 0;
    const auto check = [&](const TimelineFrame& frame) {
        bool thenRead = false, nowRead = false;
        for (const std::uint32_t width : {160u, 1280u, 3840u}) {
            // Cut, BeatSwap and Portal treat every column alike.
            const std::uint32_t columns = frame.transition == Transition::Wipe ? width : 1u;
            for (std::uint32_t x = 0; x < columns; ++x) {
                const Weights weights = compositeWeights(frame, x, width);
                thenRead = thenRead || weights.then > 0.f;
                nowRead = nowRead || weights.now > 0.f;
            }
        }
        for (RenderMode mode : kModes)
            for (const bool samplesRetro : {false, true}) {
                const StagePlan plan = planStages(frame, mode, samplesRetro);
                if ((thenRead && !plan.retroVisible) || (nowRead && !plan.modernVisible)) {
                    std::fprintf(stderr, "under-inclusive plan at t = %.4f (chapter %d)\n", frame.showSeconds,
                                 frame.chapter);
                    CHECK(false);
                }
                CHECK(!plan.retroVisible || plan.retro);
                CHECK(!plan.modernVisible || plan.modern);
                CHECK(!samplesRetro || plan.retro);
            }
        ++frames;
    };
    const double duration = evo::show::showDuration();
    for (long i = 0; i / 60.0 < duration; ++i) {
        TimelineFrame frame = evo::show::sampleTimeline(i / 60.0);
        check(frame);
        for (const float era : {0.f, 1.f}) {
            frame.era = frame.wipe = era; // EVO_FORCE_ERA
            frame.eraCut = 0.f;
            check(frame);
        }
    }
    std::printf("plan sweep: %zu frames\n", frames);
    CHECK(frames > 3 * 60 * 600);
}

// FINALE shows the 2026 picture whatever its era (the megamix's cuts and the room are both 2026); the retro
// stage runs only for the Studio's CRT. Across the chapter at 1/60 s: the retro stage runs exactly while the
// frame shows the Studio (the room), never in a cut to another scene.
void testFinale() {
    for (const float era : {0.f, 1.f})
        for (RenderMode mode : kModes) {
            const TimelineFrame finale = frameOf(9, Effect::Finale, Transition::BeatSwap, era);
            CHECK(planIs(planStages(finale, mode, false), false, true, false, true));
            CHECK(planIs(planStages(finale, mode, true), false, true, true, true));
        }

    int room = 0, cuts = 0, card = 0;
    const double start = evo::show::chapterStart(9), end = evo::show::chapterStart(10);
    for (double t = start; t < end; t += 1.0 / 60) {
        const TimelineFrame frame = evo::show::sampleTimeline(t);
        const TimelineFrame scene = evo::show::sceneFrame(frame);
        // What the renderer asks the modern stage: whether the scene shown samples the retro image.
        const bool studio = evo::modern::sceneFor(scene.effect) == evo::modern::SceneId::Studio;
        CHECK(studio == !evo::show::sceneCut(frame).has_value());
        if (evo::show::thankYouCard(frame)) {
            // The thank-you card: the 1993 picture over the whole frame (era 0), the room behind it hidden;
            // the window keeps the Studio's history warm, as through any BeatSwap.
            CHECK(studio && frame.era == 0.f);
            for (RenderMode mode : kModes) {
                const StagePlan plan = planStages(frame, mode, studio);
                CHECK(plan.retroVisible && plan.retro && !plan.modernVisible);
                CHECK(plan.modern == (mode == RenderMode::Interactive));
            }
            ++card;
            continue;
        }
        for (RenderMode mode : kModes) {
            const StagePlan plan = planStages(frame, mode, studio);
            CHECK(plan.modernVisible && plan.modern && !plan.retroVisible);
            CHECK(plan.retro == studio);
        }
        (studio ? room : cuts) += 1;
    }
    std::printf("FINALE: %d room frames, %d cut frames, %d card frames\n", room, cuts, card);
    CHECK(cuts > 60 * 3 && room > 60 * 7 && card == evo::show::kThankYouFrames);
}

void testBounces() {
    CHECK(evo::render::resolveMaxBounces(RenderMode::Interactive, 0) == 4);
    CHECK(evo::render::resolveMaxBounces(RenderMode::Export, 0) == 6);
    CHECK(evo::render::resolveMaxBounces(RenderMode::Interactive, 7) == 7);
    CHECK(evo::render::resolveMaxBounces(RenderMode::Export, 2) == 2);
}

HistoryKey key(double showSeconds) {
    return {11, 0, showSeconds, 320, 180, RenderMode::Interactive, 1};
}

// A tracker that has seen a first frame at key(1.0).
HistoryTracker started() {
    HistoryTracker tracker;
    CHECK(tracker.advance(key(1.0), false) == HistoryReset::FirstFrame);
    CHECK(tracker.framesSinceReset() == 0);
    return tracker;
}

void testHistoryReasons() {
    {
        HistoryTracker tracker = started();
        CHECK(tracker.advance(key(1.0 + 1.0 / 60), false) == HistoryReset::None);
        CHECK(tracker.framesSinceReset() == 1);
    }
    {
        HistoryTracker tracker = started();
        HistoryKey resized = key(1.0);
        resized.width = 640;
        CHECK(tracker.advance(resized, false) == HistoryReset::Resize);
        resized.height = 360;
        CHECK(tracker.advance(resized, false) == HistoryReset::Resize);
    }
    {
        HistoryTracker tracker = started();
        HistoryKey export_ = key(1.0);
        export_.mode = RenderMode::Export;
        CHECK(tracker.advance(export_, false) == HistoryReset::Settings);
        export_.spp = 2;
        CHECK(tracker.advance(export_, false) == HistoryReset::Settings);
    }
    {
        HistoryTracker tracker = started();
        HistoryKey next = key(1.0);
        next.chapter = 12;
        CHECK(tracker.advance(next, false) == HistoryReset::Chapter);
    }
    {
        HistoryTracker tracker = started();
        HistoryKey cut = key(1.0);
        cut.shot = 1;
        CHECK(tracker.advance(cut, false) == HistoryReset::Shot);
    }
    {
        HistoryTracker tracker = started();
        CHECK(tracker.advance(key(1.0), true) == HistoryReset::Skipped);
    }
    {
        HistoryTracker tracker = started();
        CHECK(tracker.advance(key(1.0), false) == HistoryReset::None);
        tracker.invalidate();
        CHECK(tracker.advance(key(1.0), false) == HistoryReset::FirstFrame);
        CHECK(tracker.framesSinceReset() == 0);
    }
}

// Two reasons on one frame: the earlier in the order FirstFrame, Resize, Settings, Chapter, Shot, TimeJump,
// Skipped is reported.
void testResetPrecedence() {
    {
        HistoryTracker tracker = started();
        HistoryKey next = key(1.0);
        next.width = 640;
        next.chapter = 12;
        CHECK(tracker.advance(next, false) == HistoryReset::Resize);
    }
    {
        HistoryTracker tracker = started();
        HistoryKey next = key(1.0);
        next.chapter = 12;
        next.shot = 3;
        CHECK(tracker.advance(next, false) == HistoryReset::Chapter);
    }
    {
        HistoryTracker tracker = started();
        HistoryKey next = key(9.0);
        next.shot = 1;
        CHECK(tracker.advance(next, false) == HistoryReset::Shot);
    }
    {
        HistoryTracker tracker = started();
        CHECK(tracker.advance(key(0.5), true) == HistoryReset::TimeJump);
    }
    {
        HistoryTracker tracker = started();
        HistoryKey next = key(1.0);
        next.spp = 4;
        next.height = 360;
        CHECK(tracker.advance(next, false) == HistoryReset::Resize);
    }
    {
        // Everything at once on a first frame is still FirstFrame.
        HistoryTracker tracker;
        CHECK(tracker.advance(key(1.0), true) == HistoryReset::FirstFrame);
    }
}

// A resize the tracker was told about counts even when the size is back to the last one when the modern
// stage runs again (A -> B -> A with no frame in between).
void testMarkedResize() {
    {
        HistoryTracker tracker = started();
        tracker.markResize();
        tracker.markResize();
        CHECK(tracker.advance(key(1.0 + 1.0 / 60), false) == HistoryReset::Resize);
        CHECK(tracker.framesSinceReset() == 0);
        CHECK(tracker.advance(key(1.0 + 2.0 / 60), false) == HistoryReset::None); // reported once
    }
    {
        // Before the first frame the reason is FirstFrame, and the mark is used up by it.
        HistoryTracker tracker;
        tracker.markResize();
        CHECK(tracker.advance(key(1.0), false) == HistoryReset::FirstFrame);
        CHECK(tracker.advance(key(1.0), false) == HistoryReset::None);
    }
    {
        // invalidate() wins over a marked resize, and clears it.
        HistoryTracker tracker = started();
        tracker.markResize();
        tracker.invalidate();
        CHECK(tracker.advance(key(1.0), false) == HistoryReset::FirstFrame);
        CHECK(tracker.advance(key(1.0), false) == HistoryReset::None);
    }
}

// Δt = 0.5 is no jump, 0.5001 is; a repeated time (Δt = 0) is none, any step back is one.
void testTimeJumps() {
    HistoryTracker tracker = started();
    CHECK(tracker.advance(key(1.5), false) == HistoryReset::None); // exactly 0.5
    CHECK(tracker.advance(key(2.0001), false) == HistoryReset::TimeJump);
    CHECK(tracker.advance(key(2.0001), false) == HistoryReset::None);
    CHECK(tracker.advance(key(2.0001 - 1.0 / 60), false) == HistoryReset::TimeJump);
    CHECK(tracker.advance(key(2.0001), false) == HistoryReset::None); // +1/60 from the last frame
}

void testFramesSinceReset() {
    HistoryTracker tracker = started();
    for (std::uint32_t i = 1; i <= 3; ++i) {
        CHECK(tracker.advance(key(1.0 + i / 60.0), false) == HistoryReset::None);
        CHECK(tracker.framesSinceReset() == i);
    }
    HistoryKey next = key(1.0 + 4 / 60.0);
    next.chapter = 12;
    CHECK(tracker.advance(next, false) == HistoryReset::Chapter);
    CHECK(tracker.framesSinceReset() == 0);
    next.showSeconds += 1 / 60.0;
    CHECK(tracker.advance(next, false) == HistoryReset::None);
    CHECK(tracker.framesSinceReset() == 1);
}

// The window traces kResetSpp {4, 2} on the first two frames of a history (more if --spp asks), then --spp;
// exports always --spp.
void testResetSpp() {
    using evo::render::frameSpp;
    CHECK(evo::render::kResetSpp[0] == 4 && evo::render::kResetSpp[1] == 2);
    CHECK(frameSpp(RenderMode::Interactive, 1, 0) == 4);
    CHECK(frameSpp(RenderMode::Interactive, 1, 1) == 2);
    CHECK(frameSpp(RenderMode::Interactive, 1, 2) == 1);
    CHECK(frameSpp(RenderMode::Interactive, 1, 1000) == 1);
    CHECK(frameSpp(RenderMode::Interactive, 3, 0) == 4 && frameSpp(RenderMode::Interactive, 3, 1) == 3);
    CHECK(frameSpp(RenderMode::Interactive, 8, 0) == 8);
    for (const std::uint32_t frames : {0u, 1u, 2u})
        CHECK(frameSpp(RenderMode::Export, 1, frames) == 1 && frameSpp(RenderMode::Export, 64, frames) == 64);
}

// The window: temporal, a-trous always, no firefly filter at any spp. Exports: no history, 3 a-trous
// iterations (5 at history length <= 3) below 16 spp; from 16 the firefly filter and a light a-trous of 2
// iterations in all. Both: 4 on the volume layer.
void testDenoiseSettings() {
    using evo::render::denoiseSettingsFor;
    evo::render::HistoryState state;
    for (const std::uint32_t spp : {1u, 4u, 15u, 16u, 64u}) {
        state.mode = RenderMode::Interactive;
        state.spp = spp;
        const auto window = denoiseSettingsFor(state);
        CHECK(window.temporal && window.atrousIterations == 3 && !window.fireflyFilter);
        CHECK(window.resetAtrousIterations == 5 && window.volumeAtrousIterations == 4);
        state.mode = RenderMode::Export;
        const auto exported = denoiseSettingsFor(state);
        CHECK(!exported.temporal && exported.atrousIterations == (spp < 16 ? 3u : 2u));
        CHECK(exported.fireflyFilter == (spp >= 16) && exported.fireflyK == 4.f);
        CHECK(exported.resetAtrousIterations == (spp < 16 ? 5u : 2u) && exported.volumeAtrousIterations == 4);
    }
}

} // namespace

int main() {
    try {
        testBoot();
        testStudioNight();
        testBeatSwap();
        testWipe();
        testPortal();
        testForcedEra();
        testPlanCoversCompositeAcrossTheShow();
        testFinale();
        testBounces();
        testHistoryReasons();
        testResetPrecedence();
        testMarkedResize();
        testTimeJumps();
        testFramesSinceReset();
        testResetSpp();
        testDenoiseSettings();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("stage_plan_test: %d checks passed\n", checksPassed);
    return 0;
}
