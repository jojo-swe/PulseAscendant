#pragma once

#include "gfx/context.hpp"
#include "gfx/fill_pass.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "render/stage_plan.hpp"
#include "show/timeline.hpp"

#include <volk.h>

#include <cstdint>

namespace evo::render {

// The 2026 side, a placeholder until steps 4-5: the fill.comp pattern's 2026 look (a smooth gradient in the
// chapter's hue with the circle that follows kick and beat), always whole-frame 2026 whatever the era state;
// the compositor decides where it shows. RGBA8 at the render resolution.
class ModernStage {
public:
    ModernStage(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height);

    // Recreates the image at the new size. The caller makes sure no submitted frame still uses the old one.
    void resize(std::uint32_t width, std::uint32_t height);
    // `history` says whether the temporal history continues; the placeholder has none and ignores it.
    void render(VkCommandBuffer cmd, const show::TimelineFrame& frame, const HistoryState& history);
    // The camera shot of a frame (a change resets the history): the placeholder has one, 0.
    std::uint32_t shotFor(const show::TimelineFrame& /*frame*/) const { return 0; }
    // Whether the frame's 2026 picture shows the retro image (the Studio's CRT): the placeholder does not.
    bool samplesRetro(const show::TimelineFrame& /*frame*/) const { return false; }

    gfx::Image& image() { return image_; }

private:
    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    gfx::FillPass fill_;
    gfx::Image image_;
};

} // namespace evo::render
