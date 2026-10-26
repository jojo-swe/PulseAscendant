#pragma once

#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstdint>
#include <vector>

namespace evo::retro {

// THE END (chapter 30), the show's last screen and its last image: a tube switching off. kTheEndCaption in
// the 8x8 font at scale 1, centred with its cursor, typed at 30 characters a second from the chapter's first
// bar line, the underscore cursor blinking on the beat. On the accent (two bars and a second before the end
// at the latest, so the black comes before it) the CRT powers off:
//   the retro frame the accent falls in (A): the picture squeezed to a band of 6 rows across the text;
//   A + 1: 2 rows; A + 2: a 2-row line;
//   A + 3 .. A + 5: one row narrowing to 45 % and 12 % of the text's width, then a 2 x 2 dot;
//   from A + 6: the dot's afterglow, fading through the DAC to black over the chapter's two bars
//   (show::transitionMarks) from A + 5; black from then to the end of the show.
// All in white; the light fades through theEndLevel. Phosphor persistence. A text and
// scene effect at 35 retro frames a second.
inline constexpr std::uint32_t kTheEndRetroRate = 35;
inline constexpr const char* kTheEndCaption = "REALITY IS WHAT YOU RENDER";
inline constexpr std::uint8_t kTheEndText = 15;  // the caption and the cursor
inline constexpr std::uint8_t kTheEndLight = 16; // the collapsing picture and the dot
inline constexpr std::uint32_t kTheEndTypeRate = 30;
// The caption's first cell: the caption and its cursor centred.
inline constexpr int kTheEndX = (320 - 8 * 27) / 2;
inline constexpr int kTheEndY = (200 - 8) / 2;

// The retro frame of the power-off (the one the accent falls in), the first of the dot, and the first black
// one after its afterglow.
std::uint32_t theEndOffFrame(const show::TimelineFrame& frame);
std::uint32_t theEndDotFrame(const show::TimelineFrame& frame);
std::uint32_t theEndBlackFrame(const show::TimelineFrame& frame);
// The caption typed so far (before the power-off).
std::string theEndTyped(const show::TimelineFrame& frame, std::uint32_t retroFrame);
// The light's grey (0..255, DAC steps) at a retro frame: white while it collapses, then the dot's afterglow,
// 0 from theEndBlackFrame on.
std::uint8_t theEndLevel(const show::TimelineFrame& frame, std::uint32_t retroFrame);
// The rect of the collapsing light at a retro frame (width 0 before the power-off and once black).
struct TheEndRect {
    int x, y, width, height;
};
TheEndRect theEndLight(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo parts of THE END (see retro/effects.hpp).
void theEndPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
void theEndScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> theEndText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
