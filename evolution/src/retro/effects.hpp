#pragma once

#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/cues.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstdint>
#include <vector>

namespace evo::retro {

// The palette index the screen of an effect without a shader is cleared to before its scene and text are
// drawn (BOOT's black, CITY's haze, FINALE's flash).
using BackgroundProvider = std::uint8_t (*)(const show::TimelineFrame&, std::uint32_t retroFrame);
// The strings an effect draws into its index image with the 8x8 font (text::TextPass::drawIndex; the low byte
// of an item's rgba is its palette index), after its shader or its background.
using TextProvider = std::vector<text::Item> (*)(const show::TimelineFrame&, std::uint32_t retroFrame);

// An effect's own texture: width x height bytes, rows top first, that its shader reads as an R8_UINT
// storage image at binding 1 (shaders/retro/rotozoom.comp). Built once, so it must not depend on the frame.
struct EffectTexture {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> texels;
};
using TextureBuilder = EffectTexture (*)();

// A retro frame of an effect: the entry, the frame its providers see (a frame of the effect's own chapter)
// and its retro frame there.
struct EffectFrame {
    show::Effect effect;
    show::TimelineFrame frame;
    std::uint32_t retroFrame;
};

// A rectangle of the 320x200 picture in whole pixels.
struct PixelRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const PixelRect&) const = default;
};

// One layer of a composed picture (EffectInfo::grid): the picture of `source`, cut to `from` and scaled into
// `to` of the colour image. The source renders as it would on its own (shader, background, scene, palette)
// but without its text, at its own frame and retro frame; a source naming the composed effect itself is that
// effect's own picture (its background, scene, text and palette at its own frame), which never composes
// again. Each pixel of `to` is the mean, in linear light, of the source pixels in its box: with
// u = x - to.x, the columns from.x + floor(u * from.width / to.width) up to (not including)
// from.x + floor((u + 1) * from.width / to.width), at least one; rows the same. The mean is multiplied by
// gain, clamped to 1 and stored as sRGB bytes; a box of one pixel at gain 1 copies its bytes. A masked layer
// leaves the pixels whose mean is black as they were.
struct GridLayer {
    EffectFrame source;
    PixelRect from, to;
    float gain = 1;
    bool masked = false;
};
// The layers of a retro frame, drawn in order over black; a pure function of the chapter and the retro frame,
// like the providers. An empty list draws the effect's own picture as if it had no grid.
using GridProvider = std::vector<GridLayer> (*)(const show::TimelineFrame&, std::uint32_t retroFrame);

// Phosphor persistence (EffectInfo::phosphor): a lit phosphor fades exponentially, to 1/e in this long.
inline constexpr double kPhosphorSeconds = 0.080;
// The same for a picture that moves fast: CREDITS' scroller travels 4.6 pixels a retro frame, and an 80 ms
// trail would double every glyph it carries, so its screen fades three times faster (a faint trail).
inline constexpr double kPhosphorFastSeconds = 0.027;
// The retro frames before the current one whose glow still shows: the picture is, per channel in linear
// light, max(frame(r), d * frame(r - 1), d^2 * frame(r - 2), d^3 * frame(r - 3)) with
// d = exp(-(1 / retroRate) / seconds) (seconds: EffectInfo::phosphorSeconds); frames before the chapter's
// first retro frame are black.
inline constexpr std::uint32_t kPhosphorFrames = 3;
double phosphorDecay(std::uint32_t retroRate, double seconds = kPhosphorSeconds);

// How the retro stage renders an effect's 1993 side. The stage caches the result by (chapter, effect,
// retroFrame) and calls the builders and providers below only when it re-renders, so each of them must be a
// pure function of the chapter and the retro frame. From the TimelineFrame they may read only what is fixed
// for the whole chapter (chapter, chapterDuration, accentSeconds, and data fixed by the chapter alone, such
// as its track's tracked beats from show::beatGrid, which GLENZ bounces on); the chapter-local time only as
// retroFrame (that is, quantised to the retro frame), never localSeconds, showSeconds or the music signals,
// which change within a retro frame and would make a cached frame differ from a fresh one.
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
    // The shader's texture, or nullptr for none. The stage builds and uploads it once, when it is made, and
    // keeps one image per builder (effects that name the same builder share it). Needs a shader.
    TextureBuilder texture = nullptr;
    // A picture composed of other effects' pictures (see GridLayer), or nullptr for none. The stage still
    // caches by this effect's (chapter, effect, retroFrame). An effect with a grid has no phosphor.
    GridProvider grid = nullptr;
    // Whether the screen shows phosphor persistence (kPhosphorSeconds): each retro frame is re-rendered from
    // (chapter, retroFrame - k), never carried over from what was shown, so a seek shows what playback does.
    bool phosphor = false;
    // The phosphor's time constant: kPhosphorSeconds, or kPhosphorFastSeconds for a fast-moving picture.
    double phosphorSeconds = kPhosphorSeconds;
};

inline constexpr const char* kTestPatternShader = "retro_test_pattern.comp.spv";
inline constexpr std::uint32_t kDefaultRetroRate = 35;
// Raytrace's 1993 side is a precalculated animation shown at a slower rate (retro/raytrace.hpp).
inline constexpr std::uint32_t kRaytraceRetroRate = 10;
// PLASMA's index image (shaders/retro/plasma.comp) and its rate; the shader's clock is retroSeconds.
inline constexpr const char* kPlasmaShader = "retro_plasma.comp.spv";
inline constexpr std::uint32_t kPlasmaRetroRate = 35;

// The registry (effects.cpp): the entry of every effect. Boot is the DOS prompt, Plasma the cycling plasma,
// Credits the sine scroller over a slow starfield, Flyby the flat-shaded ship over a starfield with its
// greeting scrolling over them, Dots the 512 morphing points, Tunnel the dot-ring tunnel that turns into a
// solid tube, Glenz Future Crew's translucent glenz vector bouncing over copper bars, Lens the rotozoomed
// logo texture under a bouncing lens, Raytrace the dithered chrome spheres over a checkerboard at 10 fps,
// City the gouraud vector city flown at sunset, Finale the eight effects as a 3x3 grid round an EVOLUTION
// scroller, Portal that grid switching off tile by tile down to `THE SCREEN_`, StudioNight the DOS prompt the
// demo exited to (retro/dos_idle.hpp), Return the CRT switched on again and the prompt typing the way back,
// Reboot the reboot script and DOTS' cube, TheEnd the last line and the tube switching off. BOOT, REBOOT, the
// DOS prompt, RETURN, CREDITS and THE END show phosphor persistence. Act III's planetary chapters (GALAXY to
// ASCENDANT) are 2026 only and keep the grey test pattern for good (renderer_test and boot_test use GALAXY's
// and check that it is the test pattern).
const EffectInfo& effectFor(show::Effect effect);

// The FINALE megamix's thank-you card (retro/finale.hpp): a text screen at FINALE's rate, drawn by the stage
// instead of FINALE's grid on the frames show::thankYouCard names.
const EffectInfo& finaleCardEffect();

} // namespace evo::retro
