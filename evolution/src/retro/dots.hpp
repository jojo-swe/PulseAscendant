#pragma once

#include "retro/dots_schedule.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace evo::retro {

// DOTS (chapter 2): a cloud of 512 points that morphs cube -> sphere -> torus -> the year "1993" and back
// (torus -> sphere -> cube), each point depth-shaded through a 32-entry ramp and drawn 2x2 when near. The
// solids spin on two axes; the year faces the camera and sways a little, so it stays legible. A scene-only
// effect on black at 35 retro frames per second; everything is a function of (chapterDuration,
// accentSeconds, retroFrame), with no state carried between frames.
//
// The schedule (seconds t = retroFrame / 35 into the chapter, A = accentSeconds held within 30..70 % of the
// chapter, D = chapterDuration): the cube at 0, then each shape arrives at A/3 (sphere), 2A/3 (torus) and A
// (the year, on the chapter's strongest hit); the year holds, and the torus, sphere and cube arrive at
// A + 0.45, 0.65 and 0.85 of (D - A); the cube holds to the end. Each morph takes the last kDotsMorphSeconds
// before its arrival (less if the interval is shorter) and blends the two shapes' world positions with
// smoothstep; between morphs a shape holds exactly.
inline constexpr std::size_t kDotsPoints = 512;

// DOTS' palette: index 0 black, a 32-entry depth ramp from kDotsRamp (far: deep blue, through blue and cyan,
// to white: near), 255 white; the rest black.
inline constexpr std::uint8_t kDotsRamp = 32;
inline constexpr std::uint8_t kDotsShades = 32;
inline constexpr std::uint8_t kDotsWhite = 255;

enum class DotsShape { Cube, Sphere, Torus, Year };

// The shapes in model space (x right, y up, z away from the camera), 512 points each:
// - Cube: the 8 x 8 x 8 lattice of side kDotsCubeSide, centred.
// - Sphere: 512 points spread evenly over a sphere of radius kDotsSphereRadius (a Fibonacci spiral).
// - Torus: 32 x 16 points on a torus round the y axis, radii kDotsTorusMajor and kDotsTorusMinor.
// - Year: "1993" in the 8x8 font (text::glyph), one cell of kDotsYearCell per set pixel, centred on the
//   set pixels' bounding box and brought kDotsYearForward towards the camera; the 512 points are dealt to
//   the set pixels in turn as evenly as they go (pixel j gets floor((j + 1) * 512 / n) - floor(j * 512 / n)
//   of the n), each pixel's k-th point at corner k % 4 of a 2x2 grid inside its cell, in layer k / 4 a
//   further kDotsYearLayer deep, so the year reads as a dot matrix from the front and shows its depth when
//   it sways.
inline constexpr double kDotsCubeSide = 1.8;
inline constexpr double kDotsSphereRadius = 1.3;
inline constexpr double kDotsTorusMajor = 1.1;
inline constexpr double kDotsTorusMinor = 0.45;
inline constexpr double kDotsYearCell = 0.15;
inline constexpr double kDotsYearForward = 0.7;
inline constexpr double kDotsYearLayer = 0.12;
inline constexpr const char* kDotsYearText = "1993";

// The fixed camera kDotsCameraDistance in front of the origin; the view depths where the shade ramp ends
// (kDotsNear: shade 31, kDotsFar: shade 0) and below which a point is drawn 2x2 (kDotsBig).
inline constexpr double kDotsCameraDistance = 6.0;
inline constexpr double kDotsNear = kDotsCameraDistance - 1.3;
inline constexpr double kDotsFar = kDotsCameraDistance + 1.3;
inline constexpr double kDotsBig = kDotsCameraDistance - 0.35;

using DotsPoints = std::array<std::array<double, 3>, kDotsPoints>;

// A shape's model points (built once).
const DotsPoints& dotsModel(DotsShape shape);

// A shape's points in world space at a retro frame, in the model's order: the solids turned on two axes
// (about y, then x, each at its own rate), the year swaying gently about y and x while it faces the camera.
DotsPoints dotsShapePoints(DotsShape shape, std::uint32_t retroFrame);

// Where the schedule is at a retro frame: morphing from one shape to the next with the smoothstep-eased
// blend in (0, 1), or holding a shape (from == to, blend 0).
struct DotsMorph {
    DotsShape from, to;
    double blend;
};
DotsMorph dotsMorph(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The schedule (DotsStep, dotsSchedule, kDotsRetroRate and kDotsMorphSeconds) is in retro/dots_schedule.hpp.

// The world positions of the 512 points at a retro frame. A morph pairs the two shapes' points by rank under
// a common key, computed at the morph's first frame (the points sorted by height into 8 bands of 64, each
// band by angle round the y axis seen from the camera), so each point flows to a nearby one instead of
// shuffling; point k blends from its rank in the first shape to the same rank in the second.
DotsPoints dotsPositions(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The fixed camera, looking at the origin along +z.
Camera dotsCamera();

// The EffectInfo parts of DOTS (see retro/effects.hpp).
void dotsPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
// The 512 points projected through dotsCamera(), back to front; shade 31 at view depth kDotsNear and 0 at
// kDotsFar (clamped), size 2 nearer than kDotsBig, else 1.
void dotsScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);
// The same for any 512 world positions (REBOOT shows the cube alone with it).
void dotsSceneFrom(RetroScene& scene, const DotsPoints& positions);

} // namespace evo::retro
