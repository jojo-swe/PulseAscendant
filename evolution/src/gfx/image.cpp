#include "gfx/image.hpp"

#include "gfx/sync.hpp"
#include "gfx/vk_check.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace evo::gfx {

namespace {

// Every access flag that writes memory. A transition to one of these is a write hazard.
constexpr VkAccessFlags2 kWriteAccess =
    VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT |
    VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

} // namespace

Image::Image(Context& ctx, const ImageDesc& desc)
    : ctx_(&ctx), extent_{desc.width, desc.height}, format_(desc.format) {
    if (desc.width == 0 || desc.height == 0 || desc.mipLevels == 0)
        throw std::invalid_argument("Image '" + std::string(desc.name) + "' needs a positive size, got " +
                                    std::to_string(desc.width) + "x" + std::to_string(desc.height) +
                                    " with " + std::to_string(desc.mipLevels) + " mip levels");

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = desc.format;
    imageInfo.extent = {desc.width, desc.height, 1};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = desc.usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vkCheck(vmaCreateImage(ctx.allocator(), &imageInfo, &allocInfo, &image_, &allocation_, nullptr),
            "vmaCreateImage");

    try {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = desc.format;
        viewInfo.subresourceRange = kColorRange;
        vkCheck(vkCreateImageView(ctx.device(), &viewInfo, nullptr, &view_), "vkCreateImageView");
    } catch (...) {
        destroy();
        throw;
    }

    vmaSetAllocationName(ctx.allocator(), allocation_, desc.name);
    ctx.setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(image_), desc.name);
    ctx.setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(view_), desc.name);
}

Image::~Image() {
    destroy();
}

Image::Image(Image&& other) noexcept
    : ctx_(std::exchange(other.ctx_, nullptr)), image_(std::exchange(other.image_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      view_(std::exchange(other.view_, VK_NULL_HANDLE)), extent_(std::exchange(other.extent_, {0, 0})),
      format_(std::exchange(other.format_, VK_FORMAT_UNDEFINED)),
      layout_(std::exchange(other.layout_, VK_IMAGE_LAYOUT_UNDEFINED)),
      writeStage_(std::exchange(other.writeStage_, VK_PIPELINE_STAGE_2_NONE)),
      writeAccess_(std::exchange(other.writeAccess_, VK_ACCESS_2_NONE)),
      readStages_(std::exchange(other.readStages_, VK_PIPELINE_STAGE_2_NONE)),
      readAccess_(std::exchange(other.readAccess_, VK_ACCESS_2_NONE)) {}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        destroy();
        ctx_ = std::exchange(other.ctx_, nullptr);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE);
        extent_ = std::exchange(other.extent_, {0, 0});
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        layout_ = std::exchange(other.layout_, VK_IMAGE_LAYOUT_UNDEFINED);
        writeStage_ = std::exchange(other.writeStage_, VK_PIPELINE_STAGE_2_NONE);
        writeAccess_ = std::exchange(other.writeAccess_, VK_ACCESS_2_NONE);
        readStages_ = std::exchange(other.readStages_, VK_PIPELINE_STAGE_2_NONE);
        readAccess_ = std::exchange(other.readAccess_, VK_ACCESS_2_NONE);
    }
    return *this;
}

void Image::destroy() noexcept {
    if (!ctx_)
        return;
    if (view_)
        vkDestroyImageView(ctx_->device(), view_, nullptr);
    if (image_)
        vmaDestroyImage(ctx_->allocator(), image_, allocation_);
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    ctx_ = nullptr;
}

bool Image::transition(VkCommandBuffer cmd, VkImageLayout newLayout, VkPipelineStageFlags2 dstStage,
                       VkAccessFlags2 dstAccess) {
    const bool writes = (dstAccess & kWriteAccess) != 0;

    if (newLayout == layout_ && !writes) {
        // Read after read is no hazard; only a stage or access not yet ordered after the last write waits.
        if ((dstStage & ~readStages_) == 0 && (dstAccess & ~readAccess_) == 0)
            return false;
        imageBarrier2(cmd, image_, layout_, layout_, writeStage_, writeAccess_, dstStage, dstAccess);
        readStages_ |= dstStage;
        readAccess_ |= dstAccess;
        return true;
    }

    // A write, or a layout change (which writes the image): wait for the last write and make it available,
    // and wait for every read since so none of them sees the new contents (write after read).
    imageBarrier2(cmd, image_, layout_, newLayout, writeStage_ | readStages_, writeAccess_, dstStage,
                  dstAccess);
    layout_ = newLayout;
    writeStage_ = dstStage; // later uses chain on this stage, which the barrier ordered after the change
    if (writes) {
        writeAccess_ = dstAccess;
        readStages_ = VK_PIPELINE_STAGE_2_NONE;
        readAccess_ = VK_ACCESS_2_NONE;
    } else {
        // A layout change into a read layout: the old write is now available, and this read is ordered.
        writeAccess_ = VK_ACCESS_2_NONE;
        readStages_ = dstStage;
        readAccess_ = dstAccess;
    }
    return true;
}

} // namespace evo::gfx
