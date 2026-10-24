#include "render/renderer.hpp"

#include <stdexcept>
#include <string>

namespace evo::render {

namespace {

// Checked before any member is built from the size.
const RenderSettings& checked(const RenderSettings& settings) {
    if (settings.width == 0 || settings.height == 0)
        throw std::invalid_argument("the render size must be positive, got " +
                                    std::to_string(settings.width) + "x" + std::to_string(settings.height));
    return settings;
}

} // namespace

Renderer::Renderer(gfx::Context& ctx, gfx::FrameRing& ring, const RenderSettings& settings)
    : ring_(ring), crt_(checked(settings).crt), retro_(ctx, ring),
      modern_(ctx, ring, settings.width, settings.height),
      compositor_(ctx, ring, settings.width, settings.height) {}

Renderer::~Renderer() {
    // The frames in flight still use the images and pipelines. Best effort, as in ~FrameRing: a wait that
    // fails (a lost device) has nothing left to protect.
    try {
        ring_.waitAll();
    } catch (const std::exception&) {
    }
}

void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    checked({width, height});
    const VkExtent2D current = output().extent();
    if (width == current.width && height == current.height)
        return;
    ring_.waitAll(); // resize: the frames in flight may still read the old images
    modern_.resize(width, height);
    compositor_.resize(width, height);
}

void Renderer::render(VkCommandBuffer cmd, const show::TimelineFrame& frame) {
    stats_.retroRendered = retro_.render(cmd, frame);
    stats_.retroFrame = retro_.retroFrame();
    modern_.render(cmd, frame);
    compositor_.render(cmd, retro_.image(), modern_.image(), frame, crt_);
}

} // namespace evo::render
