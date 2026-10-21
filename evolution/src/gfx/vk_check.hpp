#pragma once

#include <volk.h>

#include <stdexcept>
#include <string>

namespace evo::gfx {

// Turns a failed Vulkan call into an exception naming the call and the raw result code.
inline void vkCheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(what) + ": VkResult " + std::to_string(static_cast<int>(result)));
}

} // namespace evo::gfx
