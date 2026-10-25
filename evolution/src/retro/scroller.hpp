#pragma once

#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <array>
#include <cstdint>
#include <string>
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

// CREDITS (chapter 29): the 1993 sine scroller, full frame, over a slow three-layer starfield, with phosphor
// persistence. It starts from white (ASCENDANT resolves on white): the paper fades to black through the DAC
// over the chapter's first beat. The scroller carries, in order, kCreditsTitle, kCreditsLine and
// kCreditsGreetings, each entering at the right edge on a bar line of the chapter (kCreditsEntryBars, counted
// with chapterDownbeat from the chapter's start) and running left at kCreditsSpeed; then kCreditsEnd enters
// on the bar line of the chapter's downbeat D and eases in (a cubic Hermite from the scroller's speed to
// rest) to stop centred on the accent (688.02 s), where it holds. Every glyph rides y = kCreditsBaseY + a
// sin((x + phase) / kCreditsWavelength), phase = (time since the chapter's start) * kCreditsSpeed / 2; the
// amplitude a follows the bass, sampled once per retro frame (show::envelope, Bass), kCreditsAmplitude +
// kCreditsBassAmp
// * bass, and THE END?'s eases to 0 as it lands, so it holds still. A text and scene effect at 35 retro
// frames a second; nothing else is on the screen.
inline constexpr std::string_view kCreditsTitle = "EVOLUTION";
inline constexpr std::string_view kCreditsLine = "CODE: JOJO · MUSIC: JOYOLO · CONCEPT: JOHN";
inline constexpr std::string_view kCreditsGreetings =
    "GREETINGS TO  FUTURE CREW · TRITON · THE BLACK LOTUS · FARBRAUSCH · CONSPIRACY · "
    "ASD · MERCURY · FAIRLIGHT · HAUJOBB · LOGICOMA · CTRL-ALT-TEST · ANDROMEDA "
    "· MELON DEZIGN · SPACEBALLS · KEWLERS · MFX · STILL · ORANGE · ELUDE "
    "· COCOON · ALCATRAZ · APPROXIMATE";
inline constexpr std::string_view kCreditsEnd = "THE END?";
inline constexpr std::array<int, 3> kCreditsEntryBars = {0, 1, 3};
inline constexpr float kCreditsBaseY = 96;
inline constexpr double kCreditsSpeed = 160;   // pixels per second
inline constexpr double kCreditsAmplitude = 6; // pixels, with no bass
inline constexpr double kCreditsBassAmp = 18;  // pixels more at full bass
inline constexpr double kCreditsWavelength = 60;
// Where THE END? stops: centred.
inline constexpr double kCreditsEndX = (320 - 8.0 * 8) / 2;

// CREDITS' palette indices: black, the paper (white fading to black), the stars' three greys and the white
// of the text.
inline constexpr std::uint8_t kCreditsBlack = 0;
inline constexpr std::uint8_t kCreditsPaper = 1;
inline constexpr std::uint8_t kCreditsStar = 2; // 2, 3, 4: far to near
inline constexpr std::uint8_t kCreditsWhite = 15;

// The glyphs of a UTF-8 string, one string per code point (each one 8x8 glyph).
std::vector<std::string> creditGlyphs(std::string_view utf8);
// When a credit's first glyph enters at the right edge, in chapter seconds: kCreditsTitle, kCreditsLine,
// kCreditsGreetings, kCreditsEnd (index 3).
double creditEntry(const show::TimelineFrame& frame, int credit);
// The x of THE END?'s first glyph at a chapter time (unrounded).
double creditsEndX(const show::TimelineFrame& frame, double seconds);
// The sine's amplitude at a retro frame, before THE END?'s settling.
double creditsAmplitude(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo parts of CREDITS (see retro/effects.hpp).
void creditsPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::uint8_t creditsBackground(const show::TimelineFrame& frame, std::uint32_t retroFrame);
void creditsScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> creditsText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// FLYBY's 1993 side (retro/flyby.hpp) scrolls this greeting along the bottom of the frame, below the ship
// (whose lowest vertex stays above y = 136 px), so it only crosses stars: white (index 255 of FLYBY's
// palette, which only the greeting uses) with a black drop shadow. Its lowest row, shadow included, is 186,
// above the epoch card the HUD draws bottom-left (from about row 190 in a 16:9 output).
inline constexpr std::string_view kFlybyText = "WE WERE THERE IN 1993 ...";
inline constexpr float kFlybyBaseY = 172;
inline constexpr float kFlybySpeed = 60;
inline constexpr float kFlybyAmplitude = 6;
inline constexpr float kFlybyWavelength = 30;
inline constexpr std::uint8_t kFlybyIndex = 255;

std::vector<text::Item> flybyText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
