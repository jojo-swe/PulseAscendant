#pragma once

#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace evo::retro {

// PORTAL (chapter 10), the 1993 side while the camera dollies into the CRT: FINALE's grid runs on (its tiles'
// clocks continue from FINALE's, retro/finale.hpp) and switches off cell by cell on the beat, each cell
// collapsing like a tube switched off, to a bright line and a dot (portalCollapse); the centre goes last.
// Then, on the next bar line, the screen is black but for the prompt `THE SCREEN` typed at 30 characters a
// second in the middle, with the underscore cursor blinking on the beat: the single prompt the camera crosses
// into. The 1993 card is typed bottom-left as in FINALE. Once the card has left and the camera is on its way
// back out of the glass (two bars after D), the screen is the DOS listing STUDIO NIGHT opens on, cursor
// included, so the act join does not cut from the prompt to that listing. A grid effect at 35 retro frames
// a second.
//
// Times are the chapter's tracked beats (chapterBeat): the cells go in kPortalOrder on beats kPortalFirstOff
// to kPortalFirstOff + 8 (183.36 s to 186.73 s), the prompt starts typing on beat kPortalPromptBeat
// (187.16 s), and its last character lands before 187.85 s, where the camera's approach ends.
inline constexpr std::uint32_t kPortalRetroRate = 35;
inline constexpr std::array<int, 9> kPortalOrder = {0, 1, 2, 5, 8, 7, 6, 3, 4}; // round the edge, centre last
inline constexpr int kPortalFirstOff = 2;
inline constexpr int kPortalPromptBeat = 11;
inline constexpr const char* kPortalPromptText = "THE SCREEN";
inline constexpr std::uint32_t kPortalPromptRate = 30;
// The prompt's first cell: the text and its cursor centred.
inline constexpr int kPortalPromptX = (320 - 8 * 11) / 2;
inline constexpr int kPortalPromptY = (200 - 8) / 2;

// The retro frame a cell switches off on (the one its beat falls in), and the first of the prompt.
std::uint32_t portalOffFrame(const show::TimelineFrame& frame, int cell);
std::uint32_t portalPromptFrame(const show::TimelineFrame& frame);

// A cell `n` retro frames after it switched off: the rect it collapses into (centred in the cell) and the
// gain that keeps its light (the cell's area over the rect's, at most 64): a third of its height, 4 rows,
// a line of 2 rows (n = 2), then 1 row narrowing to 45 %, 12 % and a 2-pixel dot; nothing from n = 6 on.
struct Collapse {
    PixelRect rect;
    float gain;
};
inline constexpr std::uint32_t kPortalCollapseFrames = 6;
Collapse portalCollapse(const PixelRect& cell, std::uint32_t n);

// The prompt typed so far (the empty string before it starts).
std::string portalPrompt(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The first retro frame of the DOS listing: the later of the 1993 card leaving and the way out of the glass.
// The last retro frame the chapter can show (the one STUDIO NIGHT's first frame continues).
std::uint32_t portalListingFrame(const show::TimelineFrame& frame);
std::uint32_t portalLastRetroFrame(const show::TimelineFrame& frame);

// FINALE's palette until the listing, then BOOT's DOS colours (STUDIO NIGHT's).
void portalPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo parts of PORTAL: the palette above, PORTAL's own grid, scene (the card's cursor) and text
// (the centre's scroller while the centre is on, the prompt and its cursor, the card, then the DOS listing).
std::vector<GridLayer> portalGrid(const show::TimelineFrame& frame, std::uint32_t retroFrame);
void portalScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> portalText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
