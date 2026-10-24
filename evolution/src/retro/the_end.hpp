#pragma once

#include "retro/palette.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstdint>
#include <vector>

namespace evo::retro {

// THE END (chapter 30), the show's last screen: `THE END` in the 8x8 font at scale 3, centred, over black,
// fading from white to black through the palette as a VGA demo did it (the DAC's 64 levels), then black for
// the last kTheEndBlackSeconds. A text-only effect at 35 retro frames a second.
inline constexpr std::uint32_t kTheEndRetroRate = 35;
inline constexpr const char* kTheEndCaption = "THE END";
inline constexpr std::uint8_t kTheEndText = 15; // the text's palette index
inline constexpr float kTheEndScale = 3;
inline constexpr double kTheEndBlackSeconds = 2;

// The first black retro frame, floor((chapterDuration - kTheEndBlackSeconds) * 35) = `end`. Before it the
// text's DAC level falls linearly from 63 (white) at frame 0, ceil(63 (end - retroFrame) / end), to 1.
std::uint32_t theEndFadeFrames(const show::TimelineFrame& frame);
// The text's colour at retroFrame: grey of DAC level 0..63, as 8 bits (round(level * 255 / 63)).
std::uint8_t theEndLevel(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo parts of THE END (see retro/effects.hpp); the text is empty once the fade is black.
void theEndPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> theEndText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
