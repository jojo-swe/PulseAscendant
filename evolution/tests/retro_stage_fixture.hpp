// A retro stage with its own frame ring and text pass for the effect tests: every render() records one frame,
// submits, waits and reads back the 320x200 palette index image (and, when asked for, the palette-mapped
// colours).

#pragma once

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "render/retro_stage.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace evo::test {

// A 320x200 palette index image and, if the fixture reads colours, its palette-mapped colours.
struct RetroPicture {
    std::vector<std::uint8_t> indices; // row by row
    std::vector<std::uint8_t> rgb;     // row by row, 3 bytes a pixel; empty without colours

    std::uint8_t at(int x, int y) const {
        return indices[static_cast<std::size_t>(y) * render::RetroStage::kWidth +
                       static_cast<std::size_t>(x)];
    }
};

class RetroStageFixture {
public:
    static constexpr std::uint32_t kWidth = render::RetroStage::kWidth;
    static constexpr std::uint32_t kHeight = render::RetroStage::kHeight;

    explicit RetroStageFixture(gfx::Context& ctx, bool colours = false)
        : ring_(ctx), text_(ctx, ring_), stage_(ctx, ring_, text_),
          indexBuffer_(ctx, {VkDeviceSize{kWidth} * kHeight, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                             "retro stage fixture index readback"}) {
        if (colours)
            colour_.emplace(ctx, kWidth, kHeight);
    }

    RetroPicture renderPicture(const show::TimelineFrame& frame, bool* rendered = nullptr) {
        VkCommandBuffer cmd = ring_.begin();
        const bool recorded = stage_.render(cmd, frame);
        if (rendered)
            *rendered = recorded;
        gfx::Image& index = stage_.indexImage();
        index.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kWidth, kHeight, 1};
        vkCmdCopyImageToBuffer(cmd, index.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               indexBuffer_.handle(), 1, &region);
        gfx::bufferBarrier2(cmd, indexBuffer_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                            VK_ACCESS_2_HOST_READ_BIT);
        if (colour_)
            colour_->record(cmd, stage_.image());
        ring_.wait(ring_.submit());
        indexBuffer_.invalidate();
        const auto* data = static_cast<const std::uint8_t*>(indexBuffer_.mapped());
        RetroPicture picture{{data, data + indexBuffer_.size()}, {}};
        if (colour_)
            picture.rgb = colour_->rgb();
        return picture;
    }

    // The index image alone.
    std::vector<std::uint8_t> render(const show::TimelineFrame& frame, bool* rendered = nullptr) {
        return renderPicture(frame, rendered).indices;
    }

    render::RetroStage& stage() { return stage_; }

private:
    gfx::FrameRing ring_;
    text::TextPass text_;
    render::RetroStage stage_;
    gfx::Buffer indexBuffer_;
    std::optional<gfx::Readback> colour_;
};

} // namespace evo::test
