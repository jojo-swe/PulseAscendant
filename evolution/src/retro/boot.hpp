#pragma once

#include "retro/palette.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace evo::retro {

// Chapter 0, BOOT: a 1993 DOS prompt types its way into the demo. A text-only effect: the retro stage clears
// the index image to bootBackground() and draws bootText() over it with the 8x8 font, at 35 retro frames per
// second, so everything below is a function of the retro frame (and the chapter's fixed duration and
// accent), never of the exact time within it.
//
// The classic DOS look: 8x8 glyphs at scale 1 in the 320x200 index image, light grey on black, a 4-pixel
// margin at the left and the top, 8-pixel lines; a line longer than the 39 columns that fit wraps at a word
// as a 40-column screen would show it. The cursor is an underscore on row 7 of the cell after the last
// character written, blinking at 2 Hz.
inline constexpr std::uint32_t kBootRetroRate = 35;

// The palette indices BOOT uses: the DOS text colours.
inline constexpr std::uint8_t kBootBlack = 0;
inline constexpr std::uint8_t kBootGrey = 7;     // #AAAAAA, the text and the cursor
inline constexpr std::uint8_t kBootDarkGrey = 8; // #555555
inline constexpr std::uint8_t kBootWhite = 15;   // the flash

inline constexpr int kBootMargin = 4;           // pixels left of the first column and above the first line
inline constexpr int kBootLineHeight = 8;       // pixels from one line to the next
inline constexpr std::size_t kBootColumns = 39; // whole 8-pixel cells right of the margin in 320 pixels

// The first of the two white retro frames that cut from the prompt to the next chapter. See boot.cpp.
std::uint32_t bootFlashFrame(const show::TimelineFrame& frame);

// The screen as text at retro frame `retroFrame`: every line written so far, wrapped, without the
// cursor; empty from the flash on.
std::vector<std::string> bootScreen(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo parts of BOOT (see retro/effects.hpp).
void bootPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::uint8_t bootBackground(const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> bootText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
