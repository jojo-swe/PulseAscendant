// post::Look on the CPU: the acts by chapter and their grades, the eased joins at STUDIO NIGHT and REBOOT,
// the authored exposure (a key within a frame of every directed event, continuous at 60 fps with at most 0.05
// EV per frame except on cuts, zero slope at every key, RETURN -> REBOOT one continuous exposure), the bloom
// and the palette weight around each CRT crossing, the crossing palettes, and the grain's frame clock.

#include "modern/studio.hpp"
#include "post/look.hpp"
#include "show/cues.hpp"
#include "show/director.hpp"
#include "show/era.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr double kFrame = 1.0 / 60.0;

bool near(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

double middleOf(int chapter) {
    return 0.5 * (show::chapterStart(chapter) + show::chapterStart(chapter + 1));
}

bool sameGrade(const post::Grade& a, const post::Grade& b, float tolerance) {
    return near(a.contrast, b.contrast, tolerance) && near(a.lift[0], b.lift[0], tolerance) &&
           near(a.lift[1], b.lift[1], tolerance) && near(a.lift[2], b.lift[2], tolerance) &&
           near(a.splitTone, b.splitTone, tolerance) && near(a.shadowDesaturation, b.shadowDesaturation, tolerance) &&
           near(a.grain, b.grain, tolerance);
}

// (a) The acts and their grades.
void testActs() {
    for (int c = 0; c < show::chapterCount(); ++c)
        CHECK(post::actOf(c) == (c <= 10 ? 1 : c <= 19 ? 2 : c <= 28 ? 3 : 0));
    const post::Look one = post::actLook(1), two = post::actLook(2), three = post::actLook(3);
    CHECK(one.bloom == 0.020f && one.grade.splitTone == 0.02f && one.grade.grain == 0.004f);
    CHECK(one.grade.contrast == 0.f && one.grade.lift[2] == 0.f && one.grade.shadowDesaturation == 0.f);
    CHECK(two.bloom == 0.025f && two.grade.contrast == 0.05f && two.grade.grain == 0.006f);
    CHECK(two.grade.lift[2] == 0.004f && near(two.grade.lift[0], 0.0024, 1e-7) && near(two.grade.lift[1], 0.0032, 1e-7));
    CHECK(three.bloom == 0.035f && three.grade.shadowDesaturation == 0.08f && three.grade.grain == 0.003f);
    CHECK(three.grade.contrast == 0.f && three.grade.splitTone == 0.f);
    // Mid-chapter, away from joins and crossings, the frame's look is its act's.
    for (int c : {2, 5, 7, 13, 16, 22, 26}) {
        const post::Look look = post::lookFor(show::sampleTimeline(middleOf(c)));
        CHECK(look.act == post::actOf(c));
        CHECK(sameGrade(look.grade, post::actLook(look.act).grade, 0.f));
        CHECK(look.bloom == post::actLook(look.act).bloom && look.grade.palette == 0.f);
    }
    for (int c : {29, 30}) {
        const post::Look look = post::lookFor(show::sampleTimeline(middleOf(c)));
        CHECK(look.act == 0 && look.bloom == 0.f && look.grade.grain == 0.f && look.exposureOffset == 0.f);
    }
    std::printf("(a) acts by chapter and their grades\n");
}

// (b) STUDIO NIGHT and REBOOT ease from the previous act over their first 2 bars.
void testJoins() {
    for (int c : {11, 20}) {
        const double start = show::chapterStart(c);
        const show::TimelineFrame first = show::sampleTimeline(start);
        const double bar = show::transitionMarks(first).bar;
        const post::Look before = post::actLook(post::actOf(c) - 1), after = post::actLook(post::actOf(c));
        CHECK(sameGrade(post::lookFor(first).grade, before.grade, 1e-6f));
        CHECK(sameGrade(post::lookFor(show::sampleTimeline(start + 2 * bar + kFrame)).grade, after.grade, 0.f));
        const post::Look half = post::lookFor(show::sampleTimeline(start + bar));
        CHECK(near(half.grade.grain, 0.5 * (before.grade.grain + after.grade.grain), 1e-4));
        float previous = post::lookFor(first).grade.grain;
        for (double t = start; t < start + 2 * bar; t += kFrame) {
            const float grain = post::lookFor(show::sampleTimeline(t)).grade.grain;
            CHECK((grain - previous) * (after.grade.grain - before.grade.grain) >= -1e-9);
            previous = grain;
        }
        std::printf("(b) chapter %d eases its look in over %.2f s\n", c, 2 * bar);
    }
}

// (c) The exposure keys.
void testExposure() {
    const auto keys = post::exposureKeys();
    // A key within one frame of every directed event.
    for (double event : {145.80, 149.19, 209.25, 209.80, 378.0, 390.64, 394.0, 401.69, 512.92, 516.40, 520.0, 618.41,
                         664.97, 667.72}) {
        const bool found = std::any_of(keys.begin(), keys.end(),
                                       [&](const post::ExposureKey& k) { return near(k.seconds, event, kFrame); });
        if (!found)
            std::printf("no key near %.2f\n", event);
        CHECK(found);
    }
    // Jumps (two keys at one time) only on a chapter start: a cut, or the Studio's RETURN -> REBOOT join
    // where the scene's own exposure takes over the step.
    std::vector<double> jumps;
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        CHECK(keys[i].seconds <= keys[i + 1].seconds);
        if (keys[i].seconds == keys[i + 1].seconds) {
            jumps.push_back(keys[i].seconds);
            bool chapterStart = false;
            for (int c = 1; c < show::chapterCount(); ++c)
                chapterStart = chapterStart || keys[i].seconds == show::chapterStart(c);
            CHECK(chapterStart);
        }
    }
    // RETURN -> REBOOT: the Studio's total exposure is continuous across the join.
    const double join = show::chapterStart(20);
    const double returnTotal =
        modern::studioExposureEV(show::Effect::Return) + post::exposureOffset(join - 1e-6);
    const double rebootTotal = modern::studioExposureEV(show::Effect::Reboot) + post::exposureOffset(join);
    CHECK(near(returnTotal, rebootTotal, 1e-6));

    // Continuous at 60 fps: at most 0.05 EV per frame, except across a jump.
    double worst = 0, worstAt = 0;
    const double end = show::showDuration();
    for (double t = 0; t + kFrame < end; t += kFrame) {
        const double next = t + kFrame;
        const bool cut = std::any_of(jumps.begin(), jumps.end(), [&](double j) { return j > t && j <= next; });
        const double step = std::abs(post::exposureOffset(next) - post::exposureOffset(t));
        if (!cut && step > worst) {
            worst = step;
            worstAt = t;
        }
    }
    std::printf("(c) %zu keys, %zu jumps (all on chapter starts); the steepest frame %.4f EV at %.2f s\n", keys.size(),
                jumps.size(), worst, worstAt);
    CHECK(worst <= 0.05);
    // Zero slope at every key (on the side of a jump that continues the curve too).
    for (const post::ExposureKey& k : keys) {
        constexpr double h = 1e-4;
        const double here = post::exposureOffset(k.seconds);
        if (k.seconds > h) {
            const double before = post::exposureOffset(k.seconds - h);
            const bool jumpHere = std::find(jumps.begin(), jumps.end(), k.seconds) != jumps.end();
            if (!jumpHere)
                CHECK(std::abs(here - before) / h <= 0.05);
        }
        CHECK(std::abs(post::exposureOffset(k.seconds + h) - here) / h <= 0.05);
    }
    // The easing itself: 0 and 1 at the ends, zero slope there, monotonic, peak slope 4/3.
    CHECK(post::easedCruise(0) == 0.0 && near(post::easedCruise(1), 1.0, 1e-12));
    double peak = 0, previous = 0;
    for (int i = 1; i <= 10000; ++i) {
        const double v = post::easedCruise(i / 10000.0);
        CHECK(v >= previous);
        peak = std::max(peak, (v - previous) * 10000.0);
        previous = v;
    }
    CHECK(near(peak, 4.0 / 3.0, 1e-3));
    CHECK(post::easedCruise(1e-4) < 1e-6 && 1.0 - post::easedCruise(1 - 1e-4) < 1e-6);
    // The values the direction asks for.
    CHECK(near(post::exposureOffset(show::chapterStart(8) - 1e-6), -1.0, 1e-4));
    CHECK(near(post::exposureOffset(212.0), 0.7, 1e-6));
    CHECK(near(post::exposureOffset(show::chapterStart(19)), 6.0, 1e-6));
    CHECK(near(post::exposureOffset(398.0), 0.0, 1e-6));
    CHECK(near(post::exposureOffset(show::chapterStart(24)), 6.0, 1e-6));
    CHECK(near(post::exposureOffset(530.0), 0.0, 1e-6));
    CHECK(post::exposureOffset(618.41 + 0.217) > 0.29f);
    CHECK(post::exposureOffset(559.41) == 0.f); // LENS OF GOD's clip is bloom only
}

