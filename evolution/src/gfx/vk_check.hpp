#pragma once

#include <volk.h>

#include <stdexcept>
#include <string>

namespace evo::gfx {

// The name of a VkResult, e.g. "VK_ERROR_DEVICE_LOST". Kept here because the pinned Vulkan-Headers do not
// ship vk_enum_string_helper.h, and a system copy may not match their version.
inline const char* resultName(VkResult result) {
    switch (result) {
#define EVO_RESULT_NAME(name)                                                                                \
    case name:                                                                                               \
        return #name;
        EVO_RESULT_NAME(VK_SUCCESS)
        EVO_RESULT_NAME(VK_NOT_READY)
        EVO_RESULT_NAME(VK_TIMEOUT)
        EVO_RESULT_NAME(VK_EVENT_SET)
        EVO_RESULT_NAME(VK_EVENT_RESET)
        EVO_RESULT_NAME(VK_INCOMPLETE)
        EVO_RESULT_NAME(VK_ERROR_OUT_OF_HOST_MEMORY)
        EVO_RESULT_NAME(VK_ERROR_OUT_OF_DEVICE_MEMORY)
        EVO_RESULT_NAME(VK_ERROR_INITIALIZATION_FAILED)
        EVO_RESULT_NAME(VK_ERROR_DEVICE_LOST)
        EVO_RESULT_NAME(VK_ERROR_MEMORY_MAP_FAILED)
        EVO_RESULT_NAME(VK_ERROR_LAYER_NOT_PRESENT)
        EVO_RESULT_NAME(VK_ERROR_EXTENSION_NOT_PRESENT)
        EVO_RESULT_NAME(VK_ERROR_FEATURE_NOT_PRESENT)
        EVO_RESULT_NAME(VK_ERROR_INCOMPATIBLE_DRIVER)
        EVO_RESULT_NAME(VK_ERROR_TOO_MANY_OBJECTS)
        EVO_RESULT_NAME(VK_ERROR_FORMAT_NOT_SUPPORTED)
        EVO_RESULT_NAME(VK_ERROR_FRAGMENTED_POOL)
        EVO_RESULT_NAME(VK_ERROR_UNKNOWN)
        EVO_RESULT_NAME(VK_ERROR_OUT_OF_POOL_MEMORY)
        EVO_RESULT_NAME(VK_ERROR_INVALID_EXTERNAL_HANDLE)
        EVO_RESULT_NAME(VK_ERROR_FRAGMENTATION)
        EVO_RESULT_NAME(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS)
        EVO_RESULT_NAME(VK_PIPELINE_COMPILE_REQUIRED)
        EVO_RESULT_NAME(VK_ERROR_SURFACE_LOST_KHR)
        EVO_RESULT_NAME(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
        EVO_RESULT_NAME(VK_SUBOPTIMAL_KHR)
        EVO_RESULT_NAME(VK_ERROR_OUT_OF_DATE_KHR)
        EVO_RESULT_NAME(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR)
        EVO_RESULT_NAME(VK_ERROR_VALIDATION_FAILED_EXT)
        EVO_RESULT_NAME(VK_THREAD_IDLE_KHR)
        EVO_RESULT_NAME(VK_THREAD_DONE_KHR)
        EVO_RESULT_NAME(VK_OPERATION_DEFERRED_KHR)
        EVO_RESULT_NAME(VK_OPERATION_NOT_DEFERRED_KHR)
#undef EVO_RESULT_NAME
    default:
        return "unknown VkResult";
    }
}

// Turns a failed Vulkan call into an exception naming the call and the result, e.g.
// "vkCreateDevice: VK_ERROR_DEVICE_LOST (-4)".
inline void vkCheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(what) + ": " + resultName(result) + " (" +
                                 std::to_string(static_cast<int>(result)) + ")");
}

} // namespace evo::gfx
