#pragma once

#include "show/timeline.hpp"

namespace evo::render {

// How crt.comp frames the 1993 picture while a Portal chapter's camera nears the CRT's glass: the
// picture magnified about a centre, so that the beam's lines and the aperture grille resolve as the camera
// closes in. Everywhere else the identity view, and crt.comp's output is exactly the plain full-frame CRT.
//   FLYBY   the Studio's own pull-out seen on the full-frame picture: from the pillarbox at D - 2 bars in to
//           six times closer by D - 0.5 s, then back out to the picture filling the frame's width on D, where
//           the Studio takes over at the same framing (modern::studioScreenSpan);
//   TUNNEL  from the pillarbox at D - 2 bars in to six times on D (smoothstep on the logarithm of the
//           magnification), centred on where the tunnel's path leads (retro::tunnelRingCentre).
// The magnification on a w x h output with a content rectangle cw wide is max(1, zoom * mix(1, w / cw,
// fill)).
struct CrtView {
    float zoom = 1.f;
    float fill = 0.f;
    float centre[2] = {0.5f, 0.5f}; // the point of the picture that stays put, 0..1 across and down
};
CrtView crtView(const show::TimelineFrame& frame);

// Whether a view is the identity (crt.comp then runs its plain full-frame path).
inline bool isIdentity(const CrtView& view) {
    return view.zoom == 1.f && view.fill == 0.f;
}

} // namespace evo::render
