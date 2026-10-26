#include "show/director.hpp"

#include "show/era.hpp"
#include "show/music_curves.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace evo::show {

namespace {

// Matches BeatGridView::beatAt: a time exactly on a boundary belongs to what starts there.
constexpr double kOnBoundary = 1e-9;

float ramp(double from, double to, double t) {
    if (to <= from)
        return t >= to ? 1.f : 0.f;
    return float(std::clamp((t - from) / (to - from), 0.0, 1.0));
}

constexpr std::size_t kRows = kMegamixShots.size();
constexpr int kFinaleChapter = static_cast<int>(Effect::Finale);
constexpr int kGalaxyChapter = static_cast<int>(Effect::Galaxy);

// The row starts and the end of the last row, in show seconds, from FINALE's beat grid (computed once).
const std::array<double, kRows + 1>& megamixTimes() {
    static const std::array<double, kRows + 1> times = [] {
        std::array<double, kRows + 1> out{};
        const double start = chapterStart(kFinaleChapter);
        const int track = trackAt(start);
        const double offset = trackStart(track);
        const BeatGridView grid = beatGrid(track);
        // The chapter starts on a tracked beat; converting it to track seconds rounds by far less than 1 us.
        const int first = std::max(grid.beatAt(start - offset + 1e-6), 0);
        for (std::size_t row = 0; row <= kRows; ++row) {
            const int quarter = row < kRows ? kMegamixShots[row].quarter : kMegamixQuarters;
            const int beat = first + quarter / 4;
            const double from = grid.beatTime(beat), to = grid.beatTime(beat + 1);
            out[row] = offset + from + 0.25 * (quarter % 4) * (to - from);
        }
        return out;
    }();
    return times;
}

std::optional<SceneCut> megamixCut(double t) {
    const std::array<double, kRows + 1>& times = megamixTimes();
    if (t + kOnBoundary < times.front() || t + kOnBoundary >= times.back())
        return std::nullopt;
    // The last row starting at or before t.
    const auto next = std::upper_bound(times.begin(), times.end(), t + kOnBoundary);
    const auto row = static_cast<std::size_t>(next - times.begin()) - 1;
    const MegamixShot& shot = kMegamixShots[row];
    if (shot.source == Effect::Finale)
        return std::nullopt; // the room
    const double since = std::max(t - times[row], 0.0);
    const double source = megamixSource(static_cast<int>(row));
    return SceneCut{static_cast<int>(shot.source), shot.frozen ? source : source + since, shot.frozen};
}

} // namespace

PortalPhase portalPhase(const TimelineFrame& frame) {
    if (frame.transition != Transition::Portal)
        return {0.f, 0.f, 0.f, 0.0};

    const TransitionMarks marks = transitionMarks(frame);
    const double d = marks.downbeat;
    const double reach = kPortalBars * marks.bar;
    const double t = frame.showSeconds;

    PortalPhase phase;
    phase.approach = ramp(d - reach, d - kPortalGlassHalfWidth, t);
    phase.glass = float(std::clamp(1.0 - std::abs(t - d) / kPortalGlassHalfWidth, 0.0, 1.0));
    phase.inside = ramp(d + kPortalGlassHalfWidth, d + reach, t);
    phase.sinceGlass = t - d;
    return phase;
}

double megamixStart(int row) {
    return megamixTimes()[static_cast<std::size_t>(std::clamp(row, 0, static_cast<int>(kRows)))];
}

double megamixSource(int row) {
    const auto index = static_cast<std::size_t>(std::clamp(row, 0, static_cast<int>(kRows) - 1));
    const MegamixShot& shot = kMegamixShots[index];
    return shot.from == MegamixFrom::ShipPass ? wormholePass() + shot.sourceSeconds : shot.sourceSeconds;
}

