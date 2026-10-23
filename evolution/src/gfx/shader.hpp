#pragma once

#include <volk.h>

#include <filesystem>

namespace evo::gfx {

// Loads a SPIR-V file into a shader module the caller destroys. Throws std::runtime_error naming the
// path when the file is missing, unreadable or not SPIR-V sized.
VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& spv);

} // namespace evo::gfx
