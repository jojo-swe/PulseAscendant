#pragma once

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "text/atlas.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace evo::text {

// One string to draw. (x, y) is the top-left corner of its first glyph in pixels, y growing downwards; the
// position is floored to whole pixels and the scale rounded to a whole factor (at least 1), so every glyph
// pixel becomes a scale x scale block. Glyphs advance 8 * scale pixels; '\n' starts a new line 8 * scale
// lower at x. The text is UTF-8 (see glyphCodes). rgba is a colour with red in the low byte and alpha in
// the high byte for drawRgba; drawIndex writes its low byte as the palette index.
struct Item {
    float x = 0, y = 0;
    float scale = 1;
    std::uint32_t rgba = 0xFFFFFFFFu;
    std::string text;
};

// Draws Items with the 8x8 font: a compute pass over glyph instances, one 8x8 workgroup per glyph that
// reads its cell of the font atlas and writes each set pixel's scale x scale block, clipped per pixel to the
// target. Two variants:
//   drawIndex  into an R8_UINT palette index image (the 1993 stage), writing the index where the font is set;
//   drawRgba   into an R8G8B8A8_UNORM image, alpha-blending the colour over what is there.
// Items are drawn in order: an item that overlaps an earlier one of the same call goes in a later dispatch
// behind a barrier, so it lands on top (a drop shadow drawn first, then the text). Both record into the
// ring's current command buffer (between begin() and submit()) and leave the target in GENERAL.
//
// The glyph instances go to a host-visible storage buffer of the ring slot being recorded, which the CPU
// rewrites only after begin() has waited for that slot's previous frame; a buffer that runs out within a
// frame is followed by a larger one, and the slot's buffers are merged into one when it comes round again.
//
// The ring must outlive the pass; the destructor waits for it before the pipelines and buffers go.
class TextPass {
public:
    TextPass(gfx::Context& ctx, gfx::FrameRing& ring);
    ~TextPass();
    TextPass(const TextPass&) = delete;
    TextPass& operator=(const TextPass&) = delete;

    // Throw std::invalid_argument for a target of the other format, std::logic_error outside the ring's
    // begin()/submit().
    void drawIndex(VkCommandBuffer cmd, gfx::Image& indexImage, std::span<const Item> items);
    void drawRgba(VkCommandBuffer cmd, gfx::Image& rgbaImage, std::span<const Item> items);

    // The glyph instance the shaders read (shaders/text/glyphs.glsl).
    struct GlyphInstance {
        std::int32_t x, y;       // the top-left pixel of the glyph cell
        std::uint32_t codeScale; // glyph code (32..127) in the low byte, scale above it
        std::uint32_t rgba;
    };

private:
    struct SlotBuffers {
        std::vector<gfx::Buffer> buffers; // this frame's instances; only the last one has room left
        std::uint32_t used = 0;           // instances written into buffers.back()
    };

    void draw(VkCommandBuffer cmd, gfx::ComputePipeline& pass, gfx::Image& target,
              std::span<const Item> items, VkAccessFlags2 targetAccess);
    void dispatch(VkCommandBuffer cmd, gfx::ComputePipeline& pass, gfx::Image& target,
                  std::span<const GlyphInstance> glyphs, VkAccessFlags2 targetAccess);
    void resetSlot(std::uint32_t slot);

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    Atlas atlas_;
    gfx::ComputePipeline index_;
    gfx::ComputePipeline rgba_;
    std::array<SlotBuffers, gfx::FrameRing::kFramesInFlight> slots_;
    gfx::FrameRing::Subscription subscription_; // last: unregistered first
};

} // namespace evo::text