double wormholePass() {
    static const double pass = [] {
        // modern::shipTimes' rule on the chapter's own frame: its downbeats in chapter seconds, D the last
        // one at or before the accent, the pass the first one from 21 % of the chapter on, at least 2 s
        // before D.
        const TimelineFrame head = sampleTimeline(chapterStart(static_cast<int>(Effect::Wormhole)));
        const BeatGridView grid = beatGrid(head.track);
        const double begin = head.trackSeconds - head.localSeconds;
        const double duration = std::max(head.chapterDuration, 1e-3);
        const double accent = std::clamp(head.accentSeconds, 0.0, duration);
        std::vector<double> bars;
        for (int b = std::max(grid.beatAt(begin), 0); b < grid.count(); ++b) {
            const double at = grid.beatTime(b) - begin;
            if (at > duration)
                break;
            if (at >= -1e-9 && grid.bar(b) == 0)
                bars.push_back(at);
        }
        double drop = accent;
        for (const double b : bars)
            if (b <= accent + 1e-6)
                drop = b;
        double from = 0.21 * duration;
        for (const double b : bars)
            if (b >= from) {
                from = b;
                break;
            }
        return head.showSeconds - head.localSeconds + std::min(from, drop - 2.0);
    }();
    return pass;
}

std::optional<SceneCut> sceneCut(const TimelineFrame& frame) {
    switch (frame.effect) {
    case Effect::Finale:
        return megamixCut(frame.showSeconds);
    case Effect::Reboot:
        if (frame.showSeconds + kOnBoundary >= transitionMarks(frame).downbeat)
            return SceneCut{kGalaxyChapter, frame.showSeconds, false};
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

TimelineFrame sceneFrame(const TimelineFrame& frame, const SceneCut& cut) {
    TimelineFrame scene = sampleTimeline(cut.showSeconds);
    if (scene.chapter != cut.chapter) {
        // The chapter's fixed facts from its own first frame; the time, music and flow clock stay the
        // sampled time's, so the scene runs on continuously into the chapter's start.
        const TimelineFrame own = sampleTimeline(chapterStart(cut.chapter));
        scene.chapter = own.chapter;
        scene.effect = own.effect;
        scene.transition = own.transition;
        scene.title = own.title;
        scene.thenCard = own.thenCard;
        scene.nowCard = own.nowCard;
        scene.chapterDuration = own.chapterDuration;
        scene.accentSeconds = own.accentSeconds;
        scene.localSeconds = scene.showSeconds - chapterStart(cut.chapter);
        scene.chapterProgress =
            float(std::clamp(scene.localSeconds / std::max(scene.chapterDuration, 1e-9), 0.0, 1.0));
    }
    scene.era = frame.era;
    scene.wipe = frame.wipe;
    scene.eraCut = frame.eraCut;
    scene.swapCount = frame.swapCount;
    return scene;
}

TimelineFrame sceneFrame(const TimelineFrame& frame) {
    const std::optional<SceneCut> cut = sceneCut(frame);
    return cut ? sceneFrame(frame, *cut) : frame;
}

bool thankYouCard(const TimelineFrame& frame) {
    if (frame.effect != Effect::Finale)
        return false;
    const double start = megamixStart(static_cast<int>(kThankYouRow));
    const double end = start + kThankYouFrames / kThankYouFrameRate;
    const double t = frame.showSeconds + kOnBoundary;
    return t >= start && t < end;
}

void applyDirection(TimelineFrame& frame) {
    if (frame.effect == Effect::Finale)
        frame.era = megamixCut(frame.showSeconds) ? 1.f : 0.f;
}

bool holdsModern(const TimelineFrame& frame) {
    return frame.effect == Effect::Finale && !thankYouCard(frame);
}

float eraFlash(const TimelineFrame& frame) {
    if (frame.effect == Effect::Finale || frame.transition == Transition::Portal)
        return 0.f;
    return kEraFlash * std::clamp(frame.eraCut, 0.f, 1.f);
}

} // namespace evo::show
