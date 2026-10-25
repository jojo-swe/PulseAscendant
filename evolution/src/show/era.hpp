#pragma once

#include "show/timeline.hpp"

namespace evo::show {

// Fills the 1993/2026 state of a frame (era, wipe, eraCut, swapCount) from its chapter's transition. The
// frame must already carry its chapter, transition, track, trackSeconds and accentSeconds; grid is the beat
// grid of that track. Every value depends only on the chapter and the time, so seeking gives the same frame
// as playing.
//
// All times are track seconds. A is the accent (chapter start + accentSeconds), D the downbeat at or before
// A, and a bar is four beats measured at D.
//   Cut      era = the cue's eraFixed for the whole chapter.
//   Portal   era rises smoothly from A - 1 s to A + 1 s; eraCut flashes to 1 at A and decays.
//   Wipe     wipe (and era) run linearly from 0 to 1 over the two bars ending at D.
//   BeatSwap era flips at the bar lines from D - 4 bars, then on every beat of the last two bars, and holds
//            1 from D on; swapCount counts the flips so far and eraCut flashes on each.
// Chapters too short for the full window start it at the chapter start (BeatSwap then flips on every beat).
void applyEra(TimelineFrame& frame, const BeatGridView& grid);

// The landmarks of a frame's chapter transition in show seconds (applyEra's window): the chapter start, the
// accent A, the downbeat D at or before A (A itself when no beat lies between the start and A), the bar at D.
// bar is a length (four beats measured at D), not a time. Uses the frame's chapter, track and accentSeconds
// and the beat grid of its track.
struct TransitionMarks {
    double start, accent, downbeat, bar;
};
TransitionMarks transitionMarks(const TimelineFrame& frame);

} // namespace evo::show
