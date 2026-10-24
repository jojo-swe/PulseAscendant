// The stage plan and the history tracker, on the CPU: which stages a frame runs and may show, per mode, and
// when the modern stage's temporal history resets.

#include "render/stage_plan.hpp"
#include "show/timeline.hpp"

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
    // The window keeps the hidden 2026 side warm (Decision 7).
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

// EVO_FORCE_ERA=1 sets era = wipe = 1 on any chapter, BOOT included.
void testForcedEra() {
    const TimelineFrame forced = frameOf(0, Effect::Boot, Transition::Cut, 1.f, 1.f);
    for (RenderMode mode : kModes)
        CHECK(planIs(planStages(forced, mode, false), false, true, false, true));
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

} // namespace

int main() {
    try {
        testBoot();
        testStudioNight();
        testBeatSwap();
        testWipe();
        testPortal();
        testForcedEra();
        testBounces();
        testHistoryReasons();
        testTimeJumps();
        testFramesSinceReset();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("stage_plan_test: %d checks passed\n", checksPassed);
    return 0;
}
