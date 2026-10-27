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
    // After the surface passes: this many à-trous iterations (steps 1, 2, 4, ...) that smooth only the
    // chroma of the illumination, each pixel keeping its luminance (atrous.comp's chroma mode), with the
    // chromaticity stop scaled by sigmaChroma. 0 (the window): none.
    std::uint32_t chromaAtrousIterations = 0;
    float sigmaChroma = 4.f;
    // false: the frame has no participating medium, so the G-buffer's volume layer is (0, 0, 0, 1) and the
    // denoiser neither reads nor filters it (the same output bytes, without the volume passes and loads).
    bool hasMedium = true;
    // The window's fast mode: the path tracer traced the volume layer's in-scattered light in half of the
    // pixels (modern::TraceSettings::halfVolume: 1 where x + y is even, 2 where it is odd; 0 in every pixel).
    // The temporal pass fills the others from their traced neighbours and clamps the layer's history against
    // the traced pixels of the 5x5 neighbourhood (shaders/post/accumulate.comp).
    std::uint32_t volumeHalf = 0;
};

} // namespace evo::post
