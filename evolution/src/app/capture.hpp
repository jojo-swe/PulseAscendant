#pragma once

#include "app/options.hpp"

namespace evo {

// Headless: renders one frame at t = options.time and writes it to <captureDir>/frame-<time>.png, the time
// with 3 decimals and at least 3 integer digits (1.5 s -> frame-001.500.png). Creates missing directories
// and overwrites an existing file (captures are diagnostics). Prints the written path to stdout. Returns
// the process exit code. The environment variable EVO_FORCE_ERA=0 or 1 (diagnostic, capture only) forces the
// frame into 1993 or 2026; any other value is ignored.
int runCapture(const Options& options);

} // namespace evo
