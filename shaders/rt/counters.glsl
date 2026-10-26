#ifndef EVO_RT_COUNTERS_GLSL
#define EVO_RT_COUNTERS_GLSL

// Work counters for --bench-counters (docs/PERFORMANCE.md): how many rays of each kind, path vertices,
// light-selection evaluations and medium steps the path tracer spends. Only the counting build of the raygen
// (rt_pathtrace_counters.rgen.spv, -DEVO_COUNTERS) counts: there EVO_COUNT(k, n) adds n to the invocation's
// own tally, which main() adds to the frame's 64-bit totals (EVO_BINDING_COUNTERS) once at its end; in every
// other build EVO_COUNT expands to nothing, so the shaders the show runs are unchanged. The order and names
// are src/modern/path_tracer.hpp's kCounterNames.

const uint kCountPaths = 0u;             // samples traced (tracePath calls)
const uint kCountPrimaryRays = 1u;       // camera rays (the surface path's first trace)
const uint kCountBounceRays = 2u;        // the surface path's later traces
const uint kCountContinuationRays = 3u;  // traces of the primary segment's in-scattering continuation
const uint kCountHeroRays = 4u;          // traces of the further hero wavelengths (exports)
const uint kCountShadowRays = 5u;        // occlusion rays (unoccluded())
const uint kCountPaneRays = 6u;          // closest-hit traces through thin panes on shadow rays
const uint kCountGuideRays = 7u;         // the guides' centre rays
const uint kCountLensSegments = 8u;      // bent segments traced by the lens (beyond one per main ray)
const uint kCountSurfaceHits = 9u;       // path vertices on surfaces
const uint kCountSurfaceNee = 10u;       // next-event estimates at surfaces
const uint kCountMediumNee = 11u;        // next-event estimates in media (in-scatter points and vertices)
const uint kCountMediumScatter = 12u;    // medium vertices (real collisions that scatter the path)
const uint kCountInScatterPoints = 13u;  // the primary segment's in-scatter points
const uint kCountGridWalks = 14u;        // grid transmittance walks (shadow rays through a grid medium)
const uint kCountGridCells = 15u;        // majorant cells visited by the grid's DDA
const uint kCountGridLookups = 16u;      // grid density lookups (each 8 texel fetches, or one filtered)
const uint kCountTrackingSteps = 17u;    // tentative collisions of ratio and delta tracking
const uint kCountRectImportance = 18u;   // rect-light importance evaluations (light selection, its pdf)
const uint kCountTreeNodes = 19u;        // light-tree node importance evaluations
const uint kCountTreeWalks = 20u;        // light-tree walks (picks and pdfs)
const uint kCounterCount = 21u;          // modern::kCounterCount

#ifdef EVO_COUNTERS
// lo, hi 32-bit words of each 64-bit total.
layout(set = 0, binding = EVO_BINDING_COUNTERS, std430) buffer CounterBlock { uint counterTotals[]; };

uint gCounts[kCounterCount];

void countersBegin() {
    for (uint k = 0u; k < kCounterCount; ++k) {
        gCounts[k] = 0u;
    }
}

// Adds the invocation's tally to the frame's totals (the carry into the high word when the low one wraps).
void countersEnd() {
    for (uint k = 0u; k < kCounterCount; ++k) {
        uint n = gCounts[k];
        if (n != 0u) {
            uint before = atomicAdd(counterTotals[2u * k], n);
            if (before + n < before) {
                atomicAdd(counterTotals[2u * k + 1u], 1u);
            }
        }
    }
}

#define EVO_COUNT(k, n) gCounts[k] += uint(n)
#else
#define EVO_COUNT(k, n)
#endif

#endif
