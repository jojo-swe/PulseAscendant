#pragma once

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "render/compositor.hpp"
#include "render/modern_stage.hpp"
#include "render/retro_stage.hpp"
#include "show/timeline.hpp"

#include <volk.h>

#include <cstdint>

namespace evo::render {

struct RenderSettings {
    std::uint32_t width = 0, height = 0; // the render resolution (the output image)
    bool crt = true;                     // the CRT look on the 1993 picture
};

// Renders a timeline frame into one R8G8B8A8_UNORM image at the render resolution: the retro stage (only
// when its retro frame changes), the modern stage, then the compositor's CRT and era passes. Every pass is a
// compute pass recorded into the caller's command buffer from the given ring; the window blits output() and
// the exports read it back. The output is a pure function of (settings, frame).
//
// The ring must outlive the renderer, which waits for it (FrameRing::waitAll) before its images and
// pipelines go, and on resize().
class Renderer {
public:
    // Throws std::invalid_argument for a zero width or height.
    Renderer(gfx::Context& ctx, gfx::FrameRing& ring, const RenderSettings& settings);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Waits for the frames in flight, then recreates the output-size images. Not between ring.begin() and
    // ring.submit(). A no-op for the current size.
    void resize(std::uint32_t width, std::uint32_t height);
    // Records all passes; output() is GENERAL afterwards.
    void render(VkCommandBuffer cmd, const show::TimelineFrame& frame);
    gfx::Image& output() { return compositor_.output(); }
    void setCrt(bool crt) { crt_ = crt; }
    bool crt() const { return crt_; }

    struct Stats {
        bool retroRendered = false; // whether the last render() re-rendered the retro frame
        std::uint32_t retroFrame = 0;
    };
    Stats lastStats() const { return stats_; }

private:
    gfx::FrameRing& ring_;
    bool crt_;
    RetroStage retro_;
    ModernStage modern_;
    Compositor compositor_;
    Stats stats_;
};

} // namespace evo::render
