#pragma once

#include "gfx/context.hpp"
#include "gfx/fill_pass.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "modern/modern_scene.hpp"
#include "post/tonemap.hpp"
#include "render/stage_plan.hpp"
#include "show/timeline.hpp"

#include <volk.h>

#include <cstdint>

namespace evo::render {

// The 2026 side. Every frame renders HDR linear light into an RGBA16F image at the render resolution, then
// the one tonemap (post::Tonemap: ACES, the sRGB OETF and a static dither) writes the display-encoded RGBA8
// image the compositor reads. The scene comes from modern::sceneFor(effect): SceneId::Placeholder, for now
// every effect, is the fill.comp pattern's 2026 look (a smooth gradient in the chapter's hue with the circle
// that follows kick and beat) as HDR light that the tonemap maps back to that look (fill_hdr.comp), always
// whole-frame 2026 whatever the era state; the compositor decides where it shows. The path-traced scenes
// render into the same HDR image (Task 10).
class ModernStage {
public:
    ModernStage(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height);

    // Recreates the images at the new size. The caller makes sure no submitted frame still uses the old ones.
    void resize(std::uint32_t width, std::uint32_t height);
    // `history` says whether the temporal history continues; the placeholder has none and ignores it.
    void render(VkCommandBuffer cmd, const show::TimelineFrame& frame, const HistoryState& history);
    // The camera shot of a frame (a change resets the history): the placeholder has one, 0.
    std::uint32_t shotFor(const show::TimelineFrame& /*frame*/) const { return 0; }
    // Whether the frame's 2026 picture shows the retro image (the Studio's CRT): the placeholder does not.
    bool samplesRetro(const show::TimelineFrame& /*frame*/) const { return false; }

    // The display-encoded picture (R8G8B8A8_UNORM): what the compositor shows.
    gfx::Image& image() { return image_; }
    // The HDR light it was tone mapped from (R16G16B16A16_SFLOAT).
    gfx::Image& hdr() { return hdr_; }

private:
    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    gfx::FillPass fill_; // the placeholder, as HDR light
    post::Tonemap tonemap_;
    gfx::Image hdr_;
    gfx::Image image_;
};

} // namespace evo::render
