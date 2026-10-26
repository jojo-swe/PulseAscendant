#pragma once

#include "show/cues.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace evo::show {

// Everything a frame needs at one show time. Built by sampleTimeline(); the same time always gives the
// same frame, however the show got there (seeking, export, playback).
struct TimelineFrame {
    // Where we are. localSeconds and accentSeconds are seconds into the chapter; accentSeconds is the
    // strongest hit in the middle of the chapter, where its main event belongs (in Act III the tracked beat
    // the form of its track names: generated::chapterAccents).
    double showSeconds = 0, localSeconds = 0, chapterDuration = 0, accentSeconds = 0;
    int chapter = 0;
    Effect effect = Effect::Boot;
    Transition transition = Transition::Cut;
    float chapterProgress = 0, drive = 0;
    // The track playing (0 = Act I, 1 = Act II, 2 = Act III) and the time into it.
    int track = 0;
    double trackSeconds = 0;

    // Music, all in [0, 1]. kick, snare and treble are the 100 Hz low, mid and high attacks; bass and hit
    // mix them with the 20 Hz envelope as the parent did.
    float energy = 0, bass = 0, hit = 0, kick = 0, snare = 0, treble = 0, lowOnsetPhase = 0;
    std::array<float, 24> spectrum{};

    // The tracked beat grid. beatIndex is the beat at or before now within the track (0 before the first),
    // beatSeconds the length of the current beat, barPhase the position in the bar in [0, 1].
    // onDownbeatBar is true while the current beat is a bar's downbeat.
    int beatIndex = 0;
    float beatPhase = 0, barPhase = 0, beatSeconds = .5f;
    bool onDownbeatBar = false;

    // Camera-safe signals: drive smoothed over about 0.7 s with zero phase, and a flow clock that integrates
    // the smoothed motion speed across the whole show and never runs backwards.
    float cameraDrive = 0;
    double flowSeconds = 0;

    // The 1993/2026 era state (filled by applyEra; defaults here).
    float era = 0, wipe = 0, eraCut = 0;
    int swapCount = 0;

    const char* title = "";
    const char* thenCard = "";
    const char* nowCard = "";
};

// The tracked beats of one track, times in track seconds.
class BeatGridView {
public:
    BeatGridView(const double* times, const unsigned char* bars, int count)
        : times_(times), bars_(bars), count_(count) {}

    // Index of the last beat at or before trackSeconds; -1 before the first beat.
    int beatAt(double trackSeconds) const;
    // Beat times and bar positions (0 = downbeat); indices clamp into the grid.
    double beatTime(int beat) const;
    int bar(int beat) const;
    int count() const { return count_; }
    // All beat times, in order.
    std::span<const double> times() const { return {times_, static_cast<std::size_t>(count_)}; }

private:
    const double* times_;
    const unsigned char* bars_;
    int count_;
};

BeatGridView beatGrid(int track);

// The show's music, relative to the executable's directory: the --music default and the "music" field of
// showInfoJson().
inline constexpr const char* kMusicFile = "assets/music/show.wav";

double showDuration();
int chapterCount();
// Directed start of a chapter in show seconds; the index clamps into the show.
double chapterStart(int chapter);
// Wraps a time into [0, showDuration()); negative or non-finite times go to the start.
double wrapShowTime(double seconds);

TimelineFrame sampleTimeline(double showSeconds);

// The show as JSON for --show-info: duration, music file, tracks and every chapter with its start, duration,
// accent (accent in show seconds) and the eras it shows ([1993], [2026] or [1993,2026]: a Cut holds its
// eraFixed, the other transitions show both). With `sceneName`, every chapter also names the 2026 scene its
// effect renders ("scene", last in the chapter): the show does not know the scenes, so main() passes
// modern::sceneFor's names in.
using SceneNameFn = const char* (*)(Effect effect);
std::string showInfoJson(SceneNameFn sceneName = nullptr);

} // namespace evo::show
