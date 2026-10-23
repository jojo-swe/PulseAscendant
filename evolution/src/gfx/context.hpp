#pragma once

#include <volk.h>

#include <vk_mem_alloc.h>

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace evo::gfx {

struct ContextOptions {
    bool validation = false;                        // enable VK_LAYER_KHRONOS_validation if present
    std::vector<const char*> instanceExtensions;    // from SDL when windowed; empty headless
    std::vector<const char*> extraDeviceExtensions; // appended to the required list (swapchain, tests)
    PFN_vkGetInstanceProcAddr loader = nullptr;     // nullptr -> volkInitialize()
};

// Thrown when no device offers every required extension and feature; names each missing one.
struct MissingRequirements : std::runtime_error {
    explicit MissingRequirements(std::vector<std::string> missingNames);
    std::vector<std::string> missing;
};

class Context {
public:
    static Context create(const ContextOptions& options);

    ~Context();
    Context(Context&& other) noexcept;
    Context& operator=(Context&& other) noexcept;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VkQueue queue() const { return queue_; }
    uint32_t queueFamily() const { return queueFamily_; }
    VmaAllocator allocator() const { return allocator_; }
    VkCommandPool commandPool() const { return commandPool_; }

    std::string deviceName() const;
    uint32_t apiVersion() const;
    std::string deviceInfoJson() const;

    // Records on a fresh primary command buffer, submits, waits idle, frees.
    void immediate(const std::function<void(VkCommandBuffer)>& record) const;

private:
    Context() = default;
    void destroy() noexcept;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    bool validation_ = false;
};

} // namespace evo::gfx
