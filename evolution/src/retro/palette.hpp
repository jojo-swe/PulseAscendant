#pragma once

#include "show/timeline.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace evo::retro {

// A 256-colour VGA palette: entry i is the colour of index i as RGBA8 with red in the low byte (the layout
// unpackUnorm4x8 reads). Index 0 is always black.
using Palette = std::array<std::uint32_t, 256>;

// Builds the palette of one retro frame on the CPU. Cycling is done here, by retroFrame, never by a shader;
// the retro stage calls the builder only when it re-renders the retro frame, so a builder must depend only on
// (chapter, retroFrame) and the chapter's fixed facts (chapterDuration, accentSeconds), never on
// localSeconds, showSeconds or the music signals of the frame. The text and background providers of
// retro/effects.hpp keep the same contract.
using PaletteBuilder = void (*)(Palette&, const show::TimelineFrame&, std::uint32_t retroFrame);

// An opaque RGBA8 colour in the palette layout.
constexpr std::uint32_t rgba(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return 0xff000000u | std::uint32_t{blue} << 16 | std::uint32_t{green} << 8 | red;
}

// A grey ramp: index 0 black ... 255 white. The palette of the test pattern.
void grayPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);

// PLASMA's palette layout: index 0 black, 1..31 a fixed shadow ramp (black to the darkest violet) for
// anything drawn over the plasma, and the 224 entries from kPlasmaFirstIndex up the colour ring the plasma
// shader indexes (shaders/retro/plasma.comp).
inline constexpr std::size_t kPlasmaFirstIndex = 32;
inline constexpr std::size_t kPlasmaColours = 256 - kPlasmaFirstIndex;

// PLASMA's palette (palettes.cpp): the colour ring is three smooth gradients, deep violet -> magenta ->
// orange, orange -> yellow -> cyan and cyan -> deep blue -> deep violet, closed so its last entry runs into
// its first. It cycles: entry kPlasmaFirstIndex + j shows ring colour (j + retroFrame) % 224, so the colours
// flow one index per retro frame and come round every 224 retro frames (6.4 s at 35 Hz).
void plasmaPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
