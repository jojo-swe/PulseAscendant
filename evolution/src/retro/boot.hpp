#pragma once

#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace evo::retro {

// Chapter 0, BOOT: a 1993 PC powers on and a DOS prompt types its way into the demo. A text-only effect with
// phosphor persistence: the retro stage clears the index image to bootBackground() and draws bootText() over
// it with the 8x8 font, at 35 retro frames per second, so everything below is a function of the retro frame
// and the chapter's fixed facts (its duration, accent and tracked beats), never of the exact time within it.
//
// The classic DOS look: 8x8 glyphs at scale 1 in the 320x200 index image, light grey on black, a 4-pixel
// margin at the left and the top, 8-pixel lines. A line longer than the 39 columns right of the margin wraps
// at a word, as a 40-column screen shows it. The cursor is an underscore on row 7 of the cell after the last
// character written, on for the first half of every tracked beat (bootCursorOn).
//
// The script, on the chapter's tracked beats (chapterBeat; b0 = 0.41 s, b2 = 1.26, b4 = 2.11, b6 = 2.96):
//   b0          the BIOS line (kBootBiosLine) and a blank row;
//   b2          `C:\DEMO>`;
//   b4 .. b6    `EVOLUTION.EXE`, one key every (b6 - b4) / 12, the first on b4 and the last on b6;
//   accent      Enter (3.05 s);
//   b7          `Sound Blaster 16 at 220h IRQ 5 DMA 1`, its ` ...` typed at 30 characters a second, ` found`
//   on b8; b9          `Ray tracing hardware`, its dots typed at 30 a second, ` not yet` on b11; b12
//   kBootReservedRows empty rows: the slot of one more detection line of the same shape, so a
//               line added there leaves every other row where it is;
//   b15         `LOADING` and its three dots at 30 a second;
// then the flash (bootFlashFrame) cuts to FLYBY. Every write shows from the retro frame its beat falls in.
inline constexpr std::uint32_t kBootRetroRate = 35;

// The palette indices BOOT uses: the DOS text colours.
inline constexpr std::uint8_t kBootBlack = 0;
inline constexpr std::uint8_t kBootGrey = 7;     // #AAAAAA, the text and the cursor
inline constexpr std::uint8_t kBootDarkGrey = 8; // #555555
inline constexpr std::uint8_t kBootWhite = 15;   // the flash

inline constexpr int kBootMargin = 4;           // pixels left of the first column and above the first line
inline constexpr int kBootLineHeight = 8;       // pixels from one line to the next
inline constexpr std::size_t kBootColumns = 39; // whole 8-pixel cells right of the margin in 320 pixels

inline constexpr const char* kBootBiosLine = "486DX2-66   Memory Test: 640K OK";
inline constexpr const char* kBootPrompt = "C:\\DEMO>";
inline constexpr const char* kBootCommand = "EVOLUTION.EXE";
// The detection lines, each a label, a dot leader typed at kBootDetectRate and a result, exactly these.
inline constexpr const char* kBootSoundCard = "Sound Blaster 16 at 220h IRQ 5 DMA 1 ... found";
inline constexpr const char* kBootRayTracing = "Ray tracing hardware .................. not yet";
inline constexpr std::uint32_t kBootDetectRate = 30;
inline constexpr std::size_t kBootReservedRows = 2;

// The first of the two white retro frames that cut from the prompt to the next chapter. See boot.cpp.
std::uint32_t bootFlashFrame(const show::TimelineFrame& frame);

