#pragma once

#include "gfx/context.hpp"

#include <volk.h>

#include <vk_mem_alloc.h>

#include <cstdint>

namespace evo::gfx {

// The mip levels of a full chain down to 1x1: floor(log2(max(width, height))) + 1 (0 for an empty size).
constexpr std::uint32_t mipLevelsFor(std::uint32_t width, std::uint32_t height) {
    std::uint32_t levels = 0;
    for (std::uint32_t size = width > height ? width : height; size > 0; size >>= 1)
        ++levels;
    return levels;
}

struct ImageDesc {
    std::uint32_t width = 0, height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    std::uint32_t mipLevels = 1;
    const char* name = ""; // shown by the validation layer and debuggers
    // The format of view() when it differs from `format` (UNDEFINED: the same). The image is then created
    // with MUTABLE_FORMAT and EXTENDED_USAGE, so for example an R8G8B8A8_SRGB image (sampled, and mipmapped,
    // in linear light) can be written through an R8G8B8A8_UNORM storage view, byte for byte.
    VkFormat viewFormat = VK_FORMAT_UNDEFINED;
};

// A 2D device-local image with a view of its first mip level and a view of all of them. It remembers its
// layout, its last write and the reads since, so transition() records exactly the barrier the next use needs.
// The tracked state is one state for the whole image (every mip level): generateMips() orders its levels
// itself and leaves them all in that state again. The tracked state is the state at the end of everything
// recorded so far, so an image must be used from one command stream in submission order (which the frame
// ring guarantees).
class Image {
public:
    Image() = default;
    // Throws std::invalid_argument for an empty size, no mip levels or more than mipLevelsFor(width, height).
    Image(Context& ctx, const ImageDesc& desc);
    ~Image();
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    // Prepares the image (every mip level) for a use at (dstStage, dstAccess) in newLayout. A write or a
    // layout change waits for the last write and for every read since; a read in the same layout waits for
    // the last write only when this stage and access have not already been ordered after it. Returns whether
    // a barrier was recorded (none for a read that is already ordered: read after read needs no barrier).
    bool transition(VkCommandBuffer cmd, VkImageLayout newLayout, VkPipelineStageFlags2 dstStage,
                    VkAccessFlags2 dstAccess);

    // Level 0 holds the picture. Blits level i-1 -> i (linear filter) with a barrier per level, all in
    // GENERAL, and leaves every level in GENERAL ordered before (dstStage, dstAccess); the tracked state
    // follows. An sRGB image averages in linear light. Throws std::logic_error, recording nothing, if the
    // format lacks BLIT_SRC, BLIT_DST or SAMPLED_IMAGE_FILTER_LINEAR or the usage lacks TRANSFER_SRC or
    // TRANSFER_DST. A one-level image only transitions.
    void generateMips(VkCommandBuffer cmd, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

    VkImage handle() const { return image_; }
    // Mip level 0 only, in viewFormat() (storage images, attachments); the depth aspect of a depth format.
    VkImageView view() const { return view_; }
    // Every mip level in format() (samplers); the same view as view() for a one-level image whose formats
    // agree.
    VkImageView sampledView() const { return sampledView_; }
    VkImageLayout layout() const { return layout_; }
    VkExtent2D extent() const { return extent_; }
    VkFormat format() const { return format_; }
    VkFormat viewFormat() const { return viewFormat_; }
    VkImageAspectFlags aspect() const { return aspect_; } // DEPTH (+ STENCIL) for depth formats, else COLOR
    std::uint32_t mipLevels() const { return mipLevels_; }
    // How many times generateMips() has recorded; for tests that pin when an image is re-mipped.
    std::uint64_t mipGenerations() const { return mipGenerations_; }

private:
    void destroy() noexcept;

    Context* ctx_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkImageView sampledView_ = VK_NULL_HANDLE; // == view_ when one view serves both
    VkExtent2D extent_{0, 0};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkFormat viewFormat_ = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageUsageFlags usage_ = 0;
    std::uint32_t mipLevels_ = 1;
    std::uint64_t mipGenerations_ = 0;

    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 writeStage_ = VK_PIPELINE_STAGE_2_NONE; // the last write (or layout change)
    VkAccessFlags2 writeAccess_ = VK_ACCESS_2_NONE;               // its accesses still to make available
    VkPipelineStageFlags2 readStages_ = VK_PIPELINE_STAGE_2_NONE; // reads ordered after it so far
    VkAccessFlags2 readAccess_ = VK_ACCESS_2_NONE;
};

} // namespace evo::gfx
