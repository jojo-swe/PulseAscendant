#include "show/cues.hpp"
#include "show/era.hpp"
#include "show/music_curves.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

using evo::show::Effect;
using evo::show::sampleTimeline;
using evo::show::TimelineFrame;
using evo::show::Transition;

// Bit equality, so -0 against 0 or two different NaNs would count as a difference.
template <class T> bool same(const T& a, const T& b) {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

bool sameFrame(const TimelineFrame& a, const TimelineFrame& b) {
    return same(a.showSeconds, b.showSeconds) && same(a.localSeconds, b.localSeconds) &&
           same(a.chapterDuration, b.chapterDuration) && same(a.accentSeconds, b.accentSeconds) &&
           a.chapter == b.chapter && a.effect == b.effect && a.transition == b.transition &&
           same(a.chapterProgress, b.chapterProgress) && same(a.drive, b.drive) && a.track == b.track &&
           same(a.trackSeconds, b.trackSeconds) && same(a.energy, b.energy) && same(a.bass, b.bass) &&
           same(a.hit, b.hit) && same(a.kick, b.kick) && same(a.snare, b.snare) && same(a.treble, b.treble) &&
           same(a.lowOnsetPhase, b.lowOnsetPhase) && same(a.spectrum, b.spectrum) &&
           a.beatIndex == b.beatIndex && same(a.beatPhase, b.beatPhase) && same(a.barPhase, b.barPhase) &&
           same(a.beatSeconds, b.beatSeconds) && a.onDownbeatBar == b.onDownbeatBar &&
           same(a.cameraDrive, b.cameraDrive) && same(a.flowSeconds, b.flowSeconds) && same(a.era, b.era) &&
           same(a.wipe, b.wipe) && same(a.eraCut, b.eraCut) && a.swapCount == b.swapCount &&
           a.title == b.title && a.thenCard == b.thenCard && a.nowCard == b.nowCard;
}

// The schedule landmarks of a chapter, worked out here independently of era.cpp. Times in track seconds.
struct Marks {
    int track = 0;
    double offset = 0; // the track start in show seconds
    double start = 0, end = 0, accent = 0;
    int downbeatBeat = 0;
    double downbeat = 0, bar = 0;
};

Marks marks(int chapter) {
    const TimelineFrame frame = sampleTimeline(evo::show::chapterStart(chapter) + .01);
    Marks m;
    m.track = frame.track;
    m.offset = evo::show::trackStart(m.track);
    m.start = evo::show::chapterStart(chapter) - m.offset;
    m.end = m.start + frame.chapterDuration;
    m.accent = m.start + frame.accentSeconds;
    const evo::show::BeatGridView grid = evo::show::beatGrid(m.track);
    int beat = grid.beatAt(m.accent);
    while (beat > 0 && grid.bar(beat) != 0)
        --beat;
    m.downbeatBeat = beat;
    m.downbeat = grid.beatTime(beat);
    m.bar = 4 * (grid.beatTime(beat + 1) - grid.beatTime(beat));
    return m;
}

TimelineFrame at(const Marks& m, double trackSeconds) {
    return sampleTimeline(m.offset + trackSeconds);
}

// (a) Review Focus 1: a time gives the same frame however the show got there.
void checkDeterminism() {
    std::uint64_t state = 0x2026199320261993ull; // fixed seed: the test is reproducible
    auto next = [&] {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return double(state >> 11) / double(1ull << 53) * evo::show::showDuration();
    };
    std::vector<double> times(200);
    for (double& t : times)
        t = next();

    // First order: a straight play-through, the times in ascending order.
    std::vector<double> ascending = times;
    std::sort(ascending.begin(), ascending.end());
    std::vector<TimelineFrame> played;
    for (double t : ascending)
        played.push_back(sampleTimeline(t));

    // Second order: every time reached by a seek from an unrelated time, walking the list backwards.
    for (int i = int(ascending.size()) - 1; i >= 0; --i) {
        sampleTimeline(next());
        sampleTimeline(ascending[(i * 37 + 11) % ascending.size()]);
        CHECK(sameFrame(sampleTimeline(ascending[i]), played[i]));
    }
}

// (b) Portal: a smooth crossing centred on the accent, with a cut flash at A.
void checkPortal() {
    CHECK(evo::show::cues[1].transition == Transition::Portal);  // Flyby
    CHECK(evo::show::cues[20].transition == Transition::Portal); // Reboot, era 0 to 1 at the start of Act III
    for (int chapter = 0; chapter < evo::show::chapterCount(); ++chapter) {
        if (evo::show::cues[chapter].transition != Transition::Portal)
            continue;
        const Marks m = marks(chapter);
        CHECK(m.accent - 2 > m.start && m.accent + 2 < m.end);
        CHECK(at(m, m.start + .01).era == 0.f);
        CHECK(at(m, m.accent - 2).era == 0.f);
        CHECK(std::abs(at(m, m.accent).era - .5f) < 1e-3f);
        CHECK(at(m, m.accent + 2).era == 1.f);
        CHECK(at(m, m.end - .01).era == 1.f);
        CHECK(at(m, m.accent - .01).eraCut == 0.f);
        CHECK(std::abs(at(m, m.accent + .01).eraCut - std::exp(-.08f)) < 1e-3f);
        CHECK(at(m, m.accent + 1).eraCut < .001f);
        for (double t = m.start; t < m.end; t += .01) {
            const TimelineFrame frame = at(m, t);
            CHECK(frame.wipe == 0.f && frame.swapCount == 0);
        }
    }
    // Act I ends in 2026, where Act II picks up.
    CHECK(sampleTimeline(evo::show::chapterStart(11) - .01).era == 1.f);
}

// (c) Wipe: the two bars ending at the downbeat before the accent.
void checkWipe() {
    CHECK(evo::show::cues[3].transition == Transition::Wipe); // Plasma
    for (int chapter = 0; chapter < evo::show::chapterCount(); ++chapter) {
        if (evo::show::cues[chapter].transition != Transition::Wipe)
            continue;
        const Marks m = marks(chapter);
        const double from = m.downbeat - 2 * m.bar;
        CHECK(from > m.start); // the full window fits: the short-chapter rule is checked separately
        CHECK(at(m, m.start + .01).wipe == 0.f);
        CHECK(at(m, from - .05).wipe == 0.f);
        CHECK(std::abs(at(m, m.downbeat - m.bar).wipe - .5f) < 1e-3f);
        CHECK(at(m, m.downbeat).wipe == 1.f);
        CHECK(at(m, m.end - .01).wipe == 1.f);
        float previous = 0;
        for (double t = from + .005; t < m.downbeat; t += .01) {
            const TimelineFrame frame = at(m, t);
            CHECK(frame.wipe > previous);
            CHECK(frame.era == frame.wipe);
            CHECK(frame.eraCut == 0.f && frame.swapCount == 0);
            previous = frame.wipe;
        }
    }
}

// (d) BeatSwap: bar flips, then beat flips, then 2026 from the downbeat on.
void checkBeatSwap() {
    CHECK(evo::show::cues[7].transition == Transition::BeatSwap); // Raytrace
    CHECK(marks(7).downbeat - 4 * marks(7).bar > marks(7).start); // has the full four-bar window
    int shortChapters = 0;
    for (int chapter = 0; chapter < evo::show::chapterCount(); ++chapter) {
        if (evo::show::cues[chapter].transition != Transition::BeatSwap)
            continue;
        const Marks m = marks(chapter);
        const evo::show::BeatGridView grid = evo::show::beatGrid(m.track);
        CHECK(at(m, m.start + .01).era == 0.f);
        CHECK(at(m, m.start + .01).swapCount == 0);

        // The flip on D is a visible cut from 1993 to 2026.
        const TimelineFrame onDownbeat = at(m, m.downbeat);
        CHECK(at(m, m.downbeat - .01).era == 0.f);
        CHECK(onDownbeat.era == 1.f);
        CHECK(onDownbeat.eraCut == 1.f);
        CHECK(onDownbeat.swapCount % 2 == 1);
        CHECK(onDownbeat.swapCount >= (chapter == 7 ? 10 : 9));
        CHECK(at(m, m.accent + .01).era == 1.f);
        CHECK(at(m, m.end - .01).era == 1.f);
        CHECK(at(m, m.end - .01).swapCount == onDownbeat.swapCount);

        // Every beat of the last two bars flips the era.
        for (int beat = m.downbeatBeat - 8; beat < m.downbeatBeat - 1; ++beat) {
            const double mid = (grid.beatTime(beat) + grid.beatTime(beat + 1)) * .5;
            const double nextMid = (grid.beatTime(beat + 1) + grid.beatTime(beat + 2)) * .5;
            const TimelineFrame a = at(m, mid), b = at(m, nextMid);
            CHECK(a.era != b.era);
            CHECK(b.swapCount == a.swapCount + 1);
            CHECK(at(m, grid.beatTime(beat + 1)).eraCut == 1.f);
            CHECK(a.eraCut > 0.f && a.eraCut < 1.f);
        }

        if (m.downbeat - 4 * m.bar > m.start) {
            // Nothing flips before the window; before the beat flips only bar lines flip it.
            for (double t = m.start; t < m.downbeat - 4 * m.bar - .25; t += .01)
                CHECK(at(m, t).swapCount == 0 && at(m, t).era == 0.f);
            // The last two bars hold nine flips (eight beats and D).
            const TimelineFrame twoBarsOut = at(m, grid.beatTime(m.downbeatBeat - 8) - .01);
            CHECK(at(m, grid.beatTime(m.downbeatBeat - 9) + .01).swapCount == twoBarsOut.swapCount);
            CHECK(twoBarsOut.swapCount == onDownbeat.swapCount - 9);
        } else {
            // Review Focus 5 on the real show (Finale): every beat after the chapter start flips it, less
            // the first when that keeps the count odd.
            ++shortChapters;
            const int first = grid.beatAt(m.start + 1e-6) + 1;
            const int skip = (m.downbeatBeat - first + 1) % 2 == 0 ? 1 : 0;
            for (int beat = first; beat <= m.downbeatBeat; ++beat)
                CHECK(at(m, grid.beatTime(beat) + .01).swapCount == std::max(0, beat - first + 1 - skip));
            CHECK(m.downbeatBeat - first + 1 - skip == onDownbeat.swapCount);
        }
    }
    CHECK(shortChapters == 1); // Finale, whose accent comes less than four bars in
}

// Review Focus 5: a chapter whose accent comes too early for the full window starts it at the chapter start.
void checkShortChapters() {
    // BeatSwap on Raytrace with an accent 3 s in: fewer than 4 bars, so it flips on every beat from the
    // start.
    {
        TimelineFrame frame = sampleTimeline(evo::show::chapterStart(7) + .01);
        Marks m = marks(7);
        frame.accentSeconds = 3.0;
        m.accent = m.start + 3.0;
        const evo::show::BeatGridView grid = evo::show::beatGrid(m.track);
        int downbeat = grid.beatAt(m.accent);
        while (downbeat > 0 && grid.bar(downbeat) != 0)
            --downbeat;
        CHECK(grid.beatTime(downbeat) > m.start);
        const int first = grid.beatAt(m.start + 1e-6) + 1;        // the first beat after the chapter start
        const int skip = (downbeat - first + 1) % 2 == 0 ? 1 : 0; // keeps the flip on D landing on 2026
        for (int beat = first; beat <= downbeat; ++beat) {
            frame.trackSeconds = grid.beatTime(beat) + .01;
            evo::show::applyEra(frame, grid);
            const int expected = std::max(0, beat - first + 1 - skip);
            CHECK(frame.swapCount == expected);
            CHECK(frame.era == (beat == downbeat ? 1.f : float(expected % 2)));
        }
        frame.trackSeconds = grid.beatTime(downbeat) - .01;
        evo::show::applyEra(frame, grid);
        CHECK(frame.era == 0.f);
        frame.trackSeconds = grid.beatTime(first) - .01;
        evo::show::applyEra(frame, grid);
        CHECK(frame.swapCount == 0 && frame.era == 0.f);
    }
    // Wipe on Plasma with an accent 3 s in: fewer than 2 bars, so the wipe runs from the chapter start.
    {
        TimelineFrame frame = sampleTimeline(evo::show::chapterStart(3) + .01);
        const Marks m = marks(3);
        frame.accentSeconds = 3.0;
        const evo::show::BeatGridView grid = evo::show::beatGrid(m.track);
        int downbeat = grid.beatAt(m.start + 3.0);
        while (downbeat > 0 && grid.bar(downbeat) != 0)
            --downbeat;
        const double d = grid.beatTime(downbeat);
        CHECK(d > m.start);
        frame.trackSeconds = m.start;
        evo::show::applyEra(frame, grid);
        CHECK(frame.wipe == 0.f);
        frame.trackSeconds = (m.start + d) * .5;
        evo::show::applyEra(frame, grid);
        CHECK(std::abs(frame.wipe - .5f) < 1e-3f);
        frame.trackSeconds = d;
        evo::show::applyEra(frame, grid);
        CHECK(frame.wipe == 1.f && frame.era == 1.f);
    }
}

// Grids the tracker never produces still give finite values in range: no beats at all, and beats far closer
// than any tempo, through all four transitions and accents at, inside and past the chapter.
void checkDegenerateGrids() {
    const double start = evo::show::chapterStart(7); // Raytrace, on track 0: track seconds are show seconds
    std::vector<double> times;
    std::vector<unsigned char> bars;
    for (int i = 0; i < 80000; ++i) {
        times.push_back(start - 5 + i * .0005);
        bars.push_back(static_cast<unsigned char>(i % 4));
    }
    const evo::show::BeatGridView grids[] = {
        evo::show::BeatGridView(nullptr, nullptr, 0),
        evo::show::BeatGridView(times.data(), bars.data(), int(times.size())),
    };
    const Transition transitions[] = {Transition::Cut, Transition::Portal, Transition::Wipe,
                                      Transition::BeatSwap};
    for (const evo::show::BeatGridView& grid : grids)
        for (Transition transition : transitions)
            for (double accent : {0.0, .0003, 3.0, 20.0, 60.0}) {
                TimelineFrame frame = sampleTimeline(start + .01);
                frame.transition = transition;
                frame.accentSeconds = accent;
                for (double t = start - 1; t < start + 40; t += .0007) {
                    frame.trackSeconds = t;
                    evo::show::applyEra(frame, grid);
                    CHECK(std::isfinite(frame.era) && frame.era >= 0.f && frame.era <= 1.f);
                    CHECK(std::isfinite(frame.wipe) && frame.wipe >= 0.f && frame.wipe <= 1.f);
                    CHECK(std::isfinite(frame.eraCut) && frame.eraCut >= 0.f && frame.eraCut <= 1.f);
                    CHECK(frame.swapCount >= 0);
                }
            }
}

// (e) Cut: the chapter holds its fixed era.
void checkCut() {
    const struct {
        int chapter;
        Effect effect;
        float era;
    } expected[] = {{0, Effect::Boot, 0},
                    {11, Effect::StudioNight, 1},
                    {29, Effect::Credits, 1},
                    {30, Effect::TheEnd, 0}};
    for (const auto& e : expected) {
        CHECK(evo::show::cues[e.chapter].effect == e.effect);
        CHECK(evo::show::cues[e.chapter].transition == Transition::Cut);
        CHECK(evo::show::cues[e.chapter].eraFixed == e.era);
    }
    for (int chapter = 0; chapter < evo::show::chapterCount(); ++chapter) {
        const evo::show::Cue& cue = evo::show::cues[chapter];
        if (cue.transition != Transition::Cut)
            continue;
        const Marks m = marks(chapter);
        for (double t = m.start; t < m.end; t += .05) {
            const TimelineFrame frame = at(m, t);
            CHECK(frame.chapter == chapter);
            CHECK(frame.era == cue.eraFixed);
            CHECK(frame.wipe == 0.f && frame.eraCut == 0.f && frame.swapCount == 0);
        }
    }
}

// (f) Finite, in range, everywhere.
void checkFinite() {
    for (int step = 0; step * .005 < evo::show::showDuration(); ++step) {
        const TimelineFrame frame = sampleTimeline(step * .005);
        CHECK(std::isfinite(frame.era) && frame.era >= 0.f && frame.era <= 1.f);
        CHECK(std::isfinite(frame.wipe) && frame.wipe >= 0.f && frame.wipe <= 1.f);
        CHECK(std::isfinite(frame.eraCut) && frame.eraCut >= 0.f && frame.eraCut <= 1.f);
        CHECK(frame.swapCount >= 0);
    }
}

} // namespace

int main() {
    try {
        checkDeterminism(); // first, so the lazily built tables are built inside it
        checkPortal();
        checkWipe();
        checkBeatSwap();
        checkShortChapters();
        checkDegenerateGrids();
        checkCut();
        checkFinite();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED after %d checks: %s\n", checksPassed, error.what());
        return 1;
    }
    std::printf("era: %d checks passed\n", checksPassed);
    return 0;
}
