#pragma once

#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "show/timeline.hpp"

#include <volk.h>

namespace evo::gfx {

// The fill.comp test pattern as a pass: one dispatch that writes a timeline frame's chapter, era, wipe, kick
// and beat phase into an R8G8B8A8_UNORM storage image. The renderer of step 2 replaces it.
class FillPass {
public:
    explicit FillPass(Context& ctx); // loads shaderDir()/fill.comp.spv

    // Moves target to GENERAL for the shader's writes and dispatches ceil(w/8) x ceil(h/8) workgroups.
    void record(VkCommandBuffer cmd, FrameRing& ring, Image& target, const show::TimelineFrame& frame);

private:
    ComputePipeline pipeline_;
};

} // namespace evo::gfx
