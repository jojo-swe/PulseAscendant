#pragma once

// How the shared path tracer is built (modern::PathTracer), and the work counters of its counting build: a
// header of its own so that the render stage can name them without the tracer's.

#include <array>
#include <cstdint>

namespace evo::modern {

// The work counters of the counting build (shaders/rt/counters.glsl, TracerOptions::counters): totals per
// kind over the frames traced, in the order of kCounterNames.
inline constexpr std::uint32_t kCounterCount = 21;
inline constexpr std::array<const char*, kCounterCount> kCounterNames = {
    "paths",        "primaryRays",    "bounceRays",     "continuationRays", "heroRays",
    "shadowRays",   "paneRays",       "guideRays",      "lensSegments",     "surfaceHits",
    "surfaceNee",   "mediumNee",      "mediumScatter",  "inScatterPoints",  "gridWalks",
    "gridCells",    "gridLookups",    "trackingSteps",  "rectImportance",   "treeNodes",
    "treeWalks"};
using Counters = std::array<std::uint64_t, kCounterCount>;

// How a tracer is built: `counters` loads the counting raygen (rt_pathtrace_counters.rgen.spv), which tallies
// its work into a per-slot buffer that takeCounters() reads (a diagnostic: slower, the same picture).
struct TracerOptions {
    bool counters = false;
};

} // namespace evo::modern
