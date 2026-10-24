#pragma once

#include "retro/palette.hpp"
#include "show/cues.hpp"

#include <cstdint>

namespace evo::retro {

// How the retro stage renders an effect's 1993 side.
struct EffectInfo {
    // The compute shader's SPIR-V file in shaderDir(). It writes palette indices into the 320x200 R8_UINT
    // image (binding 0) with the push constants of shaders/retro/test_pattern.comp, and must be a pure
    // function of (chapter, retroFrame).
    const char* shader;
    // Retro frames per second: retroFrame = floor(localSeconds * retroRate).
    std::uint32_t retroRate;
    PaletteBuilder palette;
};

inline constexpr const char* kTestPatternShader = "retro_test_pattern.comp.spv";
inline constexpr std::uint32_t kDefaultRetroRate = 35;
// Raytrace's 1993 side is a precalculated animation shown at a slower rate (step 3 renders it).
inline constexpr std::uint32_t kRaytraceRetroRate = 10;

// The registry. In step 2 every effect shows the test pattern in grey; Boot, Plasma and Credits get their
// own entries in later tasks of the step, the other effects in later steps.
inline const EffectInfo& effectFor(show::Effect effect) {
    static constexpr EffectInfo kTestPattern{kTestPatternShader, kDefaultRetroRate, grayPalette};
    static constexpr EffectInfo kRaytrace{kTestPatternShader, kRaytraceRetroRate, grayPalette};
    switch (effect) {
    case show::Effect::Raytrace:
        return kRaytrace;
    default:
        return kTestPattern;
    }
}

} // namespace evo::retro
