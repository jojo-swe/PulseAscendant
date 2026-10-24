#pragma once

#include "retro/boot.hpp"
#include "retro/palette.hpp"
#include "show/cues.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstdint>
#include <vector>

namespace evo::retro {

// The palette index a text-only effect's screen is cleared to before its text is drawn.
using BackgroundProvider = std::uint8_t (*)(const show::TimelineFrame&, std::uint32_t retroFrame);
// The strings an effect draws into its index image with the 8x8 font (text::TextPass::drawIndex; the low byte
// of an item's rgba is its palette index), after its shader or its background.
using TextProvider = std::vector<text::Item> (*)(const show::TimelineFrame&, std::uint32_t retroFrame);

// How the retro stage renders an effect's 1993 side. The stage caches the result by (chapter, effect,
// retroFrame) and calls the builders and providers below only when it re-renders, so each of them must be a
// pure function of the chapter and the retro frame. From the TimelineFrame they may read only what is fixed
// for the whole chapter (chapter, chapterDuration, accentSeconds); the chapter-local time only as retroFrame
// (that is, quantised to the retro frame), never localSeconds, showSeconds or the music signals, which change
// within a retro frame and would make a cached frame differ from a fresh one.
struct EffectInfo {
    // The compute shader's SPIR-V file in shaderDir(). It writes palette indices into the 320x200 R8_UINT
    // image (binding 0) with the push constants of shaders/retro/test_pattern.comp, and must be a pure
    // function of (chapter, retroFrame). nullptr for a text-only effect: the stage clears the image to
    // background (index 0 when that is null too) and draws the text over it.
    const char* shader;
    // Retro frames per second: retroFrame = floor(localSeconds * retroRate).
    std::uint32_t retroRate;
    // The 256 colours of the retro frame (see PaletteBuilder).
    PaletteBuilder palette;
    // The text drawn over the frame, or nullptr for none.
    TextProvider text = nullptr;
    // The clear index of a text-only effect, or nullptr for index 0. Unused when the effect has a shader.
    BackgroundProvider background = nullptr;
};

inline constexpr const char* kTestPatternShader = "retro_test_pattern.comp.spv";
inline constexpr std::uint32_t kDefaultRetroRate = 35;
// Raytrace's 1993 side is a precalculated animation shown at a slower rate (step 3 renders it).
inline constexpr std::uint32_t kRaytraceRetroRate = 10;

// The registry. Boot is the DOS prompt; Plasma and Credits get their own entries in later tasks of the step,
// the other effects in later steps, and show the test pattern in grey until then.
inline const EffectInfo& effectFor(show::Effect effect) {
    static constexpr EffectInfo kTestPattern{kTestPatternShader, kDefaultRetroRate, grayPalette};
    static constexpr EffectInfo kRaytrace{kTestPatternShader, kRaytraceRetroRate, grayPalette};
    static constexpr EffectInfo kBoot{nullptr, kBootRetroRate, bootPalette, bootText, bootBackground};
    switch (effect) {
    case show::Effect::Boot:
        return kBoot;
    case show::Effect::Raytrace:
        return kRaytrace;
    default:
        return kTestPattern;
    }
}

} // namespace evo::retro
