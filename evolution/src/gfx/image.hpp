#pragma once

#include "gfx/context.hpp"

#include <volk.h>

#include <vk_mem_alloc.h>

#include <cstdint>

namespace evo::gfx {

struct ImageDesc {
    std::uint32_t width = 0, height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    std::uint32_t mipLevels = 1;
    const char* name = ""; // shown by the validation layer and debuggers
};

// A 2D device-local image with a view of all its mip levels. It remembers its layout, its last write and the
// reads since, so transition() records exactly the barrier the next use needs. The tracked state is the
// state at the end of everything recorded so far, so an image must be used from one command stream in
// submission order (which the frame ring guarantees).
class Image {
public:
    Image() = default;
    Image(Context& ctx, const ImageDesc& desc);
    ~Image();
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    // Prepares the image for a use at (dstStage, dstAccess) in newLayout. A write or a layout change waits
    // for the last write and for every read since; a read in the same layout waits for the last write only
    // when this stage and access have not already been ordered after it. Returns whether a barrier was
    // recorded (none for a read that is already ordered: read after read needs no barrier).
    bool transition(VkCommandBuffer cmd, VkImageLayout newLayout, VkPipelineStageFlags2 dstStage,
                    VkAccessFlags2 dstAccess);

    VkImage handle() const { return image_; }
    VkImageView view() const { return view_; }
    VkImageLayout layout() const { return layout_; }
    VkExtent2D extent() const { return extent_; }
    VkFormat format() const { return format_; }

private:
    void destroy() noexcept;

    Context* ctx_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkExtent2D extent_{0, 0};
    VkFormat format_ = VK_FORMAT_UNDEFINED;

    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 writeStage_ = VK_PIPELINE_STAGE_2_NONE; // the last write (or layout change)
    VkAccessFlags2 writeAccess_ = VK_ACCESS_2_NONE;               // its accesses still to make available
    VkPipelineStageFlags2 readStages_ = VK_PIPELINE_STAGE_2_NONE; // reads ordered after it so far
    VkAccessFlags2 readAccess_ = VK_ACCESS_2_NONE;
};

} // namespace evo::gfx
