#include "gfx/readback.hpp"

#include "gfx/sync.hpp"

#include <stdexcept>
#include <string>

namespace evo::gfx {

namespace {

constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;

std::string sizeText(std::uint32_t width, std::uint32_t height) {
    return std::to_string(width) + "x" + std::to_string(height);
}

} // namespace

Readback::Readback(Context& ctx, std::uint32_t width, std::uint32_t height)
    : width_(width), height_(height),
      buffer_(ctx, {VkDeviceSize{width} * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "readback"}) {}

void Readback::record(VkCommandBuffer cmd, Image& src) {
    if (src.extent().width != width_ || src.extent().height != height_)
        throw std::invalid_argument("Readback of " + sizeText(width_, height_) + " cannot copy a " +
                                    sizeText(src.extent().width, src.extent().height) + " image");
    if (src.format() != kFormat && src.format() != VK_FORMAT_R8G8B8A8_SRGB)
        throw std::invalid_argument("Readback copies R8G8B8A8_UNORM and _SRGB images only, got format " +
                                    std::to_string(static_cast<int>(src.format())));

    src.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT);
    // Orders this copy after an earlier submit's copy into the same buffer on the GPU too, not only through
    // the host's wait in between (write after write).
    bufferBarrier2(cmd, buffer_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy region{};
    region.bufferRowLength = 0; // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width_, height_, 1};
    vkCmdCopyImageToBuffer(cmd, src.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_.handle(), 1,
                           &region);

    // Makes the copied bytes available to host reads once the submit completes.
    bufferBarrier2(cmd, buffer_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
}

std::vector<std::uint8_t> Readback::rgb() const {
    buffer_.invalidate(); // a no-op on HOST_COHERENT memory

    const auto* rgba = static_cast<const std::uint8_t*>(buffer_.mapped());
    const std::size_t pixels = std::size_t{width_} * height_;
    std::vector<std::uint8_t> rgb(pixels * 3);
    for (std::size_t i = 0; i < pixels; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    return rgb;
}

} // namespace evo::gfx
