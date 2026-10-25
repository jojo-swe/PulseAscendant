#pragma once

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "render/scene_raster.hpp"
#include "retro/effects.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace evo::render {

// The 1993 side: the chapter's effect renders palette indices into a 320x200 R8_UINT image (its compute
// shader, or a clear for an effect without one, then the triangles and points of its scene drawn by the
// SceneRaster, then its text drawn with the text pass), which palette_map.comp turns into a 320x200 RGBA8
// picture through the 256 colours the effect's palette builder makes on the CPU. The colour image holds the
// sRGB-encoded bytes the palette gives: it is an R8G8B8A8_SRGB image with kRetroMipLevels levels, written
// through its R8G8B8A8_UNORM storage view (view(), level 0, byte for byte) and sampled through sampledView(),
// which decodes to linear light; its mips are averaged in linear light too (the blits of an sRGB image are),
// so a minified CRT keeps its brightness.
//
// Two effects compose more than one picture:
// - An effect with a grid (EffectInfo::grid, FINALE and PORTAL) draws each layer's source picture into a
//   scratch RGBA8 image, which tile.comp scales into its rect of the colour image (cleared to black first).
// - An effect with phosphor (EffectInfo::phosphor) keeps the palette mapped pictures of its last four retro
//   frames, each keyed by (chapter, effect, retroFrame), and phosphor.comp takes per channel the brightest of
//   the current one and the decayed older ones (retro::kPhosphorFrames). A picture missing from that cache
//   (after a seek, or on a chapter's first frames) is rendered from its own retro frame, never carried over
//   from what was shown, so a seek shows exactly what playback does.
// Everything runs only when the retro frame changes: retroFrame = floor(localSeconds * retroRate), keyed
// together with the chapter (and the effect), so a chapter that starts on the same retro frame number as the
// last one still renders its own picture. Every shader and texture is made up front, so no chapter stalls.
// The colour image's mip levels: a full chain from 320x200 down to 1x1, for the Studio's CRT to sample.
inline constexpr std::uint32_t kRetroMipLevels = 9;
static_assert(kRetroMipLevels == gfx::mipLevelsFor(320, 200));

class RetroStage {
public:
    static constexpr std::uint32_t kWidth = 320;
    static constexpr std::uint32_t kHeight = 200;

    // The effect registry the stage renders from: retro::effectFor, or a test's own.
    using EffectLookup = const retro::EffectInfo& (*)(show::Effect);

    // Loads every effect shader of the registry up front, so no chapter hitches on its first frame, and
    // builds and uploads every effect texture (EffectInfo::texture) once. Throws std::runtime_error if the
    // GPU cannot store to an R8_UINT image, std::invalid_argument for a texture without a shader, of no
    // texels or of a size other than width x height, and std::logic_error for a shader named both with and
    // without one. The textures upload with a blocking immediate submit here, so construct the stage before
    // the first frame is recorded (never mid-show). The text pass draws the palette indices of text-only
    // effects and text overlays (drawIndex); it must outlive the stage.
    RetroStage(gfx::Context& ctx, gfx::FrameRing& ring, text::TextPass& text,
               EffectLookup effects = retro::effectFor);

    // Records the effect and palette passes if (chapter, effect, retroFrame) differ from the last frame
    // rendered; returns whether they were recorded. image() then holds the frame's picture either way.
    // Throws what SceneRaster::draw throws for a scene past the limits, and std::logic_error for an effect
    // with both a grid and phosphor or a grid layer whose source composes again.
    bool render(VkCommandBuffer cmd, const show::TimelineFrame& frame);

    // The retro frame of the last render(), of the frame's own effect at its rate (for a grid, the composed
    // effect's, not its layers' home frames).
    std::uint32_t retroFrame() const { return retroFrame_; }
    // How many palette mapped pictures the last render() that recorded drew: 1 for a plain effect, one per
    // distinct layer source for a grid, and for phosphor the current frame plus each older one it did not
    // already hold.
    std::uint32_t picturesDrawn() const { return picturesDrawn_; }
    gfx::Image& image() { return color_; } // RGBA8 320x200, kRetroMipLevels levels (see above)
    // R8_UINT 320x200: the indices of the last picture drawn, which is the current frame's own for every
    // effect without a grid (for a grid, its last layer's source).
    gfx::Image& indexImage() { return index_; }

private:
    struct Key {
        int chapter;
        int effect;
        std::uint32_t retroFrame;
        bool operator==(const Key&) const = default;
    };

    // A palette mapped picture of the phosphor cache, and which retro frame it holds.
    struct Held {
        gfx::Image image;
        std::optional<Key> key;
    };

    // Draws the indices of `info` at (frame, retroFrame) into index_: its shader or background, its scene,
    // then `text` (nullptr for none).
    void drawIndices(VkCommandBuffer cmd, const retro::EffectInfo& info, const show::TimelineFrame& frame,
                     std::uint32_t retroFrame, retro::TextProvider text);
    // Maps index_ through info's palette at (frame, retroFrame) into `target` (RGBA8 storage, level 0).
    void mapPalette(VkCommandBuffer cmd, const retro::EffectInfo& info, const show::TimelineFrame& frame,
                    std::uint32_t retroFrame, gfx::Image& target);
    // The whole picture of `info` at (frame, retroFrame), composed from its grid when it has one, into
    // `target`.
    void drawPicture(VkCommandBuffer cmd, const retro::EffectInfo& info, const show::TimelineFrame& frame,
                     std::uint32_t retroFrame, gfx::Image& target);

    gfx::FrameRing& ring_;
    text::TextPass& text_; // draws the effects' text (EffectInfo::text)
    EffectLookup effects_;
    gfx::Image index_;
    gfx::Image color_;
    gfx::Image scratch_; // a grid layer's source picture (RGBA8 storage)
    std::array<Held, retro::kPhosphorFrames + 1> held_;
    // The palettes of the pictures drawn in a frame, one kPaletteBytes block each, per frame in flight: the
    // CPU writes the slot being recorded, whose last submit is done.
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> palettes_;
    std::uint32_t palettesUsed_ = 0; // blocks taken in the slot being recorded
    struct EffectShader {
        std::unique_ptr<gfx::ComputePipeline> pipeline;
        bool textured; // binds an effect texture at binding 1
    };
    std::map<std::string, EffectShader> shaders_; // by SPIR-V file name
    // The effects' textures (R8_UINT, read at binding 1), one per builder, uploaded at construction. Declared
    // before raster_, whose destructor waits for the ring, so they outlive every submit that read them.
    std::map<retro::TextureBuilder, gfx::Image> textures_;
    gfx::ComputePipeline paletteMap_;
    gfx::ComputePipeline phosphor_;
    gfx::ComputePipeline tile_;
    SceneRaster raster_;      // draws the effects' scenes (EffectInfo::scene)
    retro::RetroScene scene_; // the last scene built, kept for its capacity
    std::optional<Key> cached_;
    std::uint32_t retroFrame_ = 0;
    std::uint32_t picturesDrawn_ = 0;
    gfx::FrameRing::Subscription subscription_; // last: unregistered first
};

} // namespace evo::render
