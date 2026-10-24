#pragma once

#include "gfx/context.hpp"
#include "gfx/fill_pass.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "post/denoiser.hpp"
#include "post/tonemap.hpp"
#include "render/stage_plan.hpp"
#include "show/timeline.hpp"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace evo::render {

// The 2026 side. Every frame renders HDR linear light into an RGBA16F image at the render resolution, then
// the one tonemap (post::Tonemap: ACES, the sRGB OETF and a static dither) writes the display-encoded RGBA8
// image the compositor reads. The scene comes from modern::sceneFor(effect):
//   Placeholder  the fill.comp pattern's 2026 look (a smooth gradient in the chapter's hue with the circle
//                that follows kick and beat) as HDR light that the tonemap maps back to that look
//                (fill_hdr.comp), at 0 EV;
//   Studio       the path-traced room (modern::Studio): PathTracer -> Denoiser -> the HDR image, tone mapped
//                at the scene frame's exposure. The CRT samples the retro stage's colour image.
// Either way whole-frame 2026 whatever the era state; the compositor decides where it shows.
//
// Path tracing follows the HistoryState the renderer hands over: TraceSettings {spp, maxBounces, frameSeed};
// Interactive denoises temporally with 3 a-trous iterations; Export has no temporal history and filters with
// 3 a-trous iterations below 16 spp and not at all from 16 (Decision 6). The denoiser's history starts over
// whenever state.reset is not None, and the motion guide uses the previous frame's camera only while the
// history continues (never in Export). The primary hit's emission (the CRT's picture) bypasses the history
// and the filter, so the screen stays sharp and current (Review Focus 5).
//
// A path-traced scene's GPU side (BLASes, the ray tracing pipeline, the denoiser's images) is built on the
// first frame that shows it (setup work that waits for the GPU once: a stage that never shows the Studio,
// like the 1993-only tests' and most exports', never pays for it); prepare() builds it up front instead,
// which the window does so that no chapter hitches.
class ModernStage {
public:
    ModernStage(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height);
    ~ModernStage();
    ModernStage(const ModernStage&) = delete;
    ModernStage& operator=(const ModernStage&) = delete;

    // Builds every path-traced scene's GPU side now (setup: not between ring.begin() and submit()).
    void prepare();
    // Recreates the images at the new size. The caller makes sure no submitted frame still uses the old ones.
    void resize(std::uint32_t width, std::uint32_t height);
    // Records the frame. `retroImage` is the retro stage's colour image (R8G8B8A8_SRGB, mipmapped), which the
    // Studio's CRT samples; a scene that does not sample it ignores it (it may then be null). Throws
    // std::invalid_argument for a path-traced scene without a retro image.
    void render(VkCommandBuffer cmd, const show::TimelineFrame& frame, const HistoryState& state,
                gfx::Image* retroImage = nullptr);
    // The camera shot of a frame (a change resets the history): the scene camera's, 0 for the placeholder.
    std::uint32_t shotFor(const show::TimelineFrame& frame) const;
    // Whether the frame's 2026 picture shows the retro image (the Studio's CRT).
    bool samplesRetro(const show::TimelineFrame& frame) const;

    // The display-encoded picture (R8G8B8A8_UNORM): what the compositor shows.
    gfx::Image& image() { return image_; }
    // The HDR light it was tone mapped from (R16G16B16A16_SFLOAT).
    gfx::Image& hdr() { return hdr_; }
    // The Studio's path tracer, once built (tests read its G-buffer); null before.
    modern::PathTracer* studioTracer();

private:
    // A path-traced scene: its CPU description (built with the stage) and its GPU side (built on first use).
    struct Traced;

    Traced& traced(modern::SceneId id);
    void renderTraced(VkCommandBuffer cmd, Traced& scene, const show::TimelineFrame& frame,
                      const HistoryState& state, gfx::Image* retroImage);

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t width_, height_;
    gfx::FillPass fill_; // the placeholder, as HDR light
    post::Tonemap tonemap_;
    gfx::Image hdr_;
    gfx::Image image_;
    std::unique_ptr<Traced> studio_;
};

} // namespace evo::render
