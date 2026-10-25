// The standard checks of a step 5 scene test (the "standard scene contract" of the step 5 plan), shared by
// every tests/scene_<scene>_test.cpp (CMake: evo_scene_test(<scene>), linked with this fixture). Each check
// throws std::runtime_error naming the chapter, time and what failed, and prints one line per chapter it
// passes.

#pragma once

#include "gfx/context.hpp"
#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"

#include <cstdint>
#include <vector>

namespace evo::test {

// The chapters whose effect the show's scene map sends to `scene`, in show order.
std::vector<int> chaptersOf(modern::SceneId scene);

// The picture checks' settings: 160x90, spp 2 and detail 1/256 by default.
struct PictureOptions {
    std::uint32_t width = 160, height = 90, spp = 2;
    modern::SceneDetail detail{1.f / 256};
    modern::SceneFactory factory; // empty: the show's scenes
};

// The picture checks' bounds.
inline constexpr double kMinMeanLuminance = 0.03, kMaxMeanLuminance = 0.85; // display luminance, Rec. 709
inline constexpr double kMaxClippedFraction = 0.02;                         // pixels with a channel at 255
inline constexpr double kMaxBlackFraction = 0.60;                           // pixels at (0, 0, 0)
inline constexpr double kMinChangedFraction = 0.50; // pixels that differ from the placeholder's picture

// checkChapterPictures: for every chapter of `scene`, the frames at 20 %, the accent and 80 % of the chapter,
// rendered in Export without the HUD and forced into 2026 (as EVO_FORCE_ERA=1 does), must each have a mean
// display luminance in [kMinMeanLuminance, kMaxMeanLuminance], fewer than kMaxClippedFraction of their pixels
// clipped (a channel at 255), fewer than kMaxBlackFraction pure black, and more than kMinChangedFraction of
// their pixels different from the placeholder's picture of the same frame; the accent frame rendered again
// after the others must be byte for byte the same. Throws std::runtime_error when the scene shows in no
// chapter. Uses the validation layer of `ctx` as it is (the caller checks its message count).
void checkChapterPictures(gfx::Context& ctx, modern::SceneId scene, const PictureOptions& options = {});

// The sweep's limits: how far the camera's eye and target may move in 1/60 s within one shot (world units).
struct SweepLimits {
    float maxCameraStep = 1.f;
    modern::SceneDetail detail{1.f / 256};
    modern::SceneFactory factory; // empty: the show's scenes
};

struct SweepStats {
    std::uint32_t frames = 0;
    std::uint32_t maxInstances = 0, maxLights = 0, maxMaterials = 0; // the largest frame seen
    std::uint32_t shots = 0;                                         // over all chapters
    float maxCameraStep = 0;                                         // within a shot
};

// checkSceneSweep: every 1/60 s of each chapter of `scene`, on the CPU. Each frame's camera must be finite (a
// positive field of view under 180 degrees, aperture >= 0, focus distance > 0) and equal camera(); within a
// shot the eye and the target move at most limits.maxCameraStep per frame; the exposure is finite and
// clampIndirect positive and finite; shots run 0, 1, 2, ... in order (the next frame's shot is the same or
// one more); everything stays within |coordinate| < 10^4; every instance's transform is finite and neither
// mirrors nor collapses (determinant > 0); and the frame passes modern::checkSceneFrame (capacities, indices,
// lights against their instances). analytic spheres and emissive splats add the particle and splat counts against the capacity
// here.
SweepStats checkSceneSweep(modern::SceneId scene, const SweepLimits& limits = {});

} // namespace evo::test
