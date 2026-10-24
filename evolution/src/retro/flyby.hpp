#pragma once

#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"

#include <cstddef>
#include <cstdint>

namespace evo::retro {

// FLYBY (chapter 1): an original low-poly arrow-head fighter, flat shaded on the CPU, over a starfield that
// streams towards the camera, while the greeting scroller (retro/scroller.hpp) runs low over it. A scene-only
// effect on black at 35 retro frames per second; everything is a function of (chapterDuration, retroFrame).
//
// Over the chapter (progress = retroFrame / (chapterDuration * 35), eased) the camera swoops from behind and
// below the ship to a three-quarter view of its right side, a little above it, while the ship banks gently.
// The ship keeps to the upper part of the frame, clear of the epoch card bottom-left.
inline constexpr std::uint32_t kFlybyRetroRate = 35;

// FLYBY's palette layout: 16-shade ramps, dark to light, for the ship's faces (shade = flat Lambert
// intensity from one directional light, plus ambient), the engine glow (orange to white; the nozzles and
// flames pick their entry by retroFrame, so they flicker) and the stars (grey, brighter the nearer).
// Index 0 is the black of space and the scroller's shadow, 255 the scroller's white; the rest is black.
inline constexpr std::uint8_t kFlybyShades = 16;
inline constexpr std::uint8_t kFlybyHull = 16;    // 16..31 grey-blue
inline constexpr std::uint8_t kFlybyWing = 32;    // 32..47 teal
inline constexpr std::uint8_t kFlybyCockpit = 48; // 48..63 amber
inline constexpr std::uint8_t kFlybyEngine = 64;  // 64..79 orange -> white
inline constexpr std::uint8_t kFlybyStar = 128;   // 128..143 greys
inline constexpr std::uint8_t kFlybyWhite = 255;
// Every index the ship's triangles use lies in [kFlybyShipFirst, kFlybyShipLast].
inline constexpr std::uint8_t kFlybyShipFirst = kFlybyHull;
inline constexpr std::uint8_t kFlybyShipLast = kFlybyEngine + kFlybyShades - 1;

// The ship's model: faces before back-face culling (fuselage 34, wings 12, engine pods 24, flames 4).
inline constexpr std::size_t kFlybyShipTriangles = 74;
// The stars of the field; those the ship covers are left out of a frame's scene.
inline constexpr std::size_t kFlybyStars = 512;

// The camera of retro frame `retroFrame` (the ship sits at the world origin, nose along +z, y up).
Camera flybyCamera(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo parts of FLYBY (see retro/effects.hpp); the text is flybyText of retro/scroller.hpp.
void flybyPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
// The ship's visible faces in painter's order (a fixed ordering of its convex parts chosen by which side of
// their separating planes the camera is on; within a part, back-face culling alone), then the stars back to
// front, each left out if any of its pixels lies under the ship (points are drawn after all triangles).
void flybyScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
