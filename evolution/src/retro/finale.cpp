// FINALE's 1993 side: the megamix's cuts on the chapter's beats and the home frame each sub-effect renders.
// Everything here is a function of the chapter's fixed facts and the retro frame.

#include "retro/finale.hpp"

#include "retro/dots.hpp"
#include "retro/tunnel.hpp"
#include "show/music_curves.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace evo::retro {

namespace {

// Chapter starts lie on tracked beats; converting them to track seconds rounds by far less than this.
constexpr double kChapterEdge = 1e-6;

std::uint32_t framesOf(double seconds, std::uint32_t rate) {
    return static_cast<std::uint32_t>(std::floor(std::max(seconds, 0.0) * rate));
}

// The home chapter's fixed facts (its frame at its start).
show::TimelineFrame homeFacts(show::Effect effect) {
    return show::sampleTimeline(show::chapterStart(finaleHomeChapter(effect)));
}

// The last whole retro frame of a sub-effect's home chapter.
std::uint32_t homeLastFrame(show::Effect effect) {
    const std::uint32_t frames = framesOf(homeFacts(effect).chapterDuration, effectFor(effect).retroRate);
    return frames > 0 ? frames - 1 : 0;
}

} // namespace

int finaleFirstBeat(const show::BeatGridView& grid, double offset) {
    const int beat = grid.beatAt(offset + kChapterEdge);
    if (beat < 0)
        return 0; // before the track's first beat
    return grid.beatTime(beat) < offset - kChapterEdge ? beat + 1 : beat;
}

std::vector<std::uint32_t> finaleCutFrames(const show::TimelineFrame& frame) {
    // An allowed fixed fact (retro/effects.hpp), as for GLENZ: the chapter's start in its track and the
    // track's tracked beats.
    const double start = show::chapterStart(frame.chapter);
    const int track = show::trackAt(start);
    const show::BeatGridView grid = show::beatGrid(track);
    const double offset = start - show::trackStart(track);
    const int first = finaleFirstBeat(grid, offset); // the beat the chapter starts on
    std::vector<std::uint32_t> cuts;
    for (int beat = first + kFinaleBeatsPerCut; beat < grid.count(); beat += kFinaleBeatsPerCut) {
        const double local = grid.beatTime(beat) - offset;
        if (local >= frame.chapterDuration)
            break;
        const std::uint32_t cut = framesOf(local, kFinaleRetroRate);
        if (cut > 0 && (cuts.empty() || cut > cuts.back()))
            cuts.push_back(cut);
    }
    return cuts;
}

int finaleHomeChapter(show::Effect effect) {
    for (std::size_t chapter = 0; chapter < show::cues.size(); ++chapter)
        if (show::cues[chapter].effect == effect)
            return static_cast<int>(chapter);
    throw std::invalid_argument("no chapter shows this effect");
}

show::TimelineFrame finaleHomeFrame(show::Effect effect, std::uint32_t homeRetroFrame) {
    const double start = show::chapterStart(finaleHomeChapter(effect));
    const double duration = homeFacts(effect).chapterDuration;
    const double local = (homeRetroFrame + 0.5) / effectFor(effect).retroRate;
    return show::sampleTimeline(start + std::min(local, duration - kChapterEdge));
}

std::uint32_t finaleHomeStart(show::Effect effect) {
    const show::TimelineFrame home = homeFacts(effect);
    const std::uint32_t rate = effectFor(effect).retroRate;
    switch (effect) {
    case show::Effect::Flyby: // past the ship's right side
        return framesOf(0.6 * home.chapterDuration, rate);
    case show::Effect::Dots: { // the last 0.2 s of the morph into the year, which then holds
        const std::uint32_t arrival = dotsSchedule(home)[2].arrival;
        return arrival > 7 ? arrival - 7 : 0;
    }
    case show::Effect::Tunnel: // the tube, once its front has swept in
        return tunnelTubeFrame(home) + framesOf(kTunnelTubeSweepSeconds, rate) + 1;
    case show::Effect::Glenz:
        return framesOf(home.accentSeconds, rate);
    case show::Effect::Lens: // the lens high at the right end of its run (rotozoomView), over the logo
        return framesOf(kFinaleLensStartSeconds, rate);
    case show::Effect::Raytrace: // mid-orbit
        return framesOf(0.5 * home.chapterDuration, rate);
    default:
        throw std::invalid_argument("not a FINALE sub-effect");
    }
}

EffectFrame finaleResolve(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::vector<std::uint32_t> cuts = finaleCutFrames(frame);
    const auto segment =
        static_cast<std::size_t>(std::upper_bound(cuts.begin(), cuts.end(), retroFrame) - cuts.begin());
    if (segment > 0 && cuts[segment - 1] == retroFrame)
        return {show::Effect::Finale, frame, retroFrame}; // the flash: FINALE's own entry
    const std::uint32_t from = segment > 0 ? cuts[segment - 1] : 0;
    const show::Effect sub = kFinaleCuts[segment % kFinaleCuts.size()];
    // n FINALE frames into the cut are floor(n * rate / 35) frames at the sub-effect's rate.
    const std::uint32_t elapsed = (retroFrame - from) * effectFor(sub).retroRate / kFinaleRetroRate;
    const std::uint32_t home = std::min(finaleHomeStart(sub) + elapsed, homeLastFrame(sub));
    return {sub, finaleHomeFrame(sub, home), home};
}

void finalePalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    palette.fill(rgba(255, 255, 255));
    palette[0] = rgba(0, 0, 0);
}

std::uint8_t finaleBackground(const show::TimelineFrame&, std::uint32_t) {
    return kFinaleWhite;
}

} // namespace evo::retro
