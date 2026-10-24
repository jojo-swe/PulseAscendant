#pragma once

#include "gfx/context.hpp"

#include <volk.h>

#include <cstdint>

namespace evo::gfx {

// One region of a shader binding table: `offset` bytes from the table's start, records `stride` apart,
// `size` bytes in all (a whole number of records, rounded up to the base alignment).
struct SbtRegion {
    VkDeviceSize offset;
    VkDeviceSize stride;
    VkDeviceSize size;
};

// The raygen, miss and hit regions of one table, in that order, and the bytes the table needs.
struct SbtLayout {
    SbtRegion raygen, miss, hit;
    VkDeviceSize totalBytes;
};

// The layout of a table with one raygen record, `missCount` miss records and `hitGroupCount` hit records, for
// a table whose start is aligned to shaderGroupBaseAlignment. With a = alignUp(handleSize, handleAlignment):
// raygen at offset 0 with stride = size = alignUp(a, baseAlignment) (vkCmdTraceRaysKHR wants the raygen
// size equal to its stride); miss at raygen.size with stride a and size alignUp(missCount * a,
// baseAlignment); hit right after miss by the same rule. So every region starts on the base alignment, and
// the miss and hit strides are multiples of the handle alignment. Pure: a layout for any device's properties
// (lavapipe 32/16/32 and RTX 32/32/64 differ; Review Focus 4).
//
// Throws std::invalid_argument for a handle size of 0, a handle or base alignment that is not a power of two,
// a base alignment below the handle alignment, or a record stride a over maxShaderGroupStride.
SbtLayout computeSbtLayout(const RayTracingProperties& properties, std::uint32_t missCount,
                           std::uint32_t hitGroupCount);

} // namespace evo::gfx
