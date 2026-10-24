#include "render/modern_stage.hpp"

namespace evo::render {

namespace {

gfx::ImageDesc imageDesc(std::uint32_t width, std::uint32_t height) {
    return {width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, 1, "modern"};
}

} // namespace

ModernStage::ModernStage(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height)
    : ctx_(ctx), ring_(ring), fill_(ctx), image_(ctx, imageDesc(width, height)) {}

void ModernStage::resize(std::uint32_t width, std::uint32_t height) {
    image_ = gfx::Image(ctx_, imageDesc(width, height));
}

void ModernStage::render(VkCommandBuffer cmd, const show::TimelineFrame& frame) {
    gfx::FillParams params;
    params.localSeconds = static_cast<float>(frame.localSeconds);
    params.era = 1.0f;     // the 2026 look over the whole image
    params.transition = 0; // Cut: no wipe split, no cross-fade
    params.kick = frame.kick;
    params.beatPhase = frame.beatPhase;
    params.chapter = static_cast<std::uint32_t>(frame.chapter);
    fill_.record(cmd, ring_, image_, params);
}

} // namespace evo::render
