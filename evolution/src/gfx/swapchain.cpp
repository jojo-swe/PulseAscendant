#include "gfx/swapchain.hpp"

#include "gfx/vk_check.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace evo::gfx {

namespace {

constexpr VkFormat kPreferredFormat = VK_FORMAT_B8G8R8A8_UNORM;
constexpr std::uint32_t kPreferredImageCount = 3;
const VkImageSubresourceRange kColorRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

VkSurfaceFormatKHR chooseFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formats.empty())
        throw std::runtime_error("the window surface offers no formats");

    for (const auto& format : formats) {
        if (format.format == kPreferredFormat)
            return format;
    }
    return formats.front();
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess) {
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
    barrier.subresourceRange = kColorRange;

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, int desiredWidth, int desiredHeight) {
    if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
        return caps.currentExtent; // the surface dictates the size; 0x0 while minimised

    if (desiredWidth <= 0 || desiredHeight <= 0)
        return {0, 0};
    VkExtent2D extent{std::clamp(static_cast<std::uint32_t>(desiredWidth), caps.minImageExtent.width,
                                 caps.maxImageExtent.width),
                      std::clamp(static_cast<std::uint32_t>(desiredHeight), caps.minImageExtent.height,
                                 caps.maxImageExtent.height)};
    // Some platforms report a 0x0 maximum while the window is minimised.
    if (extent.width == 0 || extent.height == 0)
        return {0, 0};
    return extent;
}

Swapchain::Swapchain(Context& ctx, VkSurfaceKHR surface, int width, int height)
    : ctx_(ctx), surface_(surface) {
    try {
        createFrameSync();
        recreate(width, height); // a minimised window starts with no swapchain; present() then returns false
    } catch (...) {
        destroy();
        throw;
    }
}

Swapchain::~Swapchain() {
    destroy();
}

void Swapchain::createFrameSync() {
    const VkDevice device = ctx_.device();
    for (auto& frame : frames_) {
        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = ctx_.commandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkCheck(vkAllocateCommandBuffers(device, &allocInfo, &frame.cmd),
                "vkAllocateCommandBuffers(present)");

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable),
                "vkCreateSemaphore(imageAvailable)");

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // the first wait on each frame returns at once
        vkCheck(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight), "vkCreateFence(inFlight)");
    }
}

void Swapchain::createRenderFinished(std::size_t count) {
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderFinished_.assign(count, VK_NULL_HANDLE);
    for (auto& semaphore : renderFinished_)
        vkCheck(vkCreateSemaphore(ctx_.device(), &semaphoreInfo, nullptr, &semaphore),
                "vkCreateSemaphore(renderFinished)");
}

void Swapchain::destroyRenderFinished() noexcept {
    for (VkSemaphore semaphore : renderFinished_) {
        if (semaphore)
            vkDestroySemaphore(ctx_.device(), semaphore, nullptr);
    }
    renderFinished_.clear();
}

void Swapchain::destroy() noexcept {
    const VkDevice device = ctx_.device();
    if (!device)
        return;
    vkDeviceWaitIdle(device);
    destroyRenderFinished();
    for (auto& frame : frames_) {
        if (frame.inFlight)
            vkDestroyFence(device, frame.inFlight, nullptr);
        if (frame.imageAvailable)
            vkDestroySemaphore(device, frame.imageAvailable, nullptr);
        if (frame.cmd)
            vkFreeCommandBuffers(device, ctx_.commandPool(), 1, &frame.cmd);
        frame = {};
    }
    if (swapchain_)
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    images_.clear();
    extent_ = {0, 0};
}

bool Swapchain::recreate(int width, int height) {
    const VkPhysicalDevice physicalDevice = ctx_.physicalDevice();
    const VkDevice device = ctx_.device();

    VkSurfaceCapabilitiesKHR caps{};
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface_, &caps),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    const VkExtent2D extent = chooseExtent(caps, width, height);
    if (extent.width == 0 || extent.height == 0)
        return false;

    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        throw std::runtime_error("the window surface does not accept transfer writes (blits)");

    const VkSurfaceFormatKHR format = chooseFormat(physicalDevice, surface_);
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format.format, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
        throw std::runtime_error("the swapchain format " + std::to_string(static_cast<int>(format.format)) +
                                 " cannot be a blit destination");

    std::uint32_t imageCount = std::max(caps.minImageCount, kPreferredImageCount);
    if (caps.maxImageCount > 0) // 0 means no upper limit
        imageCount = std::min(imageCount, caps.maxImageCount);

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & compositeAlpha)) {
        for (auto candidate : {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                               VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
            if (caps.supportedCompositeAlpha & candidate) {
                compositeAlpha = candidate;
                break;
            }
        }
    }

    // Nothing may still be using the old images or semaphores once they are destroyed.
    vkCheck(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = compositeAlpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always supported; paces the loop to the display
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    vkCheck(vkCreateSwapchainKHR(device, &info, nullptr, &created), "vkCreateSwapchainKHR");
    if (swapchain_)
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
    swapchain_ = created;
    extent_ = extent;

    std::uint32_t count = 0;
    vkCheck(vkGetSwapchainImagesKHR(device, swapchain_, &count, nullptr), "vkGetSwapchainImagesKHR");
    images_.resize(count);
    vkCheck(vkGetSwapchainImagesKHR(device, swapchain_, &count, images_.data()), "vkGetSwapchainImagesKHR");

    destroyRenderFinished();
    createRenderFinished(images_.size());
    return true;
}

bool Swapchain::present(VkImage src, int srcWidth, int srcHeight) {
    if (!swapchain_)
        return false;

    const VkDevice device = ctx_.device();
    FrameSync& frame = frames_[frameIndex_];
    vkCheck(vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    std::uint32_t imageIndex = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device, swapchain_, UINT64_MAX, frame.imageAvailable,
                                                    VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
        return false; // nothing was signalled; the fence stays signalled for the next attempt
    if (acquired != VK_SUBOPTIMAL_KHR)
        vkCheck(acquired, "vkAcquireNextImageKHR");

    // Reset only once a submit that signals the fence is certain to follow.
    vkCheck(vkResetFences(device, 1, &frame.inFlight), "vkResetFences");
    vkCheck(vkResetCommandBuffer(frame.cmd, 0), "vkResetCommandBuffer");

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(frame.cmd, &beginInfo), "vkBeginCommandBuffer(present)");

    const VkImage target = images_[imageIndex];
    // The blit overwrites the whole image, so its previous contents are discarded. The acquire semaphore
    // is waited on at the transfer stage, which this barrier's source scope chains onto.
    imageBarrier(frame.cmd, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {srcWidth, srcHeight, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[1] = {static_cast<std::int32_t>(extent_.width),
                            static_cast<std::int32_t>(extent_.height), 1};
    vkCmdBlitImage(frame.cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);

    // Presentation needs no stage or access mask; the semaphore signal below orders it after the blit.
    imageBarrier(frame.cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    vkCheck(vkEndCommandBuffer(frame.cmd), "vkEndCommandBuffer(present)");

    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = renderFinished_[imageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = frame.cmd;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitInfo;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalInfo;
    vkCheck(vkQueueSubmit2(ctx_.queue(), 1, &submit, frame.inFlight), "vkQueueSubmit2(present)");

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presented = vkQueuePresentKHR(ctx_.queue(), &presentInfo);
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;

    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
        return false;
    vkCheck(presented, "vkQueuePresentKHR");
    return acquired != VK_SUBOPTIMAL_KHR;
}

} // namespace evo::gfx
