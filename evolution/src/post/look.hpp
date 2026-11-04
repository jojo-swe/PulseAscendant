#pragma once

#include "post/grade.hpp"
#include "show/timeline.hpp"

#include <cstdint>
#include <span>

namespace evo::post {

// The 2026 picture's look at one show time: everything after the trace that the direction sets (the per-act
// grade and grain, the bloom weight, the authored exposure and the palette grade after a CRT crossing). A
// pure function of the frame's show time and chapter: no measured exposure anywhere.
//
// Acts by chapter: I 0-10 (phosphor and tungsten), II 11-19 (sodium and ice), III 20-28 (solar and mineral);
// CREDITS and THE END (29, 30) are 1993 only and get no look at all.
//   Act I    bloom w 0.020, split tone 2 % (warm shadows, cool highlights), grain 0.4 %, true blacks
//   Act II   bloom w 0.025, S-curve 1.05 at mid grey, blacks lifted to 0.004 tinted (0.6, 0.8, 1.0), grain 0.6 %
//   Act III  bloom w 0.035, no S-curve, shadows 8 % less saturated, grain 0.3 %
// An act's first chapter (STUDIO NIGHT, REBOOT) eases from the previous act's look over its first 2 bars
// (smoothstep): both joins are continuous shots of the Studio.
//
// In a Portal chapter (FLYBY, TUNNEL, PORTAL, REBOOT) the bloom gives way to the CRT's own glow on the
// glass: w (1 - smoothstep(glass)) up to D, 0 at D, back to full by D + 1 bar (smoothstep). From D the
// display colour mixes toward its nearest Oklab entry of the 1993 effect's palette at D's retro frame: weight
// 1 at D, down to 0 by D + 1 bar (smoothstep); none before D, nor on PORTAL's way out (192.27).
struct Look {
    int act = 0;                   // 1, 2 or 3; 0: none (the tonemap's plain bytes)
    float bloom = 0;               // the bloom weight w: (1 - w) I + w (I * PSF)
    Grade grade;                   // grade.palette > 0 only right after a crossing
    float exposureOffset = 0;      // EV added to the scene's exposureEV (exposureOffset(showSeconds))
    GradePalette palette{};        // the palette grade.palette mixes toward (zeros when it is 0)
};
Look lookFor(const show::TimelineFrame& frame);

// The act of a chapter: 1, 2, 3, or 0 for CREDITS and THE END.
int actOf(int chapter);
// The grade and bloom weight of an act on its own (no join, no crossing, grainFrame 0).
Look actLook(int act);

// The authored exposure: EV offsets on show seconds, added to every 2026 scene's own exposureEV. Between two
// keys the curve eases with zero slope at both (an eased cruise: smoothstep-shaped acceleration over the
// first and last quarter, constant speed between, so its peak slope is 4/3 of the mean); two keys at the
// same time are a jump, allowed only on a cut. Before the first key and after the last it holds.
struct ExposureKey {
    double seconds;
    float ev;
};
std::span<const ExposureKey> exposureKeys();
float exposureOffset(double showSeconds);
// The eased cruise from 0 to 1 over u in [0, 1] (clamped), zero slope at both ends.
double easedCruise(double u);

// The bloom's scale and the palette weight of a frame (1 and 0 outside Portal chapters).
float portalBloomScale(const show::TimelineFrame& frame);
float paletteWeight(const show::TimelineFrame& frame);
// The 1993 palette of a Portal chapter's effect at its D (the retro frame shown on the glass).
GradePalette crossingPalette(const show::TimelineFrame& frame);

} // namespace evo::post
