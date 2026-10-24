#pragma once

#include "app/options.hpp"
#include "show/timeline.hpp"

namespace evo {

// Headless: renders one frame at t = options.time and writes it to <captureDir>/frame-<time>.png, the time
// with 3 decimals and at least 3 integer digits (1.5 s -> frame-001.500.png). Creates missing directories
// and overwrites an existing file (captures are diagnostics). Prints the written path to stdout. Returns
// the process exit code. The frame goes through applyForcedEra.
int runCapture(const Options& options);

// Diagnostic and test hook for the headless modes (--capture-dir and --render-stream): when the environment
// variable EVO_FORCE_ERA is 0 or 1, holds the frame in 1993 or 2026 (the era, a wipe not started or
// finished, no cut flash), so either era can be rendered at any show time. Unset or any other value leaves
// the frame as the show has it; the window never calls this. Returns whether the frame was forced.
bool applyForcedEra(show::TimelineFrame& frame);

} // namespace evo
