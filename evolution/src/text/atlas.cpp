#include "text/atlas.hpp"

#include "gfx/buffer.hpp"
#include "text/font8x8.hpp"

#include <cstring>

namespace evo::text {

std::vector<std::uint8_t> atlasPixels() {
    std::vector<std::uint8_t> pixels(std::size_t{kAtlasWidth} * kAtlasHeight, 0);
    for (int index = 0; index < kGlyphCount; ++index) {
        const GlyphRows& rows = glyph(static_cast<char>(kFirstGlyph + index));
        const std::uint32_t cellX = index % kAtlasColumns * 8, cellY = index / kAtlasColumns * 8;
        for (std::uint32_t y = 0; y < 8; ++y)
            for (std::uint32_t x = 0; x < 8; ++x)
                if (rows[y] & (0x80u >> x))
                    pixels[(cellY + y) * kAtlasWidth + cellX + x] = 255;
    }
    return pixels;
}

Atlas::Atlas(gfx::Context& ctx)
    : image_(ctx, {kAtlasWidth, kAtlasHeight, VK_FORMAT_R8_UNORM,
                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "font atlas"}) {
    const std::vector<std::uint8_t> pixels = atlasPixels();
    gfx::Buffer staging(ctx, {pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "font atlas staging"});
    std::memcpy(staging.mapped(), pixels.data(), pixels.size());
    staging.flush();

    // immediate() waits for the copy, so the staging buffer may go when this returns.
    ctx.immediate([&](VkCommandBuffer cmd) { // setup: once, when the pass is built
        image_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kAtlasWidth, kAtlasHeight, 1};
        vkCmdCopyBufferToImage(cmd, staging.handle(), image_.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);
        image_.transition(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
}

} // namespace evo::text