// The screen as text at retro frame `retroFrame`: every line written so far, wrapped, without the
// cursor; empty from the flash on.
std::vector<std::string> bootScreen(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo parts of BOOT (see retro/effects.hpp).
void bootPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::uint8_t bootBackground(const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> bootText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The chapter's tracked beats: beat i is the i-th beat of its track at or after the chapter's start, in
// chapter seconds (the chapter's fixed facts, as GLENZ's bounce reads them). A beat past the track's last is
// clamped to it.
double chapterBeat(const show::TimelineFrame& frame, int beat);
// The chapter's i-th bar line (a beat that is a bar's downbeat) at or after its start, in chapter seconds.
double chapterDownbeat(const show::TimelineFrame& frame, int bar);
// The start of the retro frame (at `rate`) that a chapter time falls in, so a write timed there shows from
// that very frame.
double frameStart(double seconds, std::uint32_t rate);
// Whether a cursor blinking on the beat is on at a retro frame: on in the first half of the tracked beat that
// holds the retro frame's centre, off before the chapter's first beat.
bool beatCursorOn(const show::TimelineFrame& frame, std::uint32_t retroFrame, std::uint32_t rate);

// The prompt's typing engine, which BOOT, RETURN, REBOOT and PORTAL share. A script is a list of writes in
// time order: at `at` seconds into the chapter, `text` appears all at once (charactersPerSecond 0) or one
// character after another, the first 1 / charactersPerSecond after `at`. promptWritten is everything written
// up to and including retro frame `retroFrame` (at kBootRetroRate), newlines included. promptCursorOn is the
// DOS prompt's own 2 Hz blink, on in the first quarter of every half second, judged at the centre of the
// retro frame.
struct PromptWrite {
    double at;
    std::string_view text;
    std::uint32_t charactersPerSecond;
};
std::string promptWritten(std::span<const PromptWrite> script, std::uint32_t retroFrame);
bool promptCursorOn(std::uint32_t retroFrame);
// BOOT's script for this chapter (the times read its beats).
std::vector<PromptWrite> bootScript(const show::TimelineFrame& frame);

// The screen rows of a text: a row per line, a line longer than kBootColumns wrapped after its last space
// that fits (the space dropped), or hard at kBootColumns when it has none. The last row is where the cursor
// is, so a text ending in a newline ends in an empty row.
std::vector<std::string> promptRows(std::string_view text);
// The prompt's text items: every row from the margin in `index`, and the cursor "_" after the last while
// `cursor`.
std::vector<text::Item> promptItems(const std::vector<std::string>& rows, bool cursor,
                                    std::uint8_t index = kBootGrey);

// The 1993 epoch card inside the retro picture, for the chapters whose 1993 picture is on the Studio's CRT
// (FINALE, PORTAL, REBOOT): the chapter's thenCard (show::cues) in the 8x8 font, bottom-left (kCardX,
// kCardY), typed at kCardRate characters a second from one bar after the chapter's start
// (show::transitionMarks) and quantised to the retro frame, with a block cursor blinking on the beat after
// the last character; held until one bar after the accent, but gone two bars before the chapter's end at the
// latest. cardShown is how many characters (code points) show, or -1 while the card is hidden.
inline constexpr std::uint32_t kCardRate = 30;
inline constexpr int kCardX = kBootMargin;
inline constexpr int kCardY = 200 - kBootMargin - 8;
int cardShown(const show::TimelineFrame& frame, std::uint32_t retroFrame, std::uint32_t rate);
// The card's text item (none while hidden or before its first character).
std::vector<text::Item> cardText(const show::TimelineFrame& frame, std::uint32_t retroFrame,
                                 std::uint32_t rate, std::uint8_t index);
// The rect the card covers: its cells and the cursor's (width 0 while hidden).
PixelRect cardRect(const show::TimelineFrame& frame, std::uint32_t retroFrame, std::uint32_t rate);
// Appends the card's block cursor (a full cell in `index`) while it is on.
void cardCursor(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame,
                std::uint32_t rate, std::uint8_t index);
// Appends a flat screen-space rectangle [x0, x1) x [y0, y1) as two triangles (nothing when empty); depth 0.
void addRect(RetroScene& scene, int x0, int y0, int x1, int y1, std::uint8_t index);

// Chapter 20, REBOOT: the Studio's CRT boots 1993 again. It opens on RETURN's last screen (the BIOS line and
// `C:\DEMO>EVOLUTION.EXE /2026`) as the Enter lands on the track's first frame, then on its beats:
// `Reinitialising reality ...` (b0), `Ray tracing hardware .................. found` (b4, the dots at 30 a
// second, the answer on b6) and `LOADING...` (b8). From b14 (419.35 s) the text gives way to DOTS' 512-point
// cube, spinning on black as the first effect of the re-run, until the portal takes the camera through the
// glass. The 1993 card is typed bottom-left throughout. Phosphor persistence; BOOT's DOS colours, with DOTS'
// shade ramp from kDotsRamp. rebootScreen is the screen as text, as bootScreen (empty once the cube shows).
inline constexpr const char* kRebootCommand = "EVOLUTION.EXE /2026";
inline constexpr std::uint8_t kRebootCard = 15;
std::vector<std::string> rebootScreen(const show::TimelineFrame& frame, std::uint32_t retroFrame);
// The first retro frame of the cube.
std::uint32_t rebootCubeFrame(const show::TimelineFrame& frame);
void rebootPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
void rebootScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> rebootText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
