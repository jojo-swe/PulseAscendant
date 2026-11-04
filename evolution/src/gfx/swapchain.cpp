#include "gfx/swapchain.hpp"

#include "gfx/sync.hpp"
#include "gfx/vk_check.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace evo::gfx {

namespace {

constexpr std::uint32_t kPreferredImageCount = 3;

// The UNORM colour formats a surface may report.
bool isUnormColor(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_R8G8B8_UNORM:
    case VK_FORMAT_B8G8R8_UNORM:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
    case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
    case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
    case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
        return true;
    default:
        return false;
    }
}

VkSurfaceFormatKHR surfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    return chooseSurfaceFormat(formats);
}

} // namespace

VkSurfaceFormatKHR chooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats) {
    if (formats.empty())
        throw std::runtime_error("the window surface offers no formats");
    const auto find = [&](auto matches) {
        return std::find_if(formats.begin(), formats.end(), [&](const auto& f) { return matches(f.format); });
    };
    for (const VkFormat preferred : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM})
        if (auto it = find([&](VkFormat format) { return format == preferred; }); it != formats.end())
            return *it;
    if (auto it = find(isUnormColor); it != formats.end())
        return *it;
    return formats.front();
}

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
        createImageAvailable();
        recreate(width, height); // a minimised window starts with no swapchain; present() then returns false
    } catch (...) {
        destroy();
        throw;
    }
}

Swapchain::~Swapchain() {
    destroy();
}

void Swapchain::createImageAvailable() {
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore& semaphore : imageAvailable_)
        vkCheck(vkCreateSemaphore(ctx_.device(), &semaphoreInfo, nullptr, &semaphore),
                "vkCreateSemaphore(imageAvailable)");
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
    for (VkSemaphore& semaphore : imageAvailable_) {
        if (semaphore)
            vkDestroySemaphore(device, semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
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

    const VkSurfaceFormatKHR format = surfaceFormat(physicalDevice, surface_);
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

bool Swapchain::present(FrameRing& ring, VkCommandBuffer cmd, Image& src) {
    if (!swapchain_) {
        ring.submit(); // minimised since the swapchain was made: the frame's other work still runs
        return false;
    }

    const VkDevice device = ctx_.device();
    // Free to reuse: the ring's begin() waited for this slot's previous submit, the one that waited on it.
    const VkSemaphore imageAvailable = imageAvailable_[ring.slot()];
    std::uint32_t imageIndex = 0;
    const VkResult acquired =
        vkAcquireNextImageKHR(device, swapchain_, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        ring.submit(); // nothing was signalled, so nothing to wait for
        return false;
    }
    if (acquired != VK_SUBOPTIMAL_KHR)
        vkCheck(acquired, "vkAcquireNextImageKHR");

    src.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkImage target = images_[imageIndex];
    // The blit overwrites the whole image, so its previous contents are discarded. The acquire semaphore
    // is waited on at the blit stage, which this barrier's source scope chains onto.
    imageBarrier2(cmd, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_BLIT_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {static_cast<std::int32_t>(src.extent().width),
                            static_cast<std::int32_t>(src.extent().height), 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[1] = {static_cast<std::int32_t>(extent_.width),
                            static_cast<std::int32_t>(extent_.height), 1};
    vkCmdBlitImage(cmd, src.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);

    // Presentation needs no access mask. The destination stage is the one the semaphore below is signalled
    // at, so the signal (and the present that waits for it) is ordered after the layout transition too.
    imageBarrier2(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);

    // The passes before the blit do not wait for the acquire; only the blit does.
    ring.submit(imageAvailable, VK_PIPELINE_STAGE_2_BLIT_BIT, renderFinished_[imageIndex]);

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presented = vkQueuePresentKHR(ctx_.queue(), &presentInfo);

    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
        return false;
    vkCheck(presented, "vkQueuePresentKHR");
    return acquired != VK_SUBOPTIMAL_KHR;
}

} // namespace evo::gfx
