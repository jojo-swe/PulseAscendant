#include "render/modern_stage.hpp"

namespace evo::render {

namespace {

// The placeholder's exposure: fill_hdr.comp inverts the tonemap at 0 EV.
constexpr float kPlaceholderExposureEV = 0.f;

gfx::ImageDesc hdrDesc(std::uint32_t width, std::uint32_t height) {
    return {width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, 1, "modern hdr"};
}

// Transfer source too: tests read the 2026 picture back on its own.
gfx::ImageDesc imageDesc(std::uint32_t width, std::uint32_t height) {
    return {width,
            height,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            1,
            "modern"};
}

} // namespace

ModernStage::ModernStage(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height)
    : ctx_(ctx), ring_(ring), fill_(ctx, gfx::FillTarget::Hdr), tonemap_(ctx, ring),
      hdr_(ctx, hdrDesc(width, height)), image_(ctx, imageDesc(width, height)) {}

void ModernStage::resize(std::uint32_t width, std::uint32_t height) {
    hdr_ = gfx::Image(ctx_, hdrDesc(width, height));
    image_ = gfx::Image(ctx_, imageDesc(width, height));
}

void ModernStage::render(VkCommandBuffer cmd, const show::TimelineFrame& frame,
                         const HistoryState& /*history*/) {
    // Every scene renders the placeholder until Task 10 wires the path-traced Studio in (SceneId::Studio:
    // PathTracer -> Denoiser -> the same HDR image, with the scene frame's exposure).
    switch (modern::sceneFor(frame.effect)) {
    case modern::SceneId::Placeholder:
    case modern::SceneId::Studio: {
        gfx::FillParams params;
        params.localSeconds = static_cast<float>(frame.localSeconds);
        params.era = 1.0f;     // the 2026 look over the whole image
        params.transition = 0; // Cut: no wipe split, no cross-fade
        params.kick = frame.kick;
        params.beatPhase = frame.beatPhase;
        params.chapter = static_cast<std::uint32_t>(frame.chapter);
        fill_.record(cmd, ring_, hdr_, params);
        tonemap_.record(cmd, hdr_, image_, kPlaceholderExposureEV);
        break;
    }
    }
}

} // namespace evo::render
