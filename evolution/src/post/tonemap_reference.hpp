#pragma once

#include "post/grade.hpp"

#include <array>
#include <cstdint>

namespace evo::post {

// The tonemap on the CPU, the mirror shaders/post/tonemap.comp is tested against. For linear
// Rec. 709 light `rgb` (a non-finite component counts as 0):
//   hdr * 2^exposureEV -> max(0) per component -> Khronos PBR Neutral -> clamp [0, 1] -> sRGB OETF
//   -> round(v * 255 + d), d a triangular dither in (-0.5, 0.5) from pcg(x, y), clamped to [0, 255].
// Black stays 0; white approaches 255 (64 rounds to it, 16384 is 255 whatever the dither); the dither is
// static per pixel (no temporal shimmer) and the same for the three channels (greys stay grey). PBR Neutral
// keeps the hue: below its knee it only subtracts the same offset from every channel, above it the colour
// moves in the plane of the input and white.

// Khronos PBR Neutral (KhronosGroup/ToneMapping, PBR_Neutral/pbrNeutral.glsl) for non-negative linear light:
// x = the smallest component, offset = x < 0.08 ? x - 6.25 x^2 : 0.04, subtracted from every component; if
// the peak p is then at least startCompression = 0.76, the colour is scaled to the new peak 1 - d^2 / (p + d
// - 0.76) (d = 0.24) and mixed towards that peak's white by 1 - 1 / (0.15 (p - newPeak) + 1). Linear, in [0,
// 1). Must match shaders/post/neutral.glsl.
std::array<float, 3> pbrNeutral(const std::array<float, 3>& rgb);
// The display value in [0, 1] before quantisation: everything above except the dither and the rounding.
std::array<float, 3> tonemapDisplay(const float rgb[3], float exposureEV);
// The sRGB OETF (linear -> encoded) for v in [0, 1]; exactly 1 at 1.
float srgbOetf(float v);
// The dither of pixel (x, y), strictly inside (-0.5, 0.5).
float tonemapDither(std::uint32_t x, std::uint32_t y);
// The byte the tonemap writes for pixel (x, y).
std::array<std::uint8_t, 3> tonemapReference(const float rgb[3], float exposureEV, std::uint32_t x,
                                             std::uint32_t y);

// The graded tonemap (tonemap.comp with ToneInputs, no bloom) of pixel (x, y) in a width x height image: the
// plain tonemap's steps with the vignette before PBR Neutral, the grade (post/grade.hpp) after it, the
// palette mix after the OETF and the dither times (1 - grade.palette). `palette` is needed when
// grade.palette > 0. With a default Grade it equals tonemapReference.
// gradeDisplay is the same before quantisation: the encoded display value in [0, 1].
std::array<float, 3> gradeDisplay(const float rgb[3], float exposureEV, const Grade& grade,
                                  const GradePaletteTable* palette, std::uint32_t x, std::uint32_t y,
                                  std::uint32_t width, std::uint32_t height);
std::array<std::uint8_t, 3> tonemapGraded(const float rgb[3], float exposureEV, const Grade& grade,
                                          const GradePaletteTable* palette, std::uint32_t x, std::uint32_t y,
                                          std::uint32_t width, std::uint32_t height);
// The grade alone on a linear display colour in [0, 1] (the S-curve to the grain), and the grain's noise of a
// pixel in a frame (triangular, zero mean, unit standard deviation).
std::array<float, 3> gradeLinear(const std::array<float, 3>& display, const Grade& grade, std::uint32_t x,
                                 std::uint32_t y);
float grainNoise(std::uint32_t x, std::uint32_t y, std::uint32_t frame);
// The S-curve of grade.contrast on display luminance (see grade.hpp).
float sCurve(float x, float contrast);

} // namespace evo::post
