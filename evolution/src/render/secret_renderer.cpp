#include "render/secret_renderer.hpp"

namespace evo::render {

namespace {

const retro::EffectInfo& secretRegistry(show::Effect) {
    return retro::secretEffect();
}

} // namespace

SecretRenderer::SecretRenderer(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width,
                               std::uint32_t height)
    : ring_(ring), width_(width), height_(height), text_(ctx, ring), retro_(ctx, ring, text_, secretRegistry),
      compositor_(ctx, ring, text_, width, height),
      blank_(ctx, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, 1, "secret part blank"}) {}

SecretRenderer::~SecretRenderer() {
    ring_.waitAll();
}

void SecretRenderer::resize(std::uint32_t width, std::uint32_t height) {
    if (width == width_ && height == height_)
        return;
    ring_.waitAll();
    compositor_.resize(width, height);
    width_ = width;
    height_ = height;
}

void SecretRenderer::render(VkCommandBuffer cmd, double t, const retro::SecretInput& input, bool crt) {
    const show::TimelineFrame frame = retro::secretFrame(t, input);
    retro_.render(cmd, frame);
    compositor_.render(cmd, retro_.image(), blank_, frame, crt, kRetroVisible);
}

} // namespace evo::render
