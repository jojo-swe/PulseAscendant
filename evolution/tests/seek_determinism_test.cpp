// Seeking with temporal accumulation on (Task 12 of step 4, Review Focus 1). The window renders Interactive:
// 1 spp per frame, accumulated over the frames since the last history reset. Whenever the user seeks (keys
// <- / -> and 1-9, 0 jump to chapter starts; the show loops at its end), the first frame after the reset must
// not show the old chapter or the old camera, and every frame from the seek on must equal, byte for byte, the
// frame a fresh renderer gives when it starts playing at the same point. The test checks this at 160x90 and
// 1 spp (4 bounces, the window's defaults) for:
//   - a renderer that plays RETURN (7 frames: an odd count, so the ring slot differs from a fresh renderer's)
//     and then seeks back into STUDIO NIGHT, on to REBOOT (1993 and the Studio through the portal
//     cross-fade), back to FINALE (BeatSwap, the megamix on the CRT), into PLASMA (a 1993 chapter with the
//     placeholder wiping in), to the start of STUDIO NIGHT (where the window's keys seek), then forwards
//     inside the same camera shot and back again inside it (both TimeJump);
//   - the STUDIO NIGHT camera cut at the accent: the frame of the cut resets (Shot) and from there equals a
//     fresh renderer started on it;
//   - a duplicated frame (dt = 0) continues the history, a step of -1/60 s resets it (TimeJump: time running
//     backwards, as at the show's loop) and equals a fresh renderer there;
//   - BeatSwap in the window: on the 1993 beats of DOTS (placeholder) and FINALE (the Studio) the modern
//     stage keeps running unseen (Decision 7) and the history never resets, while exports skip it and
//     report Skipped when it runs again; the window's 1993 beats equal the export's bytes.
// Runs with the validation layer on and fails on any message it reports.

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "modern/modern_scene.hpp"
#include "modern/studio.hpp"
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
using evo::show::Effect;
using evo::show::TimelineFrame;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr std::uint32_t kWidth = 160, kHeight = 90;
constexpr double kFrame = 1.0 / 60.0;
constexpr int kDots = 2, kPlasma = 3, kFinale = 9, kStudioNight = 11, kReturn = 19, kReboot = 20;

double chapterTime(int chapter, double fraction) {
    const double start = evo::show::chapterStart(chapter);
    return start + fraction * (evo::show::chapterStart(chapter + 1) - start);
}

RenderSettings settings(RenderMode mode) {
    RenderSettings s{kWidth, kHeight}; // CRT and HUD on; the window's spp (1) and bounces (0 = the mode's)
    s.mode = mode;
    return s;
}

struct Frame {
    std::vector<std::uint8_t> rgb;
    Renderer::Stats stats;
};

// One renderer with its own ring and readback, played frame by frame at given show times.
class Harness {
public:
    Harness(evo::gfx::Context& ctx, RenderMode mode)
        : readback_(ctx, kWidth, kHeight), ring_(ctx), renderer_(ctx, ring_, settings(mode)) {}

    void prepare() { renderer_.prepare(); } // as the window does before its first frame

    Frame render(double showSeconds) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, evo::show::sampleTimeline(showSeconds));
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return {readback_.rgb(), renderer_.lastStats()};
    }
    // `count` frames 1/60 s apart from `start`.
    std::vector<Frame> play(double start, int count) {
        std::vector<Frame> frames;
        for (int i = 0; i < count; ++i)
            frames.push_back(render(start + i * kFrame));
        return frames;
    }

private:
    evo::gfx::Readback readback_;
    evo::gfx::FrameRing ring_;
    Renderer renderer_; // last: its destructor waits for the ring
};

// What a fresh window renderer plays from `start`: the reference a seek must reproduce.
std::vector<Frame> freshPlay(evo::gfx::Context& ctx, double start, int count) {
    Harness fresh(ctx, RenderMode::Interactive);
    std::vector<Frame> frames = fresh.play(start, count);
    CHECK(frames.front().stats.historyReset == HistoryReset::FirstFrame ||
          !frames.front().stats.modernRendered);
    return frames;
}

// The seeker's frames from a seek on equal the fresh reference byte for byte; the seek frame resets with
// `reset` (when the modern stage runs) and the history then counts on as in the fresh renderer.
void checkSeek(Harness& seeker, double start, const std::vector<Frame>& reference, HistoryReset reset) {
    for (std::size_t i = 0; i < reference.size(); ++i) {
        const Frame frame = seeker.render(start + static_cast<double>(i) * kFrame);
        const Renderer::Stats& want = reference[i].stats;
        CHECK(frame.stats.modernRendered == want.modernRendered);
        CHECK(frame.stats.retroRendered == want.retroRendered);
        CHECK(frame.stats.historyReset == (i == 0 && want.modernRendered ? reset : want.historyReset));
        CHECK(frame.stats.framesSinceReset == want.framesSinceReset);
        CHECK(frame.stats.frameSeed == want.frameSeed);
        CHECK(frame.rgb == reference[i].rgb);
    }
}

