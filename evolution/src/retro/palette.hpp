#pragma once

#include "show/timeline.hpp"

#include <array>
#include <cstdint>

namespace evo::retro {

// A 256-colour VGA palette: entry i is the colour of index i as RGBA8 with red in the low byte (the layout
// unpackUnorm4x8 reads). Index 0 is always black.
using Palette = std::array<std::uint32_t, 256>;

// Builds the palette of one retro frame on the CPU. Cycling is done here, by retroFrame, never by a shader;
// the retro stage calls the builder only when it re-renders the retro frame.
using PaletteBuilder = void (*)(Palette&, const show::TimelineFrame&, std::uint32_t retroFrame);

// An opaque RGBA8 colour in the palette layout.
constexpr std::uint32_t rgba(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return 0xff000000u | std::uint32_t{blue} << 16 | std::uint32_t{green} << 8 | red;
}

// A grey ramp: index 0 black ... 255 white. The palette of the test pattern.
void grayPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
