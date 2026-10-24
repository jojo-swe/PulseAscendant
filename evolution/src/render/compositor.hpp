#pragma once

#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "show/timeline.hpp"

#include <volk.h>

#include <cstdint>

namespace evo::render {

// Puts the two eras on the output, in two passes at the render resolution:
//   crt.comp        the 320x200 retro picture into a centred 4:3 rectangle of the output, black around it,
//                   with the CRT look or plain nearest-neighbour; the rectangle is the same either way;
//   composite.comp  that screen and the modern image blended by era, wipe, transition and eraCut into the
//                   R8G8B8A8_UNORM output (also usable as a blit and copy source).
class Compositor {
public:
    Compositor(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height);

    // Recreates the images at the new size. The caller makes sure no submitted frame still uses the old ones.
    void resize(std::uint32_t width, std::uint32_t height);
    // Leaves output() in GENERAL.
    void render(VkCommandBuffer cmd, gfx::Image& retro, gfx::Image& modern, const show::TimelineFrame& frame,
                bool crt);

    gfx::Image& output() { return output_; }

private:
    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    gfx::ComputePipeline crt_;
    gfx::ComputePipeline composite_;
    gfx::Image screen_; // the 1993 picture at the output size
    gfx::Image output_;
};

} // namespace evo::render
