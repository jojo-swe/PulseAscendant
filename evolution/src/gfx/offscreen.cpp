#include "gfx/offscreen.hpp"

#include <stdexcept>
#include <string>

namespace evo::gfx {

namespace {

// Checked before any member is built from the size.
ImageDesc targetDesc(int width, int height) {
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("Offscreen size must be positive, got " + std::to_string(width) + "x" +
                                    std::to_string(height));
    return {static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            1,
            "offscreen"};
}

} // namespace

Offscreen::Offscreen(Context& ctx, int width, int height)
    : width_(width), height_(height), image_(ctx, targetDesc(width, height)),
      readback_(ctx, image_.extent().width, image_.extent().height), fill_(ctx), ring_(ctx) {}

void Offscreen::render(double seconds) {
    render(show::sampleTimeline(seconds));
}

void Offscreen::render(const show::TimelineFrame& frame) {
    VkCommandBuffer cmd = ring_.begin();
    fill_.record(cmd, ring_, image_, frame);
    readback_.record(cmd, image_);
    ring_.wait(ring_.submit());
}

Frame Offscreen::readback() const {
    return {width_, height_, readback_.rgb()};
}

} // namespace evo::gfx
