#pragma once

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "post/grade.hpp"

#include <volk.h>

#include <memory>

namespace evo::post {

class Bloom;

// What the tonemap mixes in and grades with besides the HDR image (all off by default: the plain tonemap).
struct ToneInputs {
    float exposureEV = 0;
    Grade grade;
    // The palette grade.palette mixes toward; needed when grade.palette > 0.
    const GradePalette* palette = nullptr;
    // post::Bloom's picture (R16G16B16A16_SFLOAT, half the HDR image's size rounded up) and its weight w:
    // the tonemap reads (1 - w) I + w B, with B bilinear from the half-size picture. Needed when
    // bloomWeight > 0.
    gfx::Image* bloom = nullptr;
    float bloomWeight = 0;
};

// The one tonemap of the 2026 picture (shaders/post/tonemap.comp): HDR linear light -> display-encoded RGBA8,
// exactly as tonemapReference / tonemapGraded (tonemap_reference.hpp) describe. The display encoding happens
// here and nowhere else: the LDR image holds sRGB-encoded values in a UNORM image, like the retro side, and
// the window and the exports show its bytes as they are. With ToneInputs it also mixes in the bloom, grades
// in linear display light and mixes toward a palette (post::Look decides how much, per frame); with the
// defaults the bytes are the plain tonemap's.
class Tonemap {
public:
    Tonemap(gfx::Context& ctx, gfx::FrameRing& ring); // loads shaderDir()/post_tonemap.comp.spv
    ~Tonemap();
    Tonemap(const Tonemap&) = delete;
    Tonemap& operator=(const Tonemap&) = delete;

    // Records post::Bloom over `hdr` and returns its picture, for ToneInputs::bloom. The bloom's images are
    // made on the first call and remade when the HDR image's size changes (the caller makes sure no submitted
    // frame still uses the old ones, as for any resize). anamorphic: Bloom::record's.
    gfx::Image& recordBloom(VkCommandBuffer cmd, gfx::Image& hdr, float anamorphic);
    // The bloom's images, once made (0 before).
    VkDeviceSize bloomBytes() const;

    // One dispatch over the image: hdr (R16G16B16A16_SFLOAT, storage) is read, ldr (R8G8B8A8_UNORM, storage,
    // the same size) written; both end in GENERAL. Throws std::invalid_argument, recording nothing, for
    // another format or size, a bloom picture of another size or format, or a palette weight or bloom weight
    // without its input. Between ring.begin() and ring.submit() only. A palette is written into the command
    // buffer (vkCmdUpdateBuffer, 5 KiB) only on frames whose palette weight is not 0.
    void record(VkCommandBuffer cmd, gfx::Image& hdr, gfx::Image& ldr, float exposureEV);
    void record(VkCommandBuffer cmd, gfx::Image& hdr, gfx::Image& ldr, const ToneInputs& inputs);

private:
    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    gfx::ComputePipeline pipeline_;
    gfx::Image dummyBloom_; // 1x1, bound (never read) when there is no bloom
    gfx::Buffer palette_;   // a GradePaletteTable
    std::unique_ptr<Bloom> bloom_;
};

} // namespace evo::post
