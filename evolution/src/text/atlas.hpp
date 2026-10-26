#pragma once

#include "gfx/context.hpp"
#include "gfx/image.hpp"

#include <cstdint>
#include <vector>

namespace evo::text {

// The font as an R8_UNORM image: 16 glyphs per row, 6 rows, code 32 in the top-left cell and code c in cell
// ((c - 32) % 16, (c - 32) / 16); a set pixel is 1.0, every other 0.
inline constexpr std::uint32_t kAtlasColumns = 16;
inline constexpr std::uint32_t kAtlasWidth = kAtlasColumns * 8; // 128
inline constexpr std::uint32_t kAtlasHeight = 6 * 8;            // 48

// The atlas pixels on the CPU, top-down rows of kAtlasWidth bytes, 255 for a set pixel.
std::vector<std::uint8_t> atlasPixels();

// The atlas on the GPU, uploaded once at construction through a host-visible staging buffer (a one-off
// submit that waits; startup work, not per frame) and left in SHADER_READ_ONLY_OPTIMAL for sampling with
// the nearest sampler.
class Atlas {
public:
    explicit Atlas(gfx::Context& ctx);

    gfx::Image& image() { return image_; }

private:
    gfx::Image image_;
};

} // namespace evo::text
