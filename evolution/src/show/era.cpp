#include "show/era.hpp"

#include "show/cues.hpp"
#include "show/music_curves.hpp"

#include <algorithm>
#include <cmath>

namespace evo::show {

namespace {

// Matches BeatGridView::beatAt, so a time exactly on a beat counts as at or after it.
constexpr double onBeat = 1e-9;
// Chapter starts lie on tracked beats; converting them to track seconds can round by far less than this.
constexpr double chapterEdge = 1e-6;

// The landmarks of a chapter's transition, in track seconds.
struct Window {
    double start = 0;      // chapter start
    double accent = 0;     // A
    double downbeat = 0;   // D: the downbeat at or before A
    int downbeatBeat = -1; // D's beat index; -1 when no beat comes before A
    double bar = 2;        // four beats measured at D
};

Window window(const TimelineFrame& f, const BeatGridView& grid) {
    Window w;
    w.start = chapterStart(f.chapter) - trackStart(f.track);
    w.accent = w.start + f.accentSeconds;
    w.downbeat = w.accent;

    // D never lies before the chapter start: the walk back stops at the chapter's first beat, which then
    // stands in for the downbeat. Without a beat between the chapter start and A, the transition happens on
    // A.
    int beat = grid.beatAt(w.accent);
    if (beat < 0 || grid.beatTime(beat) < w.start - chapterEdge)
        return w;
    while (beat > 0 && grid.bar(beat) != 0 && grid.beatTime(beat - 1) >= w.start - chapterEdge)
        --beat;
    w.downbeatBeat = beat;
    w.downbeat = grid.beatTime(beat);

    double beatSeconds = .5;
    if (beat + 1 < grid.count())
        beatSeconds = grid.beatTime(beat + 1) - w.downbeat;
    else if (beat > 0)
        beatSeconds = w.downbeat - grid.beatTime(beat - 1);
    w.bar = 4 * std::max(beatSeconds, 1e-3);
    return w;
}

float smoothstep(double from, double to, double t) {
    const double x = std::clamp((t - from) / (to - from), 0.0, 1.0);
    return float(x * x * (3 - 2 * x));
}

void portal(TimelineFrame& f, const Window& w) {
    const double t = f.trackSeconds;
    f.era = smoothstep(w.accent - 1, w.accent + 1, t);
    f.eraCut = t >= w.accent ? float(std::exp(-8 * (t - w.accent))) : 0.f;
}

void wipe(TimelineFrame& f, const Window& w) {
    const double t = f.trackSeconds;
    // Two bars ending at D, or from the chapter start when the chapter has less than that before D.
    const double from = std::max(w.downbeat - 2 * w.bar, w.start);
    const double span = w.downbeat - from;
    f.wipe = span > 0 ? float(std::clamp((t - from) / span, 0.0, 1.0)) : (t >= w.downbeat ? 1.f : 0.f);
    f.era = f.wipe;
}

// The BeatSwap flips at or before `until`: bar lines from the window start, then every beat up to and
// including D. A beat on the chapter start never flips, so every chapter opens in 1993.
struct Flips {
    int count = 0;
    double last = 0;
};

Flips countFlips(const Window& w, const BeatGridView& grid, double until, int skip) {
    // Beats within half a beat of a window edge belong to it, so tracker jitter cannot drop a flip.
    const double tolerance = w.bar / 8;
    const double fourBarsOut = w.downbeat - 4 * w.bar;
    const bool fullWindow = fourBarsOut >= w.start;
    const double from = fullWindow ? fourBarsOut : w.start;
    const double beatFlipsFrom = fullWindow ? w.downbeat - 2 * w.bar : w.start;

    Flips flips;
    for (int beat = grid.beatAt(std::max(from - tolerance, w.start + chapterEdge)) + 1;
         beat <= w.downbeatBeat; ++beat) {
        const double time = grid.beatTime(beat);
        if (time > until + onBeat)
            break;
        if (time < beatFlipsFrom - tolerance && grid.bar(beat) != 0)
            continue; // between bar lines, before the beat flips start
        if (skip > 0) {
            --skip;
            continue;
        }
        ++flips.count;
        flips.last = time;
    }
    return flips;
}

void beatSwap(TimelineFrame& f, const Window& w, const BeatGridView& grid) {
    const double t = f.trackSeconds;
    if (w.downbeatBeat < 0) { // no grid to swap on: a hard cut at A
        f.era = t >= w.accent ? 1.f : 0.f;
        return;
    }

    // The era starts at 0 and the flip on D must land on 1, so the schedule needs an odd number of flips:
    // when it has an even number, the earliest one is dropped.
    const int skip = countFlips(w, grid, w.downbeat, 0).count % 2 == 0 ? 1 : 0;
    const Flips flips = countFlips(w, grid, t, skip);

    f.swapCount = flips.count;
    f.era = t >= w.downbeat - onBeat ? 1.f : float(flips.count % 2);
    f.eraCut = flips.count > 0 ? float(std::min(1.0, std::exp(-10 * (t - flips.last)))) : 0.f;
}

} // namespace

TransitionMarks transitionMarks(const TimelineFrame& frame) {
    const Window w = window(frame, beatGrid(frame.track));
    const double offset = trackStart(frame.track);
    return {w.start + offset, w.accent + offset, w.downbeat + offset, w.bar};
}

void applyEra(TimelineFrame& f, const BeatGridView& grid) {
    f.era = 0;
    f.wipe = 0;
    f.eraCut = 0;
    f.swapCount = 0;

    switch (f.transition) {
    case Transition::Cut:
        f.era = cues[std::clamp(f.chapter, 0, int(cues.size()) - 1)].eraFixed;
        break;
    case Transition::Portal:
        portal(f, window(f, grid));
        break;
    case Transition::Wipe:
        wipe(f, window(f, grid));
        break;
    case Transition::BeatSwap:
        beatSwap(f, window(f, grid), grid);
        break;
    }
}

} // namespace evo::show
