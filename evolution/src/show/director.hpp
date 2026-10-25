#pragma once

#include "show/cues.hpp"
#include "show/timeline.hpp"

#include <array>
#include <optional>

namespace evo::show {

// The CRT crossing of a Portal chapter around D: approach over [D - 2 bars, D - 0.5 s], glass over
// [D - 0.5 s, D + 0.5 s] (1 exactly at D), inside over [D + 0.5 s, D + 2 bars]. All 0 outside Portal
// chapters.
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

// --- Scene cuts
// -----------------------------------------------------------------------------------------------
//
// A frame may show another chapter's 2026 scene than its own: the FINALE megamix cuts the whole frame to the
// best shots of the show, and REBOOT is already looking at the galaxy from its crossing on. The 1993 side,
// the compositor and the cards keep following the frame itself; only the 2026 scene, its camera and its
// history follow the cut.

// The 2026 scene of `chapter` at show time `showSeconds`. A frozen cut holds one scene time.
struct SceneCut {
    int chapter;
    double showSeconds;
    bool frozen;
};

// One row of the FINALE megamix. From `quarter` quarter beats after the chapter's first tracked beat and for
// `quarters` quarter beats, the frame shows `source`'s scene from `sourceSeconds` on (held there when
// `frozen`). A row whose source is FINALE itself is the room: the chapter's own Studio, no cut. The rows
// follow each other without a gap and the last one ends on D, where the room takes over for good.
struct MegamixShot {
    int quarter, quarters;
    Effect source;
    double sourceSeconds;
    bool frozen;
};

// clang-format off
inline constexpr std::array<MegamixShot, 11> kMegamixShots = {{
    { 0, 8, Effect::Raytrace, 133.0,  false}, // low tracking between the chrome balls
    { 8, 8, Effect::City,     160.5,  false}, // the puddle at street level
    {16, 8, Effect::Dots,      41.0,  false}, // the logo landed
    {24, 4, Effect::Lens,     105.45, false}, // the glass sphere on the stone
    {28, 2, Effect::Glenz,     97.4,  false}, // the caustic on the floor
    {30, 2, Effect::Finale,     0.0,  false}, // the room
    {32, 4, Effect::Finale,     0.0,  false}, // the room, a card on its first frames
    {36, 2, Effect::Tunnel,    78.0,  false}, // the points as lights
    {38, 1, Effect::Nebula,   280.2,  false}, // the star round the pillar
    {39, 1, Effect::Wormhole, 494.15, false}, // the ship crossing
    {40, 4, Effect::Raytrace, 128.10, true},  // the drop, held
}};
// clang-format on

// The megamix's length: from the chapter's first beat to D.
inline constexpr int kMegamixQuarters = 44;

constexpr bool megamixRowsChain() {
    int at = 0;
    for (const MegamixShot& shot : kMegamixShots) {
        if (shot.quarter != at || shot.quarters <= 0)
            return false;
        at += shot.quarters;
    }
    return at == kMegamixQuarters && kMegamixShots.back().source != Effect::Finale;
}
static_assert(megamixRowsChain(), "the megamix rows must follow each other and end on a cut at D");

// When megamix row `row` starts, in show seconds; row kMegamixShots.size() is the end of the last row (D).
// A quarter beat is a quarter of the tracked beat it falls in, so every boundary lies on the beat grid.
double megamixStart(int row);

// The scene cut of a frame, if any:
//   FINALE  each megamix row that is not the room cuts to its source's scene at sourceSeconds plus the time
//           since the row started (or at sourceSeconds alone, frozen);
//   REBOOT  from D on, GALAXY's scene at the frame's own time (its local time negative until GALAXY starts).
// Every other frame shows its own chapter's scene. A pure function of the frame's effect and time.
std::optional<SceneCut> sceneCut(const TimelineFrame& frame);

// The frame the cut's scene renders: sampleTimeline(cut.showSeconds), moved into cut.chapter when that time
// lies outside it (REBOOT's look ahead at GALAXY: the chapter, effect, transition, cards, duration and accent
// of GALAXY with a negative local time, and the music and flow clock of the time itself), and carrying the
// frame's own era, wipe, eraCut and swapCount.
TimelineFrame sceneFrame(const TimelineFrame& frame, const SceneCut& cut);

// The frame the 2026 side renders: sceneFrame(frame, cut) under a cut, the frame itself otherwise.
TimelineFrame sceneFrame(const TimelineFrame& frame);

// The director's part of a frame, after applyEra: in FINALE the era says what the cards show, 1 (the 2026
// card) during a cut and 0 (the 1993 card) in the room. Everything else is left as applyEra made it.
void applyDirection(TimelineFrame& frame);

// Whether the compositor shows the 2026 picture over the whole frame whatever the era: FINALE, where the
// megamix's cuts and the Studio between them are both 2026 pictures (the 1993 grid is on the Studio's CRT).
bool holdsModern(const TimelineFrame& frame);

// The white the compositor adds over the whole frame for an era flip: kEraFlash * eraCut, and none in FINALE
// (the cuts are the event) or in a Portal chapter (the glass is).
inline constexpr float kEraFlash = 0.25f;
float eraFlash(const TimelineFrame& frame);

} // namespace evo::show
