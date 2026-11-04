#include "render/crt_view.hpp"

#include "modern/studio.hpp"
#include "retro/tunnel.hpp"
#include "show/era.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace evo::render {

namespace {

// The tunnel's heading on the 1993 screen at t seconds into the chapter: the centre of the ring kAhead units
// down the tunnel, projected as retro/tunnel.cpp projects (a 70 degree field of view at a pixel aspect of
// 1.2), as a fraction of the screen.
constexpr double kAhead = 6.0;

void tunnelHeading(double localSeconds, float out[2]) {
    const double s = retro::kTunnelSpeed * localSeconds;
    const std::array<double, 2> c = retro::tunnelRingCentre(s, kAhead);
    const double focalY = 100.0 / std::tan(retro::kTunnelFovY * std::numbers::pi / 360.0);
    const double focalX = focalY * 1.2;
    out[0] = static_cast<float>(std::clamp((160.0 + focalX * c[0] / kAhead) / 320.0, 0.0, 1.0));
    out[1] = static_cast<float>(std::clamp((100.0 - focalY * c[1] / kAhead) / 200.0, 0.0, 1.0));
}

} // namespace

CrtView crtView(const show::TimelineFrame& frame) {
    CrtView view;
    if (frame.transition != show::Transition::Portal)
        return view;
    const show::TransitionMarks marks = show::transitionMarks(frame);
    const double t = frame.showSeconds;
    const double from = marks.downbeat - 2 * marks.bar;
    if (t < from || t >= marks.downbeat)
        return view;
    switch (frame.effect) {
    case show::Effect::Flyby:
        view.zoom = modern::studioScreenSpan(frame);
        view.fill = 1.f;
        break;
    case show::Effect::Tunnel: {
        const double x = std::clamp((t - from) / (marks.downbeat - from), 0.0, 1.0);
        const double eased = x * x * (3 - 2 * x);
        view.zoom = static_cast<float>(std::exp(std::log(6.0) * eased));
        float heading[2];
        tunnelHeading(frame.localSeconds, heading);
        view.centre[0] = static_cast<float>(0.5 + (heading[0] - 0.5) * eased);
        view.centre[1] = static_cast<float>(0.5 + (heading[1] - 0.5) * eased);
        break;
    }
    default:
        break;
    }
    return view;
}

} // namespace evo::render
