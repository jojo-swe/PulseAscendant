#pragma once

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
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

// The 1993 side: the chapter's effect renders palette indices into a 320x200 R8_UINT image, which
// palette_map.comp turns into a 320x200 RGBA8 image through the 256 colours the effect's palette builder
// makes on the CPU. Both passes run only when the retro frame changes: retroFrame = floor(localSeconds *
// retroRate), keyed together with the chapter (and the effect), so a chapter that starts on the same retro
// frame number as the last one still renders its own picture.
class RetroStage {
public:
    static constexpr std::uint32_t kWidth = 320;
    static constexpr std::uint32_t kHeight = 200;

    // Loads every effect shader of the registry up front, so no chapter hitches on its first frame. Throws
    // std::runtime_error if the GPU cannot store to an R8_UINT image.
    // The text pass draws the palette indices of text-only effects and text overlays (drawIndex); it must
    // outlive the stage.
    RetroStage(gfx::Context& ctx, gfx::FrameRing& ring, text::TextPass& text);

    // Records the effect and palette passes if (chapter, effect, retroFrame) differ from the last frame
    // rendered; returns whether they were recorded. image() then holds the frame's picture either way.
    bool render(VkCommandBuffer cmd, const show::TimelineFrame& frame);

    // The retro frame of the last render().
    std::uint32_t retroFrame() const { return retroFrame_; }
    gfx::Image& image() { return color_; }      // RGBA8 320x200
    gfx::Image& indexImage() { return index_; } // R8_UINT 320x200

private:
    struct Key {
        int chapter;
        int effect;
        std::uint32_t retroFrame;
        bool operator==(const Key&) const = default;
    };

    gfx::FrameRing& ring_;
    text::TextPass& text_; // for the text-only effects and overlays of the later tasks of the step
    gfx::Image index_;
    gfx::Image color_;
    // One palette per frame in flight: the CPU writes the slot being recorded, whose last submit is done.
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> palettes_;
    std::map<std::string, std::unique_ptr<gfx::ComputePipeline>> effects_; // by SPIR-V file name
    gfx::ComputePipeline paletteMap_;
    std::optional<Key> cached_;
    std::uint32_t retroFrame_ = 0;
};

} // namespace evo::render
