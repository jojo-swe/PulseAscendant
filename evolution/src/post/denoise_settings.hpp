#pragma once

// The denoiser's per-frame settings, apart from post/denoiser.hpp so that code choosing them
// (render/stage_plan.hpp) does not include Vulkan.

#include <cstdint>

namespace evo::post {

struct DenoiseSettings {
    bool temporal = true;               // false: every pixel starts its history this frame (export)
    std::uint32_t atrousIterations = 3; // steps 1, 2, 4, ...; 0: no surface filter (nor reset passes)
    float alphaColor = 0.2f, alphaMoments = 0.2f;
    std::uint32_t maxHistory = 64;
    float sigmaDepth = 1.f, sigmaNormal = 128.f, sigmaLuminance = 4.f;
    std::uint32_t resetAtrousIterations = 5; // at history length <= 3 (when atrousIterations > 0)
    bool fireflyFilter = false;              // exports at spp >= 16
    float fireflyK = 4.f;
    std::uint32_t volumeAtrousIterations = 4;
    // false: the frame has no participating medium, so the G-buffer's volume layer is (0, 0, 0, 1) and the
    // denoiser neither reads nor filters it (the same output bytes, without the volume passes and loads).
    bool hasMedium = true;
};

} // namespace evo::post
