#pragma once

#include "retro/palette.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace evo::retro {

// The sine scroller: a line of 8x8 glyphs that enters at the right edge of the 320x200 index image, runs left
// and rides a sine wave. It is a function of the retro frame alone (t = retroFrame / kScrollerRetroRate),
// never of the exact time within a retro frame, so the retro stage may cache what it draws
// (retro/effects.hpp).
inline constexpr std::uint32_t kScrollerRetroRate = 35;

// The palette index the scroller writes unless told otherwise: white in CREDITS' palette.
inline constexpr std::uint8_t kScrollerIndex = 15;
// The palette index of the scroller's optional drop shadow: black in every effect's palette.
inline constexpr std::uint8_t kScrollerShadowIndex = 0;

// The glyphs of `text` (ASCII, one byte a glyph) at time t = retroFrame / 35 s, each its own one-character
// text::Item at scale 1. With s = speed * t, glyph i has its top-left corner at
//   x = 320 - s + 8 * i,
//   y = baseY + amplitude * sin((x + phase) / wavelength),   phase = s / 2,
// both rounded to whole pixels: at t = 0 the first glyph stands just right of the image. The text loops: once
// its full width has passed (its last glyph has left at the left edge, s = 320 + 8 * size) it enters at the
// right edge again, as s runs modulo 320 + 8 * size. Glyphs wholly outside the 320x200 image and blanks
// (which draw nothing) are left out. The low byte of every item's rgba is `index`. wavelength must be
// positive. With `shadow`, every glyph also gets a drop shadow, the same glyph in kScrollerShadowIndex one
// pixel right and down: all the shadows come first, then the glyphs in the same order, so the text is drawn
// over every shadow (two dispatches of the text pass).
std::vector<text::Item> scrollerItems(std::string_view text, std::uint32_t retroFrame, float speedPxPerSec,
                                      float amplitude, float wavelength, float baseY,
                                      std::uint8_t index = kScrollerIndex, bool shadow = false);

// CREDITS (chapter 29): a 1993 sine scroller on deep blue under a static EVOLUTION title. A text-only
// effect at 35 retro frames per second.
inline constexpr std::string_view kCreditsText =
    "   EVOLUTION  ...  CODE: CLAUDE  ...  MUSIC: JOYOLO (SUNO)  ...  CONCEPT: JOHN  ...  GREETINGS TO "
    "FUTURE CREW AND EVERYONE WHO WAS THERE IN 1993  ...  REALITY IS WHAT YOU RENDER  ...";
inline constexpr std::string_view kCreditsTitle = "EVOLUTION";
inline constexpr float kCreditsTitleY = 24;
inline constexpr float kCreditsBaseY = 100;
inline constexpr float kCreditsSpeed = 60;     // pixels per second
inline constexpr float kCreditsAmplitude = 24; // pixels
inline constexpr float kCreditsWavelength = 40;

// CREDITS' palette indices: black, the deep blue it is cleared to, and the white of its text.
inline constexpr std::uint8_t kCreditsBlack = 0;
inline constexpr std::uint8_t kCreditsBlue = 1; // #000040
inline constexpr std::uint8_t kCreditsWhite = 15;

// The EffectInfo parts of CREDITS (see retro/effects.hpp).
void creditsPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::uint8_t creditsBackground(const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> creditsText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// FLYBY's 1993 side keeps the test pattern (step 3 brings the flat-shaded ship) and scrolls this greeting
// low over it, in white (index 255 of the grey ramp) with a black drop shadow, so it reads over the light
// bands.
inline constexpr std::string_view kFlybyText = "WE WERE THERE IN 1993 ...";
inline constexpr float kFlybyBaseY = 180;
inline constexpr float kFlybySpeed = 60;
inline constexpr float kFlybyAmplitude = 6;
inline constexpr float kFlybyWavelength = 30;
inline constexpr std::uint8_t kFlybyIndex = 255;

std::vector<text::Item> flybyText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
