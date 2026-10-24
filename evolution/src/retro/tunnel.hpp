#pragma once

#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace evo::retro {

// TUNNEL (chapter 4): the classic dot-ring tunnel. 96 rings of 64 dots fly at the camera; each ring's centre
// follows a path of summed sines over depth, so the tunnel snakes away into the distance; the dots fade
// into the dark through a 64-entry ramp, and the whole tunnel twists slowly. From halfway through the
// chapter a solid tube of flat-shaded quads (a two-colour checker, shaded by depth and by a light) flows in
// from the far end and replaces the dots. A scene-only effect on black at 35 retro frames per second;
// everything is a closed-form function of (chapterDuration, retroFrame), with nothing accumulated.
//
// Tunnel space is the camera's view space: the camera sits at the origin looking along +z (y up). The
// camera has travelled s = kTunnelSpeed * retroFrame / 35 units along the tunnel. The tunnel's rings are
// fixed in the world at depths w = j * kTunnelRingSpacing (integer j); a ring is seen at view depth
// z = w - s. Its centre lies on the path
//   P(w) = (sum over kTunnelPathX of a sin(f w + p), sum over kTunnelPathY of a sin(f w + p)),
// which the camera follows along its tangent: in view space the ring's centre is
//   C(z) = P(s + z) - P(s) - z P'(s),
// so the nearest rings stay centred and the far ones swing round the bends. Dot i of the ring lies at
//   C(z) + kTunnelRadius (cos a, sin a),   a = 2 pi i / 64 + tunnelTwist(retroFrame, w),
// at depth z.
inline constexpr std::uint32_t kTunnelRetroRate = 35;
inline constexpr std::size_t kTunnelRings = 96;
inline constexpr std::size_t kTunnelRingDots = 64;
inline constexpr double kTunnelRadius = 1.0;
inline constexpr double kTunnelRingSpacing = 0.25;
inline constexpr double kTunnelSpeed = 3.0; // units per second
// The 96 rings drawn are those with kTunnelNear < z <= kTunnelFar.
inline constexpr double kTunnelNear = 0.5;
inline constexpr double kTunnelFar = kTunnelNear + kTunnelRings * kTunnelRingSpacing;
inline constexpr double kTunnelFovY = 70; // degrees

struct TunnelWave {
    double amplitude, frequency, phase; // units, radians per unit of depth, radians
};
inline constexpr std::array<TunnelWave, 2> kTunnelPathX = {{{2.6, 0.13, 0.0}, {1.0, 0.31, 1.1}}};
inline constexpr std::array<TunnelWave, 2> kTunnelPathY = {{{1.6, 0.17, 0.6}, {0.6, 0.37, 2.3}}};

// The twist: a = ... + kTunnelTwistRate * t + kTunnelTwistPerDepth * w, t = retroFrame / 35 seconds.
inline constexpr double kTunnelTwistRate = 0.35;     // radians per second
inline constexpr double kTunnelTwistPerDepth = 0.08; // radians per unit of depth

// The dots: shade (0 far .. 63 near) = round(63 * f^1.5), f = (kTunnelFar - z) / (kTunnelFar - kTunnelNear)
// clamped to 0..1, so the far end sinks into the dark; size 3 nearer than kTunnelDotsLarge, 2 nearer than
// kTunnelDotsMedium, else 1. Before the tube all 96 x 64 = 6144 dots go to addPoint each frame, which leaves
// out those whose square lies off the screen (some of the nearest rings' dots: 5946..6015 remain in this
// chapter).
inline constexpr double kTunnelDotsLarge = 2.5;
inline constexpr double kTunnelDotsMedium = 6.0;

