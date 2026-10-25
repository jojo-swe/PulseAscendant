#pragma once

#include <array>
#include <cstdint>

namespace evo::post {

// The display grade the tonemap applies after PBR Neutral, in linear display light before the OETF
// (tonemap.comp, tonemapGraded in tonemap_reference.hpp). post::Look fills it per frame from the act; the
// default grades nothing. In order:
//   contrast            the S-curve on luminance: slope 1 + contrast at mid grey (0.18), 1 - contrast at 0 and
//                       1, colour scaled with its luminance (hue kept);
//   lift                black lifted to this colour: c = lift + c (1 - lift) per channel;
//   splitTone           c *= 1 + s (1 - 2 Y) (1, -0.196, -1): warm shadows, cool highlights, no luminance
//                       change on its own;
//   shadowDesaturation  saturation lowered by d (1 - min(2 Y, 1))^2: the full d at black, none from Y 0.5;
//   grain               c *= 1 + g n, n triangular with unit standard deviation, hashed from (pixel,
//                       grainFrame);
// then, before quantisation, palette mixes the sRGB-encoded colour toward its nearest palette entry in
// Oklab, and the dither shrinks by (1 - palette) so a weight of 1 gives the entry's bytes exactly. vignette
// (applied in HDR light, before PBR Neutral) scales the light by 1 - v r^2, r = 1 in the corners.
struct Grade {
    float contrast = 0;
    std::array<float, 3> lift{0.f, 0.f, 0.f};
    float splitTone = 0;
    float shadowDesaturation = 0;
    float grain = 0;
    std::uint32_t grainFrame = 0; // round(showSeconds * 60)
    float vignette = 0;
    float palette = 0;
};

// 256 colours in retro::Palette's layout: RGBA8 with red in the low byte, sRGB-encoded.
using GradePalette = std::array<std::uint32_t, 256>;

// The palette as the tonemap's buffer holds it (std430): each entry's Oklab (from its decoded linear colour,
// w = 0), then the colours themselves.
struct GradePaletteTable {
    std::array<std::array<float, 4>, 256> lab;
    std::array<std::uint32_t, 256> color;
};
static_assert(sizeof(GradePaletteTable) == 256 * 16 + 256 * 4);
GradePaletteTable gradePaletteTable(const GradePalette& palette);

// Linear sRGB -> Oklab (Ottosson), as tonemap.comp computes it.
std::array<float, 3> oklab(const std::array<float, 3>& linear);

} // namespace evo::post
