#include "gfx/shader.hpp"

#include "gfx/vk_check.hpp"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace evo::gfx {

VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& spv) {
    std::ifstream file(spv, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("cannot open shader " + spv.string());

    const auto size = static_cast<std::size_t>(file.tellg());
    if (size == 0 || size % sizeof(std::uint32_t) != 0)
        throw std::runtime_error("not a SPIR-V file (size " + std::to_string(size) + "): " + spv.string());

    std::vector<std::uint32_t> code(size / sizeof(std::uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("cannot read shader " + spv.string());

    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device, &info, nullptr, &module),
            ("vkCreateShaderModule " + spv.string()).c_str());
    return module;
}

} // namespace evo::gfx
