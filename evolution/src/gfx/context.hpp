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
    bool validation = false;                        // enable VK_LAYER_KHRONOS_validation if present, and
                                                    // route its messages to stderr via VK_EXT_debug_utils
    std::vector<const char*> instanceExtensions;    // from SDL when windowed; empty headless
    std::vector<const char*> extraDeviceExtensions; // appended to the required list (swapchain, tests)
    PFN_vkGetInstanceProcAddr loader = nullptr;     // nullptr -> volkInitialize()
};

// Thrown when the machine cannot run EVOLUTION: no Vulkan loader, no Vulkan 1.3 driver, no physical device,
// or no device offering every required extension and feature. Names each missing one.
struct MissingRequirements : std::runtime_error {
    explicit MissingRequirements(std::vector<std::string> missingNames);
    std::vector<std::string> missing;
};

// Every validation warning and error reported in this process so far, including those from destroying a
// Context. Zero unless a Context was created with validation and the layer was present.
unsigned validationMessagesReported();

// The Vulkan instance, device, queue and allocator. Only one may be alive at a time, because volk keeps the
// function pointers it loads for the instance and device in globals.
//
// Test-only: when the environment variable EVO_TEST_REQUIRE_DEVICE_EXTENSION is set, create() also
// requires the device extension it names, so a test can make any machine look unsupported.
class Context {
public:
    // Throws MissingRequirements when the machine cannot run EVOLUTION, and std::logic_error while another
    // Context is alive.
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

    // Whether the validation layer was requested and found.
    bool validationEnabled() const { return validation_; }
    // Validation warnings and errors reported since this Context was created.
    unsigned validationMessageCount() const;

    // Records on a fresh primary command buffer, submits, waits idle, frees. For one-off setup work; frames
    // go through a FrameRing.
    void immediate(const std::function<void(VkCommandBuffer)>& record) const;

    // Shared samplers with clamp-to-edge addressing and no mipmapping: nearest (the retro pixels) and linear.
    VkSampler nearestSampler() const { return nearestSampler_; }
    VkSampler linearSampler() const { return linearSampler_; }

    // Names an object for the validation layer's messages and for debuggers. A no-op without
    // VK_EXT_debug_utils (validation off) or for an empty name.
    void setObjectName(VkObjectType type, std::uint64_t handle, const char* name) const;

private:
    Context() = default;
    void destroy() noexcept;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkSampler nearestSampler_ = VK_NULL_HANDLE;
    VkSampler linearSampler_ = VK_NULL_HANDLE;
    bool validation_ = false;
    unsigned validationMessagesBefore_ = 0; // validationMessagesReported() when this Context was created
    bool live_ = false;                     // holds the one-live-Context slot
};

} // namespace evo::gfx
