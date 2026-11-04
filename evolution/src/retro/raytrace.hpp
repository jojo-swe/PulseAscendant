#pragma once

#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>

namespace evo::retro {

// RAYTRACE (chapter 7): the "precalculated" ray-traced animation of 1993, the kind that took a night per
// frame and played back at a few frames a second. shaders/retro/raytrace.comp traces one ray per pixel of the
// 320x200 index image (deliberately aliased): a fixed chrome sphere in the middle, four chrome spheres and
// one glass-coloured one bouncing round it, above an infinite red and white checkerboard, one point light
// with hard shadows, two reflection bounces and a sky gradient. Its colour goes through a fixed palette (the
// 6x6x6 colour cube and a grey ramp) with 4x4 Bayer dithering. 10 retro frames a second; the spheres move
// with pc.retroSeconds, so a frame is a pure function of retroFrame.
inline constexpr const char* kRaytraceShader = "retro_raytrace.comp.spv";

// The palette (fixed): index 0 black, 1..15 unused (black); 16..231 the 6x6x6 cube, entry 16 + 36 r + 6 g +
// b showing (51 r, 51 g, 51 b) for levels 0..5; 232..255 the 24 greys between, entry 232 + k showing
// 8 + 10 k (8, 18, ..., 238).
inline constexpr std::uint8_t kRaytraceCube = 16;
inline constexpr int kRaytraceCubeLevels = 6;
inline constexpr int kRaytraceCubeStep = 51;
inline constexpr std::uint8_t kRaytraceGreys = 232;
inline constexpr int kRaytraceGreyCount = 24;
void raytracePalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The 4x4 Bayer threshold 0..15 of a pixel, as shaders/retro/common.glsl's bayer4.
std::uint32_t raytraceBayer(std::uint32_t x, std::uint32_t y);

// The CPU reference of the shader's quantiser (shaders/retro/raytrace_quantise.glsl), in integers so both
// agree exactly. A colour (r, g, b) in 0..255 at a pixel with Bayer threshold `bayer` is first moved by the
// threshold, as a fraction of a cube step: T = c + 51 ((bayer + 0.5) / 16 - 0.5) on every channel. The index
// is the palette entry nearest T (squared distance in RGB) among two candidates: the cube entry with each
// level round(T / 51), which is floor(c / 51 + (bayer + 0.5) / 16), and the grey whose value is nearest
// the mean of T, round((mean - 8) / 10) clamped to 0..23; the grey wins only when strictly nearer. These
// are the nearest cube entry and the nearest grey, so the result is the nearest of all 240 entries.
std::uint8_t raytraceQuantise(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint32_t bayer);

// The scene, which the shader computes from pc.retroSeconds the same way (the constants must match
// raytrace.comp). Sphere 0 is the fixed chrome sphere in the middle; spheres 1..5 orbit it at
// kRaytraceOrbitRadius, bouncing; sphere kRaytraceGlassSphere is the glass-coloured one, the rest chrome.
struct RaytraceSphere {
    double centre[3];
    double radius;
};
inline constexpr int kRaytraceSpheres = 6;
inline constexpr int kRaytraceGlassSphere = 3;
inline constexpr double kRaytraceOrbitRadius = 3.0;
// Sphere i >= 1 at time t: angle a = phase(0.6 t + (i - 1) 2 pi / 5), centre (3 sin a, 0.6 + 0.9
// |sin(phase(1.7 t + 1.3 i))|, -3 cos a), radius 0.6, where phase reduces an angle to [-pi, pi) (as the
// shader does before every sin and cos, whose accuracy far from 0 differs between GPU vendors).
double raytracePhase(double radians);
std::array<RaytraceSphere, kRaytraceSpheres> raytraceSpheres(double seconds);
// The fixed camera: y up, the checkerboard the plane y = 0 with squares 1 unit wide. The shader's ray through
// a pixel centre is the one retro::project maps back to it.
Camera raytraceCamera();

} // namespace evo::retro
