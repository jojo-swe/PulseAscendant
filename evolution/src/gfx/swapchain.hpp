#pragma once

#include "gfx/context.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <vector>

namespace evo::gfx {

// Pure: clamps a desired size into the surface capabilities. A currentExtent of UINT32_MAX means the
// surface lets the swapchain pick its size; any other currentExtent is the size. A 0x0 result means the
// window is minimised: the caller skips the frame.
VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, int desiredWidth, int desiredHeight);

// FIFO presentation of a finished image. Prefers B8G8R8A8_UNORM (else the surface's first format) and asks
// for 3 images where the surface allows it. Each frame blits the source image into the acquired swapchain
// image, so the renderer never draws into the swapchain directly.
class Swapchain {
public:
    Swapchain(Context& ctx, VkSurfaceKHR surface, int width, int height);
    ~Swapchain();
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // Rebuilds for the given drawable size. Returns false (and keeps the old swapchain) when the
    // surface is 0x0.
    bool recreate(int width, int height);

    // Acquires, blits `src` (in TRANSFER_SRC_OPTIMAL) to the swapchain image with a linear filter, presents.
    // Returns false when the swapchain is out of date or suboptimal (the caller recreates).
    bool present(VkImage src, int srcWidth, int srcHeight);

    VkExtent2D extent() const { return extent_; }

private:
    static constexpr std::uint32_t kFramesInFlight = 2;

    // Per frame in flight: the fence guards the command buffer and the acquire semaphore.
    struct FrameSync {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    void createFrameSync();
    void createRenderFinished(std::size_t count);
    void destroyRenderFinished() noexcept;
    void destroy() noexcept;

    Context& ctx_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{0, 0};
    std::vector<VkImage> images_;
    // One per swapchain image: a present's wait semaphore may only be reused once that image is acquired
    // again, which the per-frame fence does not guarantee.
    std::vector<VkSemaphore> renderFinished_;
    std::array<FrameSync, kFramesInFlight> frames_{};
    std::uint32_t frameIndex_ = 0;
};

} // namespace evo::gfx
