#pragma once

#include "retro/palette.hpp"
#include "retro/scene.hpp"
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
    // function of (chapter, retroFrame). nullptr for an effect drawn by its scene or text alone: the stage
    // clears the image to background (index 0 when that is null too) and draws the scene and text over it.
    const char* shader;
    // Retro frames per second: retroFrame = floor(localSeconds * retroRate).
    std::uint32_t retroRate;
    // The 256 colours of the retro frame (see PaletteBuilder).
    PaletteBuilder palette;
    // The text drawn over the frame, or nullptr for none.
    TextProvider text = nullptr;
    // The clear index of an effect without a shader (text or scene only), or nullptr for index 0. Unused when
    // the effect has a shader.
    BackgroundProvider background = nullptr;
    // The triangles and points drawn over the shader's image or the background, before the text
    // (render::SceneRaster), or nullptr for none. The stage clears the scene before calling it.
    SceneProvider scene = nullptr;
};

inline constexpr const char* kTestPatternShader = "retro_test_pattern.comp.spv";
inline constexpr std::uint32_t kDefaultRetroRate = 35;
// Raytrace's 1993 side is a precalculated animation shown at a slower rate (step 3 renders it).
inline constexpr std::uint32_t kRaytraceRetroRate = 10;
// PLASMA's index image (shaders/retro/plasma.comp) and its rate; the shader's clock is retroSeconds.
inline constexpr const char* kPlasmaShader = "retro_plasma.comp.spv";
inline constexpr std::uint32_t kPlasmaRetroRate = 35;

// The registry (effects.cpp): the entry of every effect. Boot is the DOS prompt, Plasma the cycling plasma,
// Credits the sine scroller on deep blue, Flyby the flat-shaded ship over a starfield with its greeting
// scrolling over them, Dots the 512 morphing points. The other effects get their entries in later steps and
// show the test pattern in grey until then.
const EffectInfo& effectFor(show::Effect effect);

} // namespace evo::retro
