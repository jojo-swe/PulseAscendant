#pragma once

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace evo::gfx {

// Pure: clamps a desired size into the surface capabilities. A currentExtent of UINT32_MAX means the
// surface lets the swapchain pick its size; any other currentExtent is the size. A 0x0 result means the
// window is minimised: the caller skips the frame.
VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, int desiredWidth, int desiredHeight);

// Pure: the surface format the swapchain uses, from the surface's list. The renderer's output is linear
// R8G8B8A8_UNORM bytes that are already display-encoded, so the blit must copy them unchanged into a UNORM
// image: B8G8R8A8_UNORM first, then R8G8B8A8_UNORM, then any other UNORM colour format (A2B10G10R10, 16-bit
// and packed ones), and only then the first entry (an sRGB format, which would encode the picture twice).
// Throws std::runtime_error for an empty list.
VkSurfaceFormatKHR chooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats);

// FIFO presentation of a finished image in the format chooseSurfaceFormat() picks; asks for 3 images where
// the surface allows it. Each frame blits the source image into the acquired swapchain image, so the renderer
// never draws into the swapchain directly. The frames themselves belong to a FrameRing: the swapchain only
// adds the acquire semaphores (one per ring slot) and the present semaphores (one per swapchain image) to the
// ring's submits.
class Swapchain {
public:
    Swapchain(Context& ctx, VkSurfaceKHR surface, int width, int height);
    ~Swapchain();
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // Rebuilds for the given drawable size. Returns false (and keeps the old swapchain) when the
    // surface is 0x0.
    bool recreate(int width, int height);

    // Ends the ring's frame: acquires a swapchain image, records into cmd (the ring's current command buffer)
    // a blit of `src` to it with a linear filter (moving src to TRANSFER_SRC_OPTIMAL), submits the frame
    // through the ring, waiting for the acquire and signalling the present, and presents. Always submits,
    // also when nothing could be acquired, so every ring.begin() is matched. Returns false when the swapchain
    // is out of date or suboptimal (the caller recreates).
    bool present(FrameRing& ring, VkCommandBuffer cmd, Image& src);

    VkExtent2D extent() const { return extent_; }

private:
    void createImageAvailable();
    void createRenderFinished(std::size_t count);
    void destroyRenderFinished() noexcept;
    void destroy() noexcept;

    Context& ctx_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{0, 0};
    std::vector<VkImage> images_;
    // One per swapchain image: a present's wait semaphore may only be reused once that image is acquired
    // again, which the frame ring's wait for an earlier submit does not guarantee.
    std::vector<VkSemaphore> renderFinished_;
    // One per ring slot: FrameRing::begin() waits for the slot's previous submit, which waited on it.
    std::array<VkSemaphore, FrameRing::kFramesInFlight> imageAvailable_{};
};

} // namespace evo::gfx
