#pragma once

#include "retro/boot.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace evo::retro {

// The Studio's CRT while no 1993 effect runs (STUDIO NIGHT and RETURN, Decision 3 of step 4): the demo has
// exited to DOS and the PC sits at the prompt. BOOT's look (8x8 glyphs in grey on black, the same margin and
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

} // namespace evo::retro
