#pragma once

// GLENZ (chapter 5): the classic glenz vector of Second Reality, a translucent star-cube spinning on three
// axes and bouncing on the beat, over copper bars.
//
// The mesh (14 vertices, 24 triangles: a cube with a pyramid raised on each face) is Future Crew's, from
// GLENZ/MAIN.C of Second Reality (1993), released under the Unlicense
// (https://github.com/mtuomi/SecondReality, commit 071a82eddb7a7390dbaa491a8a473e0e923a55c2). It is ported
// through the parent project's src/scene_math.cpp: the original's points are 100 * ZZZ with ZZZ = 1, divided
// by 100 here; the triangle indices are unchanged. Credit to Future Crew (licenses/FUTURE-CREW-NOTICE.md);
// this is not an official Future Crew release.

#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace evo::retro {

inline constexpr std::uint32_t kGlenzRetroRate = 35;
// The copper bars behind the glenz (shaders/retro/copper.comp); its clock is pc.retroSeconds.
inline constexpr const char* kGlenzCopperShader = "retro_copper.comp.spv";

// The palette, one byte split into fields that the glenz faces OR into (polys.comp's glenz mode: the pixel
// becomes the copper index | the OR of every covering face's index):
//   bits 0..3  the copper bar under the pixel, written by copper.comp: 0 black, then 3 bars (red, gold,
//              green) of 5 brightness steps each, 1 + 5 * bar + step (step 4 brightest);
//   bit 4      code 1: a front face covers the pixel (kGlenzFront);
//   bit 5      code 2: a back face covers the pixel (kGlenzBack);
//   bit 6      a covering front face is of the second checker colour (kGlenzFrontB);
//   bit 7      a covering back face is of the second checker colour (kGlenzBackB).
// So the 16 copper steps come in the 4 code combinations (none, front, back, both), each in the checker
// colours of the faces: the palette has an entry for every byte, the copper seen through light-blue glass
// where a front face covers it, through deep blue-violet where a back face does, and through both where they
// overlap. A closed mesh puts a front and a back face over every pixel of its silhouette, so the code is 3
// there; the faces alternate between two colours, as in the original, so the facets and the far side's edges
// show through the near side.
inline constexpr std::uint8_t kGlenzCopperMask = 0x0f;
inline constexpr std::uint8_t kGlenzCopperBars = 3;
inline constexpr std::uint8_t kGlenzCopperSteps = 5;
// The face codes. The star-cube is not convex, so a pixel may lie under two faces of the same side; their
// codes OR to that side's second checker colour (kGlenzFrontB, kGlenzBackB) whenever either face carries it:
// lossy, but deterministic. The scene draws the back faces before the front ones only nominally: OR is
// commutative, so the order changes nothing.
inline constexpr std::uint8_t kGlenzFront = 0x10;
inline constexpr std::uint8_t kGlenzBack = 0x20;
inline constexpr std::uint8_t kGlenzFrontB = 0x40;
inline constexpr std::uint8_t kGlenzBackB = 0x80;

inline constexpr std::size_t kGlenzVertices = 14;
inline constexpr std::size_t kGlenzTriangles = 24;

using GlenzPoint = std::array<double, 3>;
using GlenzVertices = std::array<GlenzPoint, kGlenzVertices>;

// The mesh in model space: the cube's corners at +-1, the pyramids' tips at 1.7 on the axes. The triangles
// keep the original's vertex order: a face seen from outside runs counter-clockwise on the screen (its cross
// product in y-down pixel coordinates is negative), and its right-handed normal points inward.
struct GlenzMesh {
    GlenzVertices vertices;
    std::array<std::array<int, 3>, kGlenzTriangles> triangles;
};
const GlenzMesh& glenzMesh();

// Size, spin and bounce: the mesh scaled by kGlenzScale and turned about x, y and z (each at its own rate,
// from the retro frame), then lifted to y = kGlenzFloor + kGlenzLift * glenzBounce(...).
inline constexpr double kGlenzScale = 1.2;
inline constexpr double kGlenzFloor = -0.75;
inline constexpr double kGlenzLift = 1.1;
inline constexpr double kGlenzCameraDistance = 8.0;
// The spin, radians a second about x, y and z, and the starting angles (the 2026 glenz turns the same way,
// modern/scenes/glass.hpp). The star is turned about x, then y, then z.
inline constexpr double kGlenzSpin[3] = {0.83, 1.21, 0.47};
inline constexpr double kGlenzStartAngle[3] = {0.4, 0.2, 0.1};

// Where the bounce is, at `seconds` into the chapter, between the chapter's tracked beats: 0 on a beat,
// rising to 1 just before the next (beats before the first or after the last keep the nearest beat length).
// The beats are fixed data of the chapter's track (show::beatGrid), a fixed fact like chapterDuration and
// accentSeconds: the chapter and the time alone decide the phase, never the frame's music signals.
double glenzBeatPhase(const show::TimelineFrame& frame, double seconds);
// The height of the bounce, 4 p (1 - p) of the beat phase p: 0 (the floor) on every beat, 1 halfway between.
double glenzBounce(const show::TimelineFrame& frame, double seconds);
// The same on an explicit, ascending beat list (glenzBeatPhase passes the track's beats and track time):
// the phase in [0, 1] at time t, 0 on a beat (within 1e-9 before it, as show::BeatGridView::beatAt has it).
// Duplicate beat times count once; with fewer than two distinct beats the phase is 0 throughout. The height
// is continuous in t.
double glenzBeatPhaseAt(std::span<const double> beats, double t);
double glenzBounceAt(std::span<const double> beats, double t);

// The mesh's vertices in world space at a retro frame (the bounce at retroFrame / kGlenzRetroRate).
GlenzVertices glenzWorldVertices(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The fixed camera, kGlenzCameraDistance in front of the origin, looking along +z.
Camera glenzCamera();

// The EffectInfo parts of GLENZ (see retro/effects.hpp).
void glenzPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
// Every triangle in glenz mode: the back faces first, then the front faces, each of index kGlenzBack or
// kGlenzFront, plus kGlenzBackB / kGlenzFrontB on the odd triangles of the mesh (the checker).
void glenzScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
