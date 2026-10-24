#pragma once

#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

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

// Flat Lambert shading of the hull, wing and cockpit faces: intensity = kFlybyAmbient + max(0, n . light),
// shade = floor(intensity * 16) clamped to 0..15, so a face turned from the light gets shade 0 and one facing
// it (intensity 1 and over) shade 15, never the next ramp's first entry.
inline constexpr double kFlybyAmbient = 0.05;
std::uint8_t flybyShade(double intensity);

// One visible face of the ship in world space, as flybyScene hands it to addTriangle.
enum class FlybyPart : std::uint8_t { Fuselage, Wing, Pod, Flame };
struct FlybyFace {
    double world[3][3];
    std::uint8_t index; // flat palette index, ramp + shade
    std::uint8_t ramp;  // the first entry of its ramp: kFlybyHull, kFlybyWing, kFlybyCockpit or kFlybyEngine
    FlybyPart part;
    int side; // 0 right (+x), 1 left; 0 for the fuselage
};

// The ship's faces the camera sees at retroFrame, in painter's order: a fixed ordering of its convex parts
// chosen by which side of their separating planes the camera is on (a small BSP of the model; within a part,
// back-face culling alone). Exact but for two known seams: a wing root sinks up to 0.075 units into the
// fuselage, so when a wing is drawn after the fuselage a sliver of its root can show through the hull; and an
// engine's two crossed flame triangles cut through each other.
std::vector<FlybyFace> flybyShipFaces(const show::TimelineFrame& frame, std::uint32_t retroFrame);
// The same seen from another camera, the ship's attitude still that of retroFrame (PORTAL flies the ship
// away); flybyShipFaces uses flybyCamera. FLYBY's flight never brings the camera within |x| <= 0.34 of the
// ship's centre plane, where the fuselage is drawn after the near side, but a reuse at other angles may.
std::vector<FlybyFace> flybyShipFacesFrom(const Camera& camera, std::uint32_t retroFrame);
// The stars of the field; those the ship covers are left out of a frame's scene.
inline constexpr std::size_t kFlybyStars = 512;

// The camera of retro frame `retroFrame` (the ship sits at the world origin, nose along +z, y up).
Camera flybyCamera(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo parts of FLYBY (see retro/effects.hpp); the text is flybyText of retro/scroller.hpp.
void flybyPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
// The ship's faces (flybyShipFaces, in that order), then the stars back to front, each left out if any of its
// pixels lies under the ship (points are drawn after all triangles). The ship's order is its painter's order:
// do not call sortBackToFront on this scene (FINALE reuses this provider), as mean depths misorder its parts.
void flybyScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);
// FLYBY's scene seen from another camera (the stars keep their own); flybyScene uses flybyCamera.
void flybySceneFrom(RetroScene& scene, const Camera& camera, std::uint32_t retroFrame);

} // namespace evo::retro
