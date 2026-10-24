#include "gfx/sbt.hpp"

#include <stdexcept>
#include <string>

namespace evo::gfx {

namespace {

bool isPowerOfTwo(std::uint32_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

// `alignment` is a power of two.
VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

SbtLayout computeSbtLayout(const RayTracingProperties& properties, std::uint32_t missCount,
                           std::uint32_t hitGroupCount) {
    const std::uint32_t handleSize = properties.shaderGroupHandleSize;
    const std::uint32_t handleAlignment = properties.shaderGroupHandleAlignment;
    const std::uint32_t baseAlignment = properties.shaderGroupBaseAlignment;
    if (handleSize == 0)
        throw std::invalid_argument("computeSbtLayout: shader group handle size 0");
    if (!isPowerOfTwo(handleAlignment) || !isPowerOfTwo(baseAlignment))
        throw std::invalid_argument("computeSbtLayout: handle alignment " + std::to_string(handleAlignment) +
                                    " and base alignment " + std::to_string(baseAlignment) +
                                    " must be powers of two");
    if (baseAlignment < handleAlignment)
        throw std::invalid_argument("computeSbtLayout: base alignment " + std::to_string(baseAlignment) +
                                    " below the handle alignment " + std::to_string(handleAlignment));

    const VkDeviceSize record = alignUp(handleSize, handleAlignment);
    if (record > properties.maxShaderGroupStride)
        throw std::invalid_argument("computeSbtLayout: record stride " + std::to_string(record) +
                                    " over maxShaderGroupStride " +
                                    std::to_string(properties.maxShaderGroupStride));

    SbtLayout layout{};
    const VkDeviceSize raygenSize = alignUp(record, baseAlignment);
    layout.raygen = {0, raygenSize, raygenSize};
    layout.miss = {layout.raygen.offset + layout.raygen.size, record,
                   alignUp(VkDeviceSize{missCount} * record, baseAlignment)};
    layout.hit = {layout.miss.offset + layout.miss.size, record,
                  alignUp(VkDeviceSize{hitGroupCount} * record, baseAlignment)};
    layout.totalBytes = layout.hit.offset + layout.hit.size;
    return layout;
}

} // namespace evo::gfx
