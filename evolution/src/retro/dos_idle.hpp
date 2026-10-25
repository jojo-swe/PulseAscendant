#pragma once

#include "retro/boot.hpp"
#include "retro/palette.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace evo::retro {

// The Studio's CRT while no 1993 effect runs (STUDIO NIGHT): the demo has exited to DOS and the PC sits at
// the prompt. BOOT's look (8x8 glyphs in grey on black, the same margin and
// palette): a directory listing of C:\DEMO and, on the last line, `C:\DEMO>` with BOOT's cursor blinking at
// 2 Hz after it. Text only, at BOOT's rate, and a pure function of the retro frame (nothing else changes).
inline constexpr std::uint32_t kDosIdleRetroRate = kBootRetroRate;

// The prompt the screen ends on; the cursor follows it.
inline constexpr const char* kDosIdlePrompt = "C:\\DEMO>";

// The screen as text, one string per row, without the cursor: the listing, then kDosIdlePrompt last.
std::vector<std::string> dosIdleScreen(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo text provider (with bootPalette, on index 0 black): the rows in grey, and the cursor "_" in
// the cell after the prompt while promptCursorOn(retroFrame).
std::vector<text::Item> dosIdleText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// Chapter 19, RETURN: the CRT is dark until the accent (401.69 s), when it switches on with a one-beat flash
// that fades through the DAC (kReturnFlash, from white to black, (1 - u)^2 over the beat) and leaves the POST
// screen: BOOT's BIOS line on the next beat, `C:\DEMO>` on the bar line after it, the cursor blinking on the
// beat. Over the chapter's last four bars `EVOLUTION.EXE /2026` is typed on the beat: `EVOLUTION.EXE` one key
// a beat from the bar line 16 beats before the chapter's last beat, then ` /2026` one key every half beat,
// its last key on that last beat (412.38 s, the retro frame before the join), so REBOOT opens on the finished
// line. Phosphor persistence; BOOT's palette plus the flash.
inline constexpr std::uint8_t kReturnFlash = 16;
// The retro frame of the flash (the one the accent falls in) and of the last key.
std::uint32_t returnFlashFrame(const show::TimelineFrame& frame);
std::uint32_t returnLastKeyFrame(const show::TimelineFrame& frame);
// The screen as text (empty while dark), as bootScreen.
std::vector<std::string> returnScreen(const show::TimelineFrame& frame, std::uint32_t retroFrame);
void returnPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::uint8_t returnBackground(const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> returnText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
