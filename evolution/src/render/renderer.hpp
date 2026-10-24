#pragma once

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "render/compositor.hpp"
#include "render/modern_stage.hpp"
#include "render/retro_stage.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <volk.h>

#include <cstdint>
#include <vector>

namespace evo::render {

struct RenderSettings {
    std::uint32_t width = 0, height = 0; // the render resolution (the output image)
    bool crt = true;                     // the CRT look on the 1993 picture
    bool hud = true;                     // the chapter title and the epoch cards
};

// The HUD of a frame at a width x height output, drop shadows included (each text item follows its shadow,
// the same text in black one pixel right and down): the chapter title top-left at 1x and the epoch card
// bottom-left at 2x, the 1993 card while era < 0.5 and the 2026 card from 0.5; during a Wipe the 2026 card
// bottom-left and the 1993 card bottom-right (raised a line when the two would meet). Empty texts are left
// out. Every pixel, shadows included, stays at least kHudMargin pixels inside the output edges.
inline constexpr int kHudMargin = 8;
std::vector<text::Item> hudItems(const show::TimelineFrame& frame, std::uint32_t width, std::uint32_t height);

// Renders a timeline frame into one R8G8B8A8_UNORM image at the render resolution: the retro stage (only
// when its retro frame changes), the modern stage, then the compositor's CRT and era passes. Every pass is a
// compute pass recorded into the caller's command buffer from the given ring, the HUD text last; the window
// blits output() and the exports read it back. The output is a pure function of (settings, frame).
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
    void setHud(bool hud) { hud_ = hud; }
    bool hud() const { return hud_; }

    struct Stats {
        bool retroRendered = false; // whether the last render() re-rendered the retro frame
        std::uint32_t retroFrame = 0;
    };
    Stats lastStats() const { return stats_; }

private:
    gfx::FrameRing& ring_;
    bool crt_;
    bool hud_;
    text::TextPass text_; // before the stages that draw with it
    RetroStage retro_;
    ModernStage modern_;
    Compositor compositor_;
    Stats stats_;
};

} // namespace evo::render