// The solid tube, from the retro frame floor(chapterDuration * 35 / 2): kTunnelSegments quads round each
// band between two tube rings kTunnelBandLength apart (tube ring m at w = m * kTunnelBandLength, its vertex
// k at angle 2 pi k / 16 + twist), for the bands between view depths kTunnelTubeNear and kTunnelTubeFar.
// Over kTunnelTubeSweepSeconds the tube's front sweeps (smoothstep) from kTunnelFar to kTunnelTubeNear:
// bands wholly beyond the front (and within kTunnelTubeFar) are drawn, and only the dots of rings nearer
// than it. Each quad is two flat triangles in one index: ramp A where m + k is even, B where odd (a checker
// that scrolls with the flight), its shade fading from 31 near to 0 at kTunnelTubeFar, times a light on the
// side the camera sees. The bands go far to near (painter's order: each band is a slab of depth, nearer
// slabs drawn later), each band's faces seen from inside before those seen from outside (a band is convex).
// A face seen from outside is culled when it projects wholly inside the mouth (the nearest band's near ring),
// since a ray through the mouth meets the inside of a wall first; so the tube's outside shows only during
// the sweep, where the far end bends out of the mouth's view. At most 40 bands x 16 quads x 2 = 1280
// triangles before culling and clipping; 780..927 drawn in this chapter once the tube is whole.
inline constexpr std::size_t kTunnelSegments = 16;
inline constexpr double kTunnelBandLength = 2 * kTunnelRingSpacing;
inline constexpr double kTunnelTubeNear = 0.2;
inline constexpr double kTunnelTubeFar = 20.2;
inline constexpr double kTunnelTubeSweepSeconds = 1.5;

// The palette: index 0 black; the dot ramp (far: near black, through violet and pink, to warm white near);
// two tube ramps of 32 shades for the checker (A: blue, B: violet), each dark far to light near; 255 white;
// the rest black.
inline constexpr std::uint8_t kTunnelDotRamp = 64; // 64..127
inline constexpr std::uint8_t kTunnelDotShades = 64;
inline constexpr std::uint8_t kTunnelTubeA = 128; // 128..159
inline constexpr std::uint8_t kTunnelTubeB = 160; // 160..191
inline constexpr std::uint8_t kTunnelTubeShades = 32;
inline constexpr std::uint8_t kTunnelWhite = 255;

// The path P(w) and its slope P'(w).
std::array<double, 2> tunnelPath(double w);
std::array<double, 2> tunnelPathSlope(double w);
// How far the camera has travelled at a retro frame.
double tunnelTravel(std::uint32_t retroFrame);
// C(z): the centre in view space of the ring at view depth z once the camera has travelled s. C(0) = 0 and
// C'(0) = 0, so the nearest rings sit on the view axis.
std::array<double, 2> tunnelRingCentre(double s, double z);
// The twist angle of the tunnel at depth w at a retro frame.
double tunnelTwist(std::uint32_t retroFrame, double w);

// One ring of dots at a retro frame: its world depth w, view depth z and centre C(z) in view space.
struct TunnelRing {
    double w, z;
    std::array<double, 2> centre;
};
// The 96 rings with kTunnelNear < z <= kTunnelFar, far to near.
std::vector<TunnelRing> tunnelRings(std::uint32_t retroFrame);

// The first retro frame of the solid tube, and the view depth of the tube's front at a retro frame
// (infinity before the tube: dots only; kTunnelTubeNear once it is whole: the tube only).
std::uint32_t tunnelTubeFrame(const show::TimelineFrame& frame);
double tunnelTubeFront(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// One face of the tube in view space as tunnelScene hands it to addTriangle, in drawing order. With cull
// false, also the faces seen from outside that the mouth hides, which the scene leaves out (for tests).
struct TunnelFace {
    double world[3][3];
    std::uint8_t index;
};
std::vector<TunnelFace> tunnelTubeFaces(const show::TimelineFrame& frame, std::uint32_t retroFrame,
                                        bool cull = true);

// The fixed camera at the origin, looking along +z.
Camera tunnelCamera();

// The EffectInfo parts of TUNNEL (see retro/effects.hpp).
void tunnelPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
// The tube's faces (tunnelTubeFaces), then the dots of the rings nearer than the tube's front, far to near
// (each ring's dots in order i = 0..63), those whose square lies wholly off the screen left out.
void tunnelScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
