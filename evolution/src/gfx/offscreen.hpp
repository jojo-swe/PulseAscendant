#pragma once

#include "gfx/context.hpp"
#include "gfx/fill_pass.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "show/timeline.hpp"

#include <volk.h>

#include <cstdint>
#include <vector>

namespace evo::gfx {

// A read-back frame: top-down rows, 3 bytes (RGB) per pixel.
struct Frame {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgb;
};

// A headless fixture for --render-stream, --capture-dir and the tests: renders the fill.comp test pattern for
// a timeline frame into an RGBA8 image and reads it back, on its own FrameRing. Every render() submits and
// waits for that submit, so readback() always sees that frame. The window does not use it: it records the
// passes into its own ring and presents without reading back.
class Offscreen {
public:
    Offscreen(Context& ctx, int width, int height);
    ~Offscreen() = default;
    Offscreen(const Offscreen&) = delete;
    Offscreen& operator=(const Offscreen&) = delete;

    // Records the fill pass (dispatch ceil(w/8) x ceil(h/8)) and the readback copy, submits and waits. The
    // pattern shows the frame's chapter, era, wipe, kick and beat phase.
    void render(const show::TimelineFrame& frame);
    // render(show::sampleTimeline(seconds)).
    void render(double seconds);
    // RGBA -> RGB conversion on the host of the last rendered frame.
    Frame readback() const;

    VkImage image() const { return image_.handle(); }
    // The image's layout right after render(): TRANSFER_SRC_OPTIMAL, left by the readback copy.
    VkImageLayout layout() const { return image_.layout(); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_ = 0;
    int height_ = 0;
    Image image_;
    Readback readback_;
    FillPass fill_;
    FrameRing ring_; // last: destroyed first, after waiting for its submits
};

} // namespace evo::gfx
