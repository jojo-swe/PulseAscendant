#pragma once

#include "show/timeline.hpp"

namespace evo::show {

// The CRT crossing of a Portal chapter around D: approach over [D - 2 bars, D - 0.5 s], glass over
// [D - 0.5 s, D + 0.5 s] (1 exactly at D), inside over [D + 0.5 s, D + 2 bars]. All 0 outside Portal chapters.
//   approach  linear 0 -> 1 across its span, 0 before it and 1 after it;
//   glass     clamp(1 - |t - D| / 0.5 s, 0, 1): a tent peaking on the glass plane;
//   inside    linear 0 -> 1 across its span, 0 before it and 1 after it;
//   sinceGlass t - D in seconds (negative before the glass).
// approach and inside are linear on purpose: the cameras and grades that read them ease them as they need.
// D and the bar are transitionMarks(frame)'s, t is the frame's show time, so the phase is a pure function of
// the time.
struct PortalPhase {
    float approach, glass, inside;
    double sinceGlass;
};
PortalPhase portalPhase(const TimelineFrame& frame);

// The half width of the glass phase around D, in seconds.
inline constexpr double kPortalGlassHalfWidth = 0.5;
// The approach starts and the inside ends this many bars from D.
inline constexpr double kPortalBars = 2;

} // namespace evo::show
