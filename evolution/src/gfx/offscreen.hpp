#pragma once

#include "gfx/context.hpp"

#include <volk.h>

#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

namespace evo::gfx {

// A read-back frame: top-down rows, 3 bytes (RGB) per pixel.
struct Frame {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgb;
};

// Renders the fill.comp test pattern into an RGBA8 storage image with a compute dispatch and copies it
// to a host-visible buffer. Every render() waits for the GPU, so readback() always sees that frame.
class Offscreen {
public:
    Offscreen(Context& ctx, int width, int height);
    ~Offscreen();
    Offscreen(const Offscreen&) = delete;
    Offscreen& operator=(const Offscreen&) = delete;

    // Records, submits and waits: barrier -> dispatch ceil(w/8) x ceil(h/8) -> barrier -> copy to buffer.
    void render(double seconds);
    // RGBA -> RGB conversion on the host of the last rendered frame.
    Frame readback() const;

    VkImage image() const { return image_; }
    VkImageLayout layout() const { return layout_; } // TRANSFER_SRC_OPTIMAL after render()
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void createTargets();
    void createPipeline();
    void destroy() noexcept;

    Context& ctx_;
    int width_ = 0;
    int height_ = 0;

    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation imageAllocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    VkBuffer readbackBuffer_ = VK_NULL_HANDLE;
    VmaAllocation readbackAllocation_ = VK_NULL_HANDLE;
    const std::uint8_t* mapped_ = nullptr;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace evo::gfx
