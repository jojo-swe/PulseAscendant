#include "gfx/image.hpp"

#include "gfx/sync.hpp"
#include "gfx/vk_check.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
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

std::string hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, "%x", value);
    return text;
}

// The aspect a view of a format sees: the depth of a depth/stencil format, else its only aspect.
VkImageAspectFlags viewAspect(VkImageAspectFlags aspect) {
    return (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 ? VK_IMAGE_ASPECT_DEPTH_BIT : aspect;
}

} // namespace

Image::Image(Context& ctx, const ImageDesc& desc)
    : ctx_(&ctx), extent_{desc.width, desc.height}, format_(desc.format),
      viewFormat_(desc.viewFormat == VK_FORMAT_UNDEFINED ? desc.format : desc.viewFormat),
      aspect_(formatAspect(desc.format)), usage_(desc.usage), mipLevels_(desc.mipLevels) {
    if (desc.width == 0 || desc.height == 0 || desc.mipLevels == 0 ||
        desc.mipLevels > mipLevelsFor(desc.width, desc.height))
        throw std::invalid_argument("Image '" + std::string(desc.name) + "' needs a positive size and 1 to " +
                                    std::to_string(mipLevelsFor(desc.width, desc.height)) +
                                    " mip levels, got " + std::to_string(desc.width) + "x" +
                                    std::to_string(desc.height) + " with " + std::to_string(desc.mipLevels));
    const bool mutableFormat = viewFormat_ != format_;

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
    // Two formats: the image may be viewed as either, and may carry a usage only the view format supports
    // (storage on an sRGB image through its UNORM view).
    const std::array<VkFormat, 2> formats = {format_, viewFormat_};
    VkImageFormatListCreateInfo formatList{VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};
    if (mutableFormat) {
        imageInfo.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
        formatList.viewFormatCount = static_cast<std::uint32_t>(formats.size());
        formatList.pViewFormats = formats.data();
        imageInfo.pNext = &formatList;

        // Whether the device supports this combination (an extended usage such as storage on an sRGB image
        // is optional): a clear error rather than a failed vkCreateImage.
        VkPhysicalDeviceImageFormatInfo2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
        query.pNext = &formatList;
        query.format = imageInfo.format;
        query.type = imageInfo.imageType;
        query.tiling = imageInfo.tiling;
        query.usage = imageInfo.usage;
        query.flags = imageInfo.flags;
        VkImageFormatProperties2 supported{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
        const VkResult result =
            vkGetPhysicalDeviceImageFormatProperties2(ctx.physicalDevice(), &query, &supported);
        const VkImageFormatProperties& limits = supported.imageFormatProperties;
        if (result != VK_SUCCESS || desc.width > limits.maxExtent.width ||
            desc.height > limits.maxExtent.height || desc.mipLevels > limits.maxMipLevels)
            throw std::runtime_error(
                "Image '" + std::string(desc.name) + "': the device does not support format " +
                std::to_string(static_cast<int>(format_)) + " viewed as " +
                std::to_string(static_cast<int>(viewFormat_)) + " with usage 0x" + hex(desc.usage) + " at " +
                std::to_string(desc.width) + "x" + std::to_string(desc.height) + " with " +
                std::to_string(desc.mipLevels) + " mip levels" +
                (result == VK_SUCCESS ? " (at most " + std::to_string(limits.maxExtent.width) + "x" +
                                            std::to_string(limits.maxExtent.height) + ", " +
                                            std::to_string(limits.maxMipLevels) + " levels)"
                                      : std::string()));
    }

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vkCheck(vmaCreateImage(ctx.allocator(), &imageInfo, &allocInfo, &image_, &allocation_, nullptr),
            "vmaCreateImage");

    try {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = viewFormat_;
        viewInfo.subresourceRange = subresourceRange(viewAspect(aspect_), 0, 1);
        vkCheck(vkCreateImageView(ctx.device(), &viewInfo, nullptr, &view_), "vkCreateImageView");

        if (mipLevels_ == 1 && !mutableFormat) {
            sampledView_ = view_;
        } else {
            viewInfo.format = format_;
            viewInfo.subresourceRange = subresourceRange(viewAspect(aspect_));
            // The sampled view of a two-format image leaves out the storage usage its format may lack.
            VkImageViewUsageCreateInfo viewUsage{VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};
            viewUsage.usage = desc.usage & ~VkImageUsageFlags{VK_IMAGE_USAGE_STORAGE_BIT};
            if (mutableFormat && viewUsage.usage != 0)
                viewInfo.pNext = &viewUsage;
            vkCheck(vkCreateImageView(ctx.device(), &viewInfo, nullptr, &sampledView_), "vkCreateImageView");
        }
    } catch (...) {
        destroy();
        throw;
    }

    vmaSetAllocationName(ctx.allocator(), allocation_, desc.name);
    ctx.setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(image_), desc.name);
    ctx.setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(view_), desc.name);
    if (sampledView_ != view_)
        ctx.setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(sampledView_),
                          (std::string(desc.name) + " (sampled)").c_str());
}

