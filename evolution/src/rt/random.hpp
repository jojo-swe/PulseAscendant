#pragma once

// The CPU mirror of shaders/rt/random.glsl's hash, for what the CPU must compute exactly as the GPU does (the
// path tracer's frame seed, the tonemap's dither reference). material_test checks the two agree.

#include <cstdint>

namespace evo::rt {

// The PCG-RXS-M-XS 32-bit hash (Jarzynski and Olano, "Hash Functions for GPU Rendering", 2020), bit for bit.
constexpr std::uint32_t pcgHash(std::uint32_t v) {
    const std::uint32_t state = v * 747796405u + 2891336453u;
    const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

} // namespace evo::rt
