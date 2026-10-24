#pragma once

#include <array>
#include <cstdint>

namespace evo::post {

// The tonemap on the CPU, the mirror shaders/post/tonemap.comp is tested against (Review Focus 6). For linear
// Rec. 709 light `rgb` (a non-finite component counts as 0):
//   hdr * 2^exposureEV -> ACES fit (Hill: sRGB -> AP1 input matrix, max(0) in AP1, RRT + ODT rational fit,
//   output matrix) -> clamp [0, 1] -> sRGB OETF -> round(v * 255 + d), d a triangular dither in (-0.5, 0.5)
//   from pcg(x, y), clamped to [0, 255]. Black stays 0 and white 255; the dither is static per pixel (no
//   temporal shimmer) and the same for the three channels (greys stay grey).

// The display value in [0, 1] before quantisation: everything above except the dither and the rounding.
std::array<float, 3> tonemapDisplay(const float rgb[3], float exposureEV);
// The sRGB OETF (linear -> encoded) for v in [0, 1]; exactly 1 at 1.
float srgbOetf(float v);
// The dither of pixel (x, y), strictly inside (-0.5, 0.5).
float tonemapDither(std::uint32_t x, std::uint32_t y);
// The byte the tonemap writes for pixel (x, y).
std::array<std::uint8_t, 3> tonemapReference(const float rgb[3], float exposureEV, std::uint32_t x,
                                             std::uint32_t y);

} // namespace evo::post