// The plan's case and its journey through the show: every seek lands on a fresh renderer's frames.
void testSeeks(evo::gfx::Context& ctx) {
    const double night = chapterTime(kStudioNight, 0.4); // shot 0, the dolly towards the desk
    const double reboot = chapterTime(kReboot, 0.73);    // the portal: 1993 and the Studio both visible
    const double finale = chapterTime(kFinale, 0.5);     // BeatSwap, the megamix on the CRT
    const double plasma = chapterTime(kPlasma, 0.45);    // the wipe: 1993 and the placeholder
    const double nightStart = evo::show::chapterStart(kStudioNight); // where the window's keys seek to
    const double nightEarly = chapterTime(kStudioNight, 0.1);

    const TimelineFrame rebootFrame = evo::show::sampleTimeline(reboot);
    CHECK(rebootFrame.effect == Effect::Reboot && rebootFrame.era > 0.f && rebootFrame.era < 1.f);
    const TimelineFrame plasmaFrame = evo::show::sampleTimeline(plasma);
    CHECK(plasmaFrame.effect == Effect::Plasma && plasmaFrame.wipe > 0.f && plasmaFrame.wipe < 1.f);
    CHECK(evo::show::sampleTimeline(finale).effect == Effect::Finale);
    // STUDIO NIGHT from `night` stays in one camera shot for the 12 frames, and so does the early jump.
    const std::uint32_t shot = evo::modern::studioCamera(evo::show::sampleTimeline(night)).shot;
    CHECK(evo::modern::studioCamera(evo::show::sampleTimeline(night + 11 * kFrame)).shot == shot);
    CHECK(evo::modern::studioCamera(evo::show::sampleTimeline(nightEarly)).shot == shot);

    const std::vector<Frame> nightRef = freshPlay(ctx, night, 12);
    const std::vector<Frame> rebootRef = freshPlay(ctx, reboot, 6);
    const std::vector<Frame> finaleRef = freshPlay(ctx, finale, 6);
    const std::vector<Frame> plasmaRef = freshPlay(ctx, plasma, 4);
    const std::vector<Frame> nightStartRef = freshPlay(ctx, nightStart, 4);
    // The Studio chapters run both stages: the path tracer and the 1993 picture on its CRT.
    for (const std::vector<Frame>* ref : {&nightRef, &rebootRef, &finaleRef, &nightStartRef})
        CHECK(ref->front().stats.modernRendered && ref->front().stats.retroRendered);
    // The references are path traced and accumulate: consecutive frames differ.
    CHECK(nightRef[0].rgb != nightRef[1].rgb && nightRef[10].rgb != nightRef[11].rgb);

    Harness seeker(ctx, RenderMode::Interactive);
    seeker.prepare();
    const std::vector<Frame> played = seeker.play(chapterTime(kReturn, 0.5), 7); // another chapter's camera
    CHECK(played.back().stats.modernRendered && played.back().stats.framesSinceReset == 6);

    checkSeek(seeker, night, nightRef, HistoryReset::Chapter);           // back to STUDIO NIGHT
    checkSeek(seeker, reboot, rebootRef, HistoryReset::Chapter);         // forward to REBOOT
    checkSeek(seeker, finale, finaleRef, HistoryReset::Chapter);         // back to FINALE
    checkSeek(seeker, plasma, plasmaRef, HistoryReset::Chapter);         // a 1993 chapter
    checkSeek(seeker, nightStart, nightStartRef, HistoryReset::Chapter); // a chapter key: its start
    seeker.play(nightEarly, 3);                                          // into the same shot, early on
    checkSeek(seeker, night, nightRef, HistoryReset::TimeJump);          // forward inside the shot
    checkSeek(seeker, night, nightRef, HistoryReset::TimeJump); // backward inside the shot (-11/60 s)
}

// The STUDIO NIGHT cut at the accent: the frame of the cut resets (Shot), and from there the window's frames
// equal a fresh renderer's started on that frame. The frames before the cut continue the history.
void testShotCut(evo::gfx::Context& ctx) {
    const double accent = evo::show::chapterStart(kStudioNight) +
                          evo::show::sampleTimeline(chapterTime(kStudioNight, 0.5)).accentSeconds;
    const double start = accent - 3 * kFrame;
    int cut = -1;
    for (int i = 1; i < 8 && cut < 0; ++i) {
        const auto shotAt = [&](int k) {
            return evo::modern::studioCamera(evo::show::sampleTimeline(start + k * kFrame)).shot;
        };
        if (shotAt(i) != shotAt(i - 1))
            cut = i;
    }
    CHECK(cut > 0); // the shot changes within the frames played

    Harness window(ctx, RenderMode::Interactive);
    const std::vector<Frame> before = window.play(start, cut);
    for (int i = 1; i < cut; ++i)
        CHECK(before[i].stats.historyReset == HistoryReset::None);
    const double cutTime = start + cut * kFrame;
    checkSeek(window, cutTime, freshPlay(ctx, cutTime, 4), HistoryReset::Shot);
}

