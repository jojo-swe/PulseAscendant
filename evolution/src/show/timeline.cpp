#include "show/timeline.hpp"

#include "show/generated/beat_data.hpp"
#include "show/generated/cue_data.hpp"
#include "show/music_curves.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

namespace evo::show {

static_assert(cues.size() == generated::chapterCount, "cues.hpp and cue_data.hpp disagree on the chapters");

constexpr bool targetsMatchDirection() {
    for (std::size_t i = 0; i < cues.size(); ++i)
        if (cues[i].target != generated::chapterTargets[i])
            return false;
    return true;
}
static_assert(targetsMatchDirection(),
              "cues.hpp targets differ from tools/show_targets.json; edit both or regenerate cue_data.hpp");
static_assert(generated::chapterStarts[0] == 0.0, "the show starts with chapter 0");

namespace {

// Per-chapter facts that depend only on the data, computed once so any time samples them identically.
struct Direction {
    int track = 0;
    double start = 0, duration = 0;
    double accent = 0;    // seconds into the chapter
    double flowStart = 0; // flowSeconds at the chapter start
};

// The parent's accent rule: the strongest hit between 28 % and 76 % of the chapter, with a bonus for a
// rising bass onset and a slight pull towards 52 %. Searched on the 20 Hz envelope grid.
double findAccent(int track, double offset, double duration) {
    double accent = duration * .5;
    float best = -1;
    const double rate = 20.0;
    for (int step = int(std::ceil(duration * .28 * rate)); step <= duration * .76 * rate; ++step) {
        const double at = step / rate;
        const float bass = envelope(track, offset + at, EnvelopeChannel::Bass);
        const float bassBefore = envelope(track, offset + at - .1, EnvelopeChannel::Bass);
        const float onset = std::max(0.f, bass - bassBefore);
        const float score = envelope(track, offset + at, EnvelopeChannel::Hit) + onset * .5f +
                            .02f * float(1 - std::abs(at / duration - .52));
        if (score > best) {
            best = score;
            accent = at;
        }
    }
    return accent;
}

// How fast a chapter's flow clock runs before the music adds to it.
double baseFlowRate(const Cue& cue) {
    return 1.3 + cue.drive * 1.65;
}

const std::vector<Direction>& directions() {
    static const std::vector<Direction> result = [] {
        std::vector<Direction> all(cues.size());
        double flow = 0;
        for (int chapter = 0; chapter < chapterCount(); ++chapter) {
            Direction& d = all[chapter];
            d.start = chapterStart(chapter);
            const double end = chapter + 1 < chapterCount() ? chapterStart(chapter + 1) : showDuration();
            d.duration = end - d.start;
            d.track = trackAt(d.start);
            const double offset = d.start - trackStart(d.track);
            d.accent = findAccent(d.track, offset, d.duration);
            d.flowStart = flow;
            flow += d.duration * baseFlowRate(cues[chapter]) + smoothFlow(d.track, offset + d.duration) -
                    smoothFlow(d.track, offset);
        }
        return all;
    }();
    return result;
}

int chapterAt(double showSeconds) {
    const double* starts = generated::chapterStarts;
    const double* found = std::upper_bound(starts, starts + generated::chapterCount, showSeconds + 1e-9);
    return std::clamp(int(found - starts) - 1, 0, generated::chapterCount - 1);
}

void sampleBeats(TimelineFrame& f) {
    const BeatGridView grid = beatGrid(f.track);
    const int i = grid.beatAt(f.trackSeconds);
    const double previous = i >= 0 ? grid.beatTime(i) : 0.0;
    double next;
    if (i + 1 < grid.count())
        next = grid.beatTime(i + 1);
    else // past the last beat: keep the last beat length
        next = previous + (i > 0 ? grid.beatTime(i) - grid.beatTime(i - 1) : .5);
    const double length = std::max(next - previous, 1e-6);

    f.beatIndex = std::max(i, 0);
    f.beatSeconds = float(std::max(.2, next - previous));
    f.beatPhase = i >= 0 ? float(std::clamp((f.trackSeconds - previous) / length, 0.0, 1.0)) : 0.f;
    f.barPhase = i >= 0 ? (float(grid.bar(i)) + f.beatPhase) * .25f : 0.f;
    f.onDownbeatBar = i >= 0 && grid.bar(i) == 0;
}

void sampleMusic(TimelineFrame& f, const Cue& cue) {
    const int track = f.track;
    const double t = f.trackSeconds;
    f.kick = reactivity(track, t, ReactivityChannel::Low);
    f.snare = reactivity(track, t, ReactivityChannel::Mid);
    f.treble = reactivity(track, t, ReactivityChannel::High);
    f.lowOnsetPhase = reactivity(track, t, ReactivityChannel::LowOnsetPhase);
    f.energy = envelope(track, t, EnvelopeChannel::Energy);
    f.bass = .28f * envelope(track, t, EnvelopeChannel::Bass) + .72f * f.kick;
    f.hit =
        std::max({envelope(track, t, EnvelopeChannel::Hit) * .55f, f.snare, f.kick * .7f, f.treble * .5f});
    f.spectrum = spectrum(track, t);
    f.drive = std::clamp(cue.drive * .65f + .2f * f.energy + .2f * f.kick + .1f * f.snare, 0.f, 1.f);
    f.cameraDrive = std::clamp(cue.drive * .65f + smoothDrive(track, t), 0.f, 1.f);
}

} // namespace

int BeatGridView::beatAt(double trackSeconds) const {
    return int(std::upper_bound(times_, times_ + count_, trackSeconds + 1e-9) - times_) - 1;
}

double BeatGridView::beatTime(int beat) const {
    return count_ > 0 ? times_[std::clamp(beat, 0, count_ - 1)] : 0.0;
}

int BeatGridView::bar(int beat) const {
    return count_ > 0 ? bars_[std::clamp(beat, 0, count_ - 1)] : 0;
}

BeatGridView beatGrid(int track) {
    track = std::clamp(track, 0, trackCount() - 1);
    const int first = generated::beatOffsets[track];
    const int count = generated::beatOffsets[track + 1] - first;
    return BeatGridView(generated::beatTimes + first, generated::beatBar + first, count);
}

double showDuration() {
    // From whole frames, so 34 135 680 frames is exactly the decimal 711.16 (adding the track lengths in
    // seconds would round at every step).
    std::uint64_t frames = 0;
    for (int track = 0; track < trackCount(); ++track)
        frames += trackFrames(track);
    return double(frames) / 48000.0;
}

int chapterCount() {
    return generated::chapterCount;
}

double chapterStart(int chapter) {
    return generated::chapterStarts[std::clamp(chapter, 0, generated::chapterCount - 1)];
}

double wrapShowTime(double seconds) {
    return std::isfinite(seconds) && seconds > 0 ? std::fmod(seconds, showDuration()) : 0.0;
}

TimelineFrame sampleTimeline(double showSeconds) {
    const double t = wrapShowTime(showSeconds);
    const int chapter = chapterAt(t);
    const Cue& cue = cues[chapter];
    const Direction& d = directions()[chapter];

    TimelineFrame f;
    f.showSeconds = t;
    f.chapter = chapter;
    f.effect = cue.effect;
    f.transition = cue.transition;
    f.title = cue.title;
    f.thenCard = cue.thenCard;
    f.nowCard = cue.nowCard;

    // The chapter's track, so the music and the beat grid never switch tracks inside a chapter.
    f.track = d.track;
    const double trackOffset = trackStart(d.track);
    f.trackSeconds = t - trackOffset;

    f.localSeconds = std::max(0.0, t - d.start);
    f.chapterDuration = d.duration;
    f.accentSeconds = d.accent;
    f.chapterProgress = float(std::clamp(f.localSeconds / d.duration, 0.0, 1.0));

    sampleMusic(f, cue);
    sampleBeats(f);

    const double chapterTrackSeconds = d.start - trackOffset;
    f.flowSeconds = d.flowStart + f.localSeconds * baseFlowRate(cue) + smoothFlow(d.track, f.trackSeconds) -
                    smoothFlow(d.track, chapterTrackSeconds);
    return f;
}

std::string showInfoJson() {
    std::ostringstream out;
    out << std::setprecision(12);
    out << "{\"title\":\"EVOLUTION\",\"version\":\"" << EVO_VERSION << "\",\"duration\":" << showDuration()
        << ",\"music\":\"music/show.wav\",\"tracks\":[";
    for (int track = 0; track < trackCount(); ++track) {
        if (track)
            out << ',';
        out << "{\"frames\":" << trackFrames(track) << ",\"start\":" << trackStart(track) << '}';
    }
    out << "],\"chapters\":[";
    for (int chapter = 0; chapter < chapterCount(); ++chapter) {
        const Cue& cue = cues[chapter];
        const Direction& d = directions()[chapter];
        if (chapter)
            out << ',';
        out << "{\"index\":" << chapter << ",\"title\":\"" << cue.title << "\",\"effect\":\""
            << effectName(cue.effect) << "\",\"transition\":\"" << transitionName(cue.transition)
            << "\",\"track\":" << d.track << ",\"start\":" << d.start << ",\"duration\":" << d.duration
            << ",\"accent\":" << d.start + d.accent << '}';
    }
    out << "]}";
    return out.str();
}

} // namespace evo::show
