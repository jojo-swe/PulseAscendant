#pragma once

#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"

#include <volk.h>

namespace evo::post {

// The one tonemap of the 2026 picture (shaders/post/tonemap.comp): HDR linear light -> display-encoded RGBA8,
// exactly as tonemapReference (tonemap_reference.hpp) describes. The display encoding happens here and
// nowhere else: the LDR image holds sRGB-encoded values in a UNORM image, like the retro side, and the
// window and the exports show its bytes as they are.
class Tonemap {
public:
    Tonemap(gfx::Context& ctx, gfx::FrameRing& ring); // loads shaderDir()/post_tonemap.comp.spv

    // One dispatch over the image: hdr (R16G16B16A16_SFLOAT, storage) is read, ldr (R8G8B8A8_UNORM, storage,
    // the same size) written; both end in GENERAL. Throws std::invalid_argument, recording nothing, for
    // another format or size. Between ring.begin() and ring.submit() only.
    void record(VkCommandBuffer cmd, gfx::Image& hdr, gfx::Image& ldr, float exposureEV);

private:
    gfx::FrameRing& ring_;
    gfx::ComputePipeline pipeline_;
};

} // namespace evo::post
