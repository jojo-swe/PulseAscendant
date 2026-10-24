#pragma once

#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace evo::gfx {

// The words of a SPIR-V file. Throws std::runtime_error naming the path when the file is missing,
// unreadable or not SPIR-V sized.
std::vector<std::uint32_t> loadSpirv(const std::filesystem::path& spv);

// A shader module of SPIR-V words the caller destroys; `name` is for error messages.
VkShaderModule createShaderModule(VkDevice device, std::span<const std::uint32_t> words,
                                  const std::filesystem::path& name);

// Loads a SPIR-V file into a shader module the caller destroys (loadSpirv + createShaderModule).
VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& spv);

// Whether any instruction of a SPIR-V module has the given opcode (walking the instruction stream after the
// 5-word header). Throws std::runtime_error for a module that is not SPIR-V (wrong magic, an instruction of
// word count 0 or running past the end).
bool spirvHasOpcode(std::span<const std::uint32_t> words, std::uint16_t opcode);

inline constexpr std::uint16_t kSpirvOpTraceRayKHR = 4445;
inline constexpr std::uint16_t kSpirvOpExecuteCallableKHR = 4446;

} // namespace evo::gfx
