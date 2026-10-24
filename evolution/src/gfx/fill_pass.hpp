#pragma once

#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"

#include <volk.h>

#include <cstdint>

namespace evo::gfx {

// What fill.comp draws: the values of a timeline frame it shows. gfx never samples the timeline; the caller
// (the renderer's modern stage, or a test) fills these in.
struct FillParams {
    float localSeconds = 0; // seconds into the chapter
    float era = 0;          // 0 = 1993 look, 1 = 2026 look
    float kick = 0;         // [0, 1]
    float beatPhase = 0;    // [0, 1)
    float wipe = 0;         // the Wipe edge as a fraction of the width
    std::uint32_t chapter = 0;
    std::uint32_t transition = 0; // a show::Transition value: 1 Portal cross-fades, 3 Wipe splits
};

// What a FillPass writes. Display: the pattern's display-encoded colours into an R8G8B8A8_UNORM image
// (fill.comp). Hdr: the same picture as linear light into an R16G16B16A16_SFLOAT image (fill_hdr.comp), the
// light that post/tonemap.comp at 0 EV maps back to those colours (within its dither's one step).
enum class FillTarget { Display, Hdr };

// The fill.comp test pattern as a pass: one dispatch that writes FillParams' chapter, era, wipe, kick and
// beat phase into a storage image. The renderer uses its Hdr build as the placeholder 2026 picture.
class FillPass {
public:
    // Loads shaderDir()/fill.comp.spv (Display) or fill_hdr.comp.spv (Hdr).
    explicit FillPass(Context& ctx, FillTarget target = FillTarget::Display);

    // Moves target to GENERAL for the shader's writes and dispatches ceil(w/8) x ceil(h/8) workgroups.
    // Throws std::invalid_argument, recording nothing, for a target of another format than the pass writes.
    void record(VkCommandBuffer cmd, FrameRing& ring, Image& target, const FillParams& params);

private:
    VkFormat format_;
    ComputePipeline pipeline_;
};

} // namespace evo::gfx
