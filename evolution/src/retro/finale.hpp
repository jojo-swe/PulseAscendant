#pragma once

#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "show/cues.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace evo::retro {

// FINALE (chapter 9): the 1993 megamix. One effect (so the stage's cache key, (chapter, effect, retroFrame),
// stays right) whose resolver (EffectInfo::resolve) cuts between FLYBY, DOTS, TUNNEL, GLENZ, LENS and
// RAYTRACE in that order, round and round, every kFinaleBeatsPerCut tracked beats of the chapter counted from
// its first beat (the chapter starts on one), with one white frame on each cut: the retro frame the cut's
// beat falls in. The beats are the chapter's fixed facts (show::beatGrid), as GLENZ's bounce.
//
// Each sub-effect renders exactly as in its own (home) chapter: it gets a frame of its home chapter (its
// chapter, duration, accent and beats) and a home retro frame at its own rate, starting at a moment that
// shows it at its best (finaleHomeStart) and advancing with the finale: n FINALE frames into a cut (35 a
// second) are floor(n * rate / 35) home frames on, so RAYTRACE keeps its 10 frames a second. The text is
// FINALE's own (none; the epoch card is the HUD's), so FLYBY's greeting does not show.
//
// Every two beats rather than the plan's two bars: the chapter is 12.2 s, about 7 bars, so two bars would
// show at most five of the six; and with the current beat data BeatSwap shows 1993 only on every other beat
// before its downbeat D and 2026 from D on, so a cut every two beats puts each of the six on its own 1993
// beat, the flash where the beat swaps back to 1993 (era_test's checkBeatSwap pins that pattern for FINALE,
// the one BeatSwap chapter too short for the four-bar window).
inline constexpr std::uint32_t kFinaleRetroRate = 35;
inline constexpr int kFinaleBeatsPerCut = 2;
inline constexpr std::array<show::Effect, 6> kFinaleCuts = {show::Effect::Flyby,  show::Effect::Dots,
                                                            show::Effect::Tunnel, show::Effect::Glenz,
                                                            show::Effect::Lens,   show::Effect::Raytrace};
// The flash: the index the frame is cleared to; every palette entry but 0 (black, as always) is white.
inline constexpr std::uint8_t kFinaleWhite = 255;

// The cuts of a FINALE frame's chapter: the retro frames their beats fall in (the flash frames), ascending.
std::vector<std::uint32_t> finaleCutFrames(const show::TimelineFrame& frame);
// The first beat of the grid at or after `offset` track seconds (within 1e-6): the beat a chapter starting
// there starts on; beat 0 for an offset before the track's first beat.
int finaleFirstBeat(const show::BeatGridView& grid, double offset);

// A sub-effect's home chapter, and the frame it gets there at a home retro frame: the timeline in the middle
// of that retro frame (clamped into the chapter).
int finaleHomeChapter(show::Effect effect);
show::TimelineFrame finaleHomeFrame(show::Effect effect, std::uint32_t homeRetroFrame);
// Where a sub-effect's cut starts in its home chapter, in home retro frames: FLYBY at 60 % (the side view),
// DOTS just before the year arrives, TUNNEL once the solid tube is whole, GLENZ on its accent, LENS at
// kFinaleLensStartSeconds (the lens wholly on the screen, high, at the right end of its run, where
// rotozoom.hpp's lens(t) has tri = 1: t = 6.2 / 2; finale_test checks where it is), RAYTRACE halfway
// (mid-orbit).
inline constexpr double kFinaleLensStartSeconds = 3.1;
std::uint32_t finaleHomeStart(show::Effect effect);

// The EffectInfo parts of FINALE: the resolver, and the flash (its own entry: a clear to kFinaleWhite, every
// colour white).
EffectFrame finaleResolve(const show::TimelineFrame& frame, std::uint32_t retroFrame);
void finalePalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::uint8_t finaleBackground(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
