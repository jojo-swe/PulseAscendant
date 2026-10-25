#pragma once

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "render/compositor.hpp"
#include "render/retro_stage.hpp"
#include "retro/secret_part.hpp"
#include "text/text_pass.hpp"

#include <volk.h>

#include <cstdint>

namespace evo::render {

// Renders the secret part (retro/secret_part.hpp) at an output size: its own retro stage, whose registry
// holds the part's one effect, and its own compositor with the CRT on the 1993 side only. Nothing is path
// traced and the show's Renderer is never touched, so the part can run beside it in the window or alone
// in a stream. The output is an R8G8B8A8_UNORM image like Renderer::output(), a pure function of (size,
// crt, t, the settled input).
class SecretRenderer {
public:
    // Builds the pipelines (setup: not between ring.begin() and ring.submit()). The ring must outlive it.
    SecretRenderer(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height);
    ~SecretRenderer();
    SecretRenderer(const SecretRenderer&) = delete;
    SecretRenderer& operator=(const SecretRenderer&) = delete;

    // Waits for the frames in flight, then recreates the output-size images. A no-op for the current size.
    void resize(std::uint32_t width, std::uint32_t height);
    // Records the part at second t; output() is GENERAL afterwards.
    void render(VkCommandBuffer cmd, double t, const retro::SecretInput& input, bool crt = true);

    gfx::Image& output() { return compositor_.output(); }
    gfx::Image& retroImage() { return retro_.image(); } // the 320x200 picture (tests)
    gfx::Image& indexImage() { return retro_.indexImage(); }

private:
    gfx::FrameRing& ring_;
    std::uint32_t width_, height_;
    text::TextPass text_;
    RetroStage retro_;
    Compositor compositor_;
    gfx::Image blank_; // stands in for the 2026 image, which the part never shows (never read)
};

} // namespace evo::render
