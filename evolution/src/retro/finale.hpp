#pragma once

#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/cues.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace evo::retro {

// FINALE (chapter 9): every 1993 effect at once on the Studio's CRT. A 3x3 grid (EffectInfo::grid) of FLYBY,
// DOTS, PLASMA / TUNNEL, the centre, GLENZ / LENS, RAYTRACE, CITY: each tile is that effect's own picture
// (its shader, scene and palette, without its text) at a frame of its home chapter, box-downsampled 3:1 from
// kFinaleSource into a 106 x 66 cell; 1-pixel black gutters part the cells. The centre is FINALE's own
// picture: `EVOLUTION` riding a sine through the cell in a warm colour-cycled ramp. The 1993 card is typed
// bottom-left over the grid (retro/boot.hpp, cardText) on a black cell background, the way DOS draws text.
//
// Each tile runs from a moment that shows its effect at its best (finaleHomeStart) and advances with the
// finale at its own rate: n retro frames of FINALE (35 a second) into the chapter are floor(n * rate / 35)
// home frames on, wrapping back to the start at the end of the home chapter. PORTAL continues the count
// (finaleElapsed), so the grid runs on without a seam across the chapter join. Everything is a pure function
// of (chapter, retro frame).
inline constexpr std::uint32_t kFinaleRetroRate = 35;
inline constexpr std::array<show::Effect, 8> kFinaleTiles = {
    show::Effect::Flyby, show::Effect::Dots, show::Effect::Plasma,   show::Effect::Tunnel,
    show::Effect::Glenz, show::Effect::Lens, show::Effect::Raytrace, show::Effect::City};
inline constexpr int kFinaleCellWidth = 106;
inline constexpr int kFinaleCellHeight = 66;
inline constexpr int kFinaleCentre = 4; // the centre cell, in reading order 0..8
// The part of a full 320x200 picture a tile shows: three source pixels to a tile pixel each way.
inline constexpr PixelRect kFinaleSource = {1, 1, 3 * kFinaleCellWidth, 3 * kFinaleCellHeight};

// FINALE's own colours: black, kFinaleWhite for the card and kFinaleRamp.. + 7 the scroller's warm ramp.
inline constexpr std::uint8_t kFinaleWhite = 15;
inline constexpr std::uint8_t kFinaleRamp = 16;
inline constexpr int kFinaleRampSize = 8;
inline constexpr const char* kFinaleScrollText = "EVOLUTION   ";
inline constexpr double kFinaleScrollSpeed = 40; // pixels a second

// Cell `cell` (0..8, reading order) of the grid.
PixelRect finaleCell(int cell);
// The cell of tile `tile` (an index into kFinaleTiles): the cells in reading order, skipping the centre.
int finaleTileCell(std::size_t tile);
// FINALE's retro frames since its first, for a frame of FINALE or PORTAL (which continues the count).
std::uint32_t finaleElapsed(const show::TimelineFrame& frame, std::uint32_t retroFrame);
// What tile `effect` shows `elapsed` FINALE frames in: its entry at its home chapter's frame and home retro
// frame.
EffectFrame finaleTileSource(show::Effect effect, std::uint32_t elapsed);

// A tile's home chapter, and the frame it gets there at a home retro frame: the timeline in the middle of
// that retro frame (clamped into the chapter).
int finaleHomeChapter(show::Effect effect);
show::TimelineFrame finaleHomeFrame(show::Effect effect, std::uint32_t homeRetroFrame);
// Where a tile starts in its home chapter, in home retro frames: FLYBY at 60 % (the side view), DOTS just
// before the year arrives, PLASMA at its accent, TUNNEL once the solid tube is whole, GLENZ on its accent,
// LENS at kFinaleLensStartSeconds (the lens wholly on the screen, high, at the right end of its run, where
// rotozoom.hpp's lens(t) has tri = 1: t = 6.2 / 2), RAYTRACE halfway (mid-orbit), CITY at 40 %.
inline constexpr double kFinaleLensStartSeconds = 3.1;
std::uint32_t finaleHomeStart(show::Effect effect);

// The scroller's glyphs in the centre cell `elapsed` FINALE frames in (FINALE's own text without the card).
std::vector<text::Item> finaleScroller(std::uint32_t elapsed);

// The EffectInfo parts of FINALE. The grid: the eight tiles, FINALE's own picture in the centre cell, and in
// the card's rect while the card shows.
std::vector<GridLayer> finaleGrid(const show::TimelineFrame& frame, std::uint32_t retroFrame);
void finalePalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
void finaleScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> finaleText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