Image::~Image() {
    destroy();
}

Image::Image(Image&& other) noexcept {
    *this = std::move(other);
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        destroy();
        ctx_ = std::exchange(other.ctx_, nullptr);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE);
        sampledView_ = std::exchange(other.sampledView_, VK_NULL_HANDLE);
        extent_ = std::exchange(other.extent_, {0, 0});
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        viewFormat_ = std::exchange(other.viewFormat_, VK_FORMAT_UNDEFINED);
        aspect_ = std::exchange(other.aspect_, VK_IMAGE_ASPECT_COLOR_BIT);
        usage_ = std::exchange(other.usage_, 0);
        mipLevels_ = std::exchange(other.mipLevels_, 1);
        mipGenerations_ = std::exchange(other.mipGenerations_, 0);
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
    if (sampledView_ && sampledView_ != view_)
        vkDestroyImageView(ctx_->device(), sampledView_, nullptr);
    if (view_)
        vkDestroyImageView(ctx_->device(), view_, nullptr);
    if (image_)
        vmaDestroyImage(ctx_->allocator(), image_, allocation_);
    sampledView_ = VK_NULL_HANDLE;
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
        imageBarrier2(cmd, image_, layout_, layout_, writeStage_, writeAccess_, dstStage, dstAccess,
                      subresourceRange(aspect_));
        readStages_ |= dstStage;
        readAccess_ |= dstAccess;
        return true;
    }

    // A write, or a layout change (which writes the image): wait for the last write and make it available,
    // and wait for every read since so none of them sees the new contents (write after read).
    imageBarrier2(cmd, image_, layout_, newLayout, writeStage_ | readStages_, writeAccess_, dstStage,
                  dstAccess, subresourceRange(aspect_));
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

void Image::generateMips(VkCommandBuffer cmd, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    constexpr VkFormatFeatureFlags kBlit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    constexpr VkImageUsageFlags kTransfer = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (mipLevels_ == 1) { // nothing to blit, so no blit support needed
        ++mipGenerations_;
        transition(cmd, VK_IMAGE_LAYOUT_GENERAL, dstStage, dstAccess);
        return;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(ctx_->physicalDevice(), format_, &properties);
    if ((properties.optimalTilingFeatures & kBlit) != kBlit)
        throw std::logic_error("Image::generateMips(): format " + std::to_string(static_cast<int>(format_)) +
                               " cannot be blitted with a linear filter");
    if ((usage_ & kTransfer) != kTransfer)
        throw std::logic_error("Image::generateMips() needs TRANSFER_SRC and TRANSFER_DST usage");
    ++mipGenerations_;

    // Every level into GENERAL after everything recorded before: level 0's picture is made visible to the
    // first blit's read, and the other levels' earlier reads are done before the blits overwrite them.
    transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
               VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
    std::int32_t width = static_cast<std::int32_t>(extent_.width);
    std::int32_t height = static_cast<std::int32_t>(extent_.height);
    for (std::uint32_t level = 1; level < mipLevels_; ++level) {
        // The level the blit reads was written by the blit before it (level 0 by the transition above).
        if (level > 1)
            imageBarrier2(cmd, image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                          VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                          subresourceRange(aspect_, level - 1, 1));
        const std::int32_t nextWidth = std::max(width / 2, 1);
        const std::int32_t nextHeight = std::max(height / 2, 1);
        VkImageBlit2 region{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
        region.srcSubresource = {aspect_, level - 1, 0, 1};
        region.srcOffsets[1] = {width, height, 1};
        region.dstSubresource = {aspect_, level, 0, 1};
        region.dstOffsets[1] = {nextWidth, nextHeight, 1};
        VkBlitImageInfo2 blit{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
        blit.srcImage = image_;
        blit.srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        blit.dstImage = image_;
        blit.dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        blit.regionCount = 1;
        blit.pRegions = &region;
        blit.filter = VK_FILTER_LINEAR;
        vkCmdBlitImage2(cmd, &blit);
        width = nextWidth;
        height = nextHeight;
    }

    // The tracked state of the whole image now: the blits' writes (levels 1 and up) are the last write, not
    // yet available; their reads ran in the same stage, so waiting for that stage covers them too. Then the
    // requested use waits for it, which makes every level visible to (dstStage, dstAccess).
    layout_ = VK_IMAGE_LAYOUT_GENERAL;
    writeStage_ = VK_PIPELINE_STAGE_2_BLIT_BIT;
    writeAccess_ = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    readStages_ = VK_PIPELINE_STAGE_2_NONE;
    readAccess_ = VK_ACCESS_2_NONE;
    transition(cmd, VK_IMAGE_LAYOUT_GENERAL, dstStage, dstAccess);
}

} // namespace evo::gfx
