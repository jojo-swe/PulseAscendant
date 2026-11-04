#include "gfx/shader.hpp"

#include "gfx/vk_check.hpp"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace evo::gfx {

std::vector<std::uint32_t> loadSpirv(const std::filesystem::path& spv) {
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
    return code;
}

VkShaderModule createShaderModule(VkDevice device, std::span<const std::uint32_t> words,
                                  const std::filesystem::path& name) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = words.size_bytes();
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device, &info, nullptr, &module),
            ("vkCreateShaderModule " + name.string()).c_str());
    return module;
}

VkShaderModule loadShaderModule(VkDevice device, const std::filesystem::path& spv) {
    const std::vector<std::uint32_t> code = loadSpirv(spv);
    return createShaderModule(device, code, spv);
}

bool spirvHasOpcode(std::span<const std::uint32_t> words, std::uint16_t opcode) {
    constexpr std::uint32_t kMagic = 0x07230203u;
    constexpr std::size_t kHeaderWords = 5;
    if (words.size() < kHeaderWords || words[0] != kMagic)
        throw std::runtime_error("spirvHasOpcode: not a SPIR-V module");
    for (std::size_t i = kHeaderWords; i < words.size();) {
        const std::uint32_t wordCount = words[i] >> 16;
        if (wordCount == 0 || i + wordCount > words.size())
            throw std::runtime_error("spirvHasOpcode: malformed instruction at word " + std::to_string(i));
        if ((words[i] & 0xFFFFu) == opcode)
            return true;
        i += wordCount;
    }
    return false;
}

} // namespace evo::gfx
