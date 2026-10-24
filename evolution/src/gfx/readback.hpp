#pragma once

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/image.hpp"

#include <volk.h>

#include <cstdint>
#include <vector>

namespace evo::gfx {

// Copies mip level 0 of an R8G8B8A8_UNORM (or _SRGB: the bytes as stored) image of the given size into a
// host-visible buffer, for export only
// (--render-stream, --capture-dir, tests); the window never reads frames back.
class Readback {
public:
    Readback(Context& ctx, std::uint32_t width, std::uint32_t height); // a width * height * 4 byte buffer

    // Moves src to TRANSFER_SRC_OPTIMAL, copies it and makes the bytes visible to the host once the submit
    // completes. Throws std::invalid_argument (recording nothing) for another size or format.
    void record(VkCommandBuffer cmd, Image& src);
    // The copied pixels as top-down RGB rows. Call only after the submit that recorded the copy completed
    // (FrameRing::wait(value) on the value its submit() returned).
    std::vector<std::uint8_t> rgb() const;

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    Buffer buffer_;
};

} // namespace evo::gfx