// A duplicated frame (dt = 0, a stalled clock) continues the history; time running backwards by one frame
// resets it (TimeJump) and the frame equals a fresh renderer's.
void testDuplicateAndBackwards(evo::gfx::Context& ctx) {
    const double t = chapterTime(kStudioNight, 0.3);
    Harness window(ctx, RenderMode::Interactive);
    const Frame first = window.render(t);
    const Frame duplicate = window.render(t);
    CHECK(duplicate.stats.historyReset == HistoryReset::None && duplicate.stats.framesSinceReset == 1);
    CHECK(duplicate.rgb != first.rgb); // a second sample accumulated, with the next seed
    const Frame next = window.render(t + kFrame);
    CHECK(next.stats.historyReset == HistoryReset::None && next.stats.framesSinceReset == 2);
    checkSeek(window, t, freshPlay(ctx, t, 3), HistoryReset::TimeJump);
}

// The first frame at or after `from` (stepping 1/60 s) whose era is `era`, within the chapter.
double firstFrameWithEra(int chapter, double from, float era) {
    for (double t = from; t < evo::show::chapterStart(chapter + 1); t += kFrame) {
        const TimelineFrame frame = evo::show::sampleTimeline(t);
        if (frame.transition == evo::show::Transition::BeatSwap && frame.era == era)
            return t;
    }
    throw std::runtime_error("no frame with that era in the chapter");
}

// BeatSwap in the window (Decision 7): the hidden 2026 side keeps tracing on the 1993 beats, so the history
// never resets across the swaps; exports skip it there and report Skipped when it runs again. A 1993 beat
// looks the same in both modes.
void testBeatSwap(evo::gfx::Context& ctx) {
    for (const int chapter : {kDots, kFinale}) {
        // From a 2026 beat over the next 1993 beat into the next 2026 beat, frames 0.1 s apart (no time jump
        // in the window; a beat lasts about 0.42 s, so every beat gets frames).
        const double nowBeat = firstFrameWithEra(chapter, chapterTime(chapter, 0.3), 1.f);
        const double retroBeat = firstFrameWithEra(chapter, nowBeat, 0.f);
        const double nextNowBeat = firstFrameWithEra(chapter, retroBeat, 1.f);
        std::vector<double> times;
        for (double t = nowBeat; t < nextNowBeat + 0.15; t += 0.1)
            times.push_back(t);
        Harness window(ctx, RenderMode::Interactive);
        Harness exported(ctx, RenderMode::Export);
        bool sawRetro = false, sawSkipped = false;
        bool exportRan = false, previousExportModern = false;
        for (std::size_t i = 0; i < times.size(); ++i) {
            const TimelineFrame frame = evo::show::sampleTimeline(times[i]);
            const Frame w = window.render(times[i]);
            const Frame e = exported.render(times[i]);
            CHECK(w.stats.modernRendered); // always in the window
            CHECK(w.stats.historyReset == (i == 0 ? HistoryReset::FirstFrame : HistoryReset::None));
            CHECK(w.stats.framesSinceReset == i);
            CHECK(e.stats.modernRendered == (frame.era >= .5f)); // only the 2026 beats in exports
            if (frame.era < .5f) {
                sawRetro = true;
                CHECK(w.rgb == e.rgb); // the unseen 2026 side does not reach the picture
            } else if (!exportRan) {
                CHECK(e.stats.historyReset == HistoryReset::FirstFrame);
            } else if (!previousExportModern) {
                // Skipped, or TimeJump when the 1993 stretch lasted over 0.5 s (TimeJump takes precedence).
                sawSkipped = true;
                CHECK(e.stats.historyReset == HistoryReset::Skipped ||
                      e.stats.historyReset == HistoryReset::TimeJump);
            }
            exportRan = exportRan || e.stats.modernRendered;
            previousExportModern = e.stats.modernRendered;
        }
        CHECK(sawRetro && sawSkipped);
    }
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        for (const Effect effect : {Effect::Finale, Effect::StudioNight, Effect::Return, Effect::Reboot})
            CHECK(evo::modern::sceneFor(effect) == evo::modern::SceneId::Studio);
        CHECK(evo::modern::sceneFor(Effect::Plasma) == evo::modern::SceneId::Placeholder);
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testSeeks(ctx);
            testShotCut(ctx);
            testDuplicateAndBackwards(ctx);
            testBeatSwap(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("seek_determinism_test: %d checks passed\n", checksPassed);
    return 0;
}