// (d) Around each crossing: the bloom gives way on the glass and returns by D + 1 bar; the palette weight is
// 1 at D and 0 by D + 1 bar, and never on PORTAL's way out.
void testCrossings() {
    for (int c : {1, 4, 10, 20}) {
        const show::TimelineFrame mid = show::sampleTimeline(middleOf(c));
        CHECK(mid.transition == show::Transition::Portal);
        const show::TransitionMarks m = show::transitionMarks(mid);
        const double d = m.downbeat, bar = m.bar;
        const auto at = [](double t) { return show::sampleTimeline(t); };
        CHECK(post::portalBloomScale(at(d - 1.0)) == 1.f);
        CHECK(near(post::portalBloomScale(at(d - 0.25)), 0.5, 1e-3));
        CHECK(post::portalBloomScale(at(d)) == 0.f);
        CHECK(near(post::portalBloomScale(at(d + 0.5 * bar)), 0.5, 1e-3));
        CHECK(post::portalBloomScale(at(d + bar)) == 1.f);
        CHECK(post::paletteWeight(at(d - 0.01)) == 0.f);
        CHECK(post::paletteWeight(at(d)) == 1.f);
        CHECK(near(post::paletteWeight(at(d + 0.5 * bar)), 0.5, 1e-3));
        CHECK(post::paletteWeight(at(d + bar)) == 0.f);
        const post::Look atGlass = post::lookFor(at(d));
        CHECK(atGlass.grade.palette == 1.f && atGlass.bloom == 0.f);
        // The palette is the 1993 effect's own at D's retro frame: not all black.
        int coloured = 0;
        for (std::uint32_t entry : atGlass.palette)
            coloured += (entry & 0xffffffu) != 0 ? 1 : 0;
        std::printf("(d) chapter %d: D %.2f, bar %.3f s, the crossing palette has %d coloured entries\n", c, d, bar,
                    coloured);
        CHECK(coloured >= 2);
        CHECK(post::lookFor(at(d + 1.5 * bar)).grade.palette == 0.f);
    }
    // PORTAL's way out and every other chapter: no palette, full bloom.
    CHECK(post::paletteWeight(show::sampleTimeline(192.27)) == 0.f);
    for (int c : {0, 2, 7, 11, 18, 23, 28})
        for (double f : {0.1, 0.5, 0.9}) {
            const show::TimelineFrame frame =
                show::sampleTimeline(show::chapterStart(c) + f * (show::chapterStart(c + 1) - show::chapterStart(c)));
            CHECK(post::paletteWeight(frame) == 0.f && post::portalBloomScale(frame) == 1.f);
        }
}

// (e) The grain's frame: round(showSeconds * 60), the same for the same time, the next for the next frame.
void testGrainFrame() {
    for (double t : {12.5, 250.0, 600.25}) {
        const post::Look a = post::lookFor(show::sampleTimeline(t)), b = post::lookFor(show::sampleTimeline(t));
        const post::Look next = post::lookFor(show::sampleTimeline(t + kFrame));
        CHECK(a.grade.grainFrame == std::uint32_t(std::llround(t * 60)));
        CHECK(a.grade.grainFrame == b.grade.grainFrame && next.grade.grainFrame == a.grade.grainFrame + 1);
    }
}

} // namespace

int main() {
    try {
        testActs();
        testJoins();
        testExposure();
        testCrossings();
        testGrainFrame();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("look_test: %d checks passed\n", checksPassed);
    return 0;
}
