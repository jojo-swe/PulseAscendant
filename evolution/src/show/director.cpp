#include "show/director.hpp"

#include "show/era.hpp"

#include <algorithm>
#include <cmath>

namespace evo::show {

namespace {

float ramp(double from, double to, double t) {
    if (to <= from)
        return t >= to ? 1.f : 0.f;
    return float(std::clamp((t - from) / (to - from), 0.0, 1.0));
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

} // namespace evo::show
