#pragma once

#include <volk.h>

#include <cstdint>

namespace evo::gfx {

// Every mip level and array layer of a colour image.
inline constexpr VkImageSubresourceRange kColorRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0,
                                                     VK_REMAINING_ARRAY_LAYERS};

// The aspects of a format: DEPTH for the depth formats, DEPTH | STENCIL for the combined ones, STENCIL for
// S8_UINT, COLOR for every other format.
inline constexpr VkImageAspectFlags formatAspect(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    case VK_FORMAT_S8_UINT:
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

// `levels` mip levels from `baseLevel` (all the rest by default) and every array layer, of the given aspects.
inline constexpr VkImageSubresourceRange subresourceRange(VkImageAspectFlags aspect,
                                                          std::uint32_t baseLevel = 0,
                                                          std::uint32_t levels = VK_REMAINING_MIP_LEVELS) {
    return {aspect, baseLevel, levels, 0, VK_REMAINING_ARRAY_LAYERS};
}

// One synchronization2 image barrier: waits for (srcStage, srcAccess), moves the image from `from` to `to`
// and makes it available to (dstStage, dstAccess). Same queue family on both sides.
inline void imageBarrier2(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                          VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                          VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                          const VkImageSubresourceRange& range = kColorRange) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

// One synchronization2 barrier over a whole buffer.
inline void bufferBarrier2(VkCommandBuffer cmd, VkBuffer buffer, VkPipelineStageFlags2 srcStage,
                           VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                           VkAccessFlags2 dstAccess) {
    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace evo::gfx
