#pragma once

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/gpu_timers.hpp"
#include "gfx/image.hpp"
#include "render/compositor.hpp"
#include "render/modern_stage.hpp"
#include "render/retro_stage.hpp"
#include "render/stage_plan.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace evo::render {

struct RenderSettings {
    std::uint32_t width = 0, height = 0;  // the render resolution (the output image)
    bool crt = true;                      // the CRT look on the 1993 picture
    bool hud = true;                      // the epoch cards (and the chapter title, when hudTitle is set)
    bool hudTitle = false;                // the chapter title: a development aid, set only in the window
    RenderMode mode = RenderMode::Export; // the window renders Interactive, the exports Export
    std::uint32_t spp = 1;                // samples per pixel per frame (at least 1)
    std::uint32_t maxBounces = 0;         // 0 = the mode's default (interactive 4, export 6)
    modern::SceneDetail detail;           // the 2026 scenes' detail (--detail)
    // The 2026 side traces at traceSize(width, height, traceScale) and upscales in time (Task 4 of step 5);
    // in (0, 1]. The window runs at 0.67 by default (--trace-scale), the CLI's exports always at 1.
    float traceScale = 1.f;
    // The export shutter (Task 9 of step 5): the seconds the shutter is open per frame, 0.5 / fps for a 180
    // degree shutter (the CLI's exports), 0 for none. Exports trace up to 8 time slices of it, one per 4
    // samples (modern/shutter.hpp), for motion blur; the window never has a shutter.
    double shutterSeconds = 0;
    // Test hooks: the modern stage's scene factory and effect -> scene map (empty: the show's).
    modern::SceneFactory sceneFactory;
    modern::SceneMap sceneMap;
};

// The HUD scales with the output height in whole steps: unit = max(1, round(height / 360)), so 1 at 640x360,
// 2 at 1280x720, 4 at 2560x1440 and 6 at 3840x2160. Every margin is kHudMarginUnits units and every drop
// shadow is the same text in black one unit right and down.
inline constexpr int kHudMarginUnits = 8;
int hudUnit(std::uint32_t height);

// The HUD of a frame at a width x height output, drop shadows included (each text item follows its shadow):
// the chapter title top-left at scale unit when `title` is set, and the epoch card bottom-left at scale
// 2 * unit, the 1993 card while era < 0.5 and the 2026 card from 0.5. A Wipe chapter shows the 1993 card
// before its wipe (wipe = 0) and the 2026 card after it (wipe = 1); while 0 < wipe < 1 it shows both, the
// 2026 card bottom-left and the 1993 card bottom-right (raised a line when the two would meet). A card that
// at 2 * unit would cross the right margin is drawn at scale unit. Empty texts are left out. Every pixel,
// shadows included, stays at least kHudMarginUnits * unit pixels inside the output edges (a card too wide
// even at scale unit, in a very narrow output, is the exception).
std::vector<text::Item> hudItems(const show::TimelineFrame& frame, std::uint32_t width, std::uint32_t height,
                                 bool title);

// Renders a timeline frame into one R8G8B8A8_UNORM image at the render resolution: the retro stage (only
// when its retro frame changes), the modern stage, then the compositor's CRT and era passes. Every pass is a
// compute pass recorded into the caller's command buffer from the given ring, the HUD text last; the window
// blits output() and the exports read it back. The output is a pure function of (settings, frame) in Export
// mode, and of (settings, the frames since the last history reset) in Interactive mode.
//
// Only the stages a frame needs run (planStages): a stage the frame does not show is skipped, and the
// compositor is told which images are visible, so a skipped stage's stale image never reaches the output.
// The history tracker advances on every frame in which the modern stage runs.
//
// Scene cuts (show/director.hpp): the modern stage renders show::sceneFrame(frame), the FINALE megamix's or
// REBOOT's cut to another chapter's scene, else the frame itself. That frame picks the scene, its camera
// shot, whether the scene samples the retro image, the history key and the seed, so a cut resets the history
// (Chapter), the window's first frame after it traces the reset frame's samples, and a cut frame's 2026
// picture is the picture of the cut's scene time. The retro stage, the compositor and the HUD follow the
// frame itself.
//
// The ring must outlive the renderer, which waits for it (FrameRing::waitAll) before its images and
// pipelines go, and on resize().
//
// Every frame is timed on the GPU (gfx::GpuTimers, when the queue has timestamps): "total" around the frame,
// "retro" (when the retro stage runs), the modern stage's passes (ModernStage) and "compositor" (CRT, era
// blend and HUD). Stats::gpuMs holds the scopes of the newest frame the GPU completed.
class Renderer {
public:
    // Throws std::invalid_argument for a zero width, height or spp, or a trace scale outside (0, 1].
    Renderer(gfx::Context& ctx, gfx::FrameRing& ring, const RenderSettings& settings);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Waits for the frames in flight, then recreates the output-size images; the modern stage's next frame
    // resets its history (Resize), even after a resize back to the size it last rendered. Not between
    // ring.begin() and ring.submit(). A no-op for the current size.
    void resize(std::uint32_t width, std::uint32_t height);
    // Builds the GPU side of the path-traced scenes that the frames at these show times show (the scene of
    // a frame's cut, if it has one), now instead of in the first frame that shows them, so that no chapter
    // hitches (setup: not between ring.begin() and ring.submit()). The exports pass the times they will
    // render. An unprepared renderer builds a scene inside its first frame, with the same pixels
    // (ModernStage).
    void prepare(std::span<const double> showSeconds);
    // Every path-traced scene of the show (the window).
    void prepareAll() { modern_.prepareAll(); }
    // A new trace scale: waits for the frames in flight, recreates the trace-size images, and the modern
    // stage's next frame resets its history (Resize). A no-op for the current scale; throws
    // std::invalid_argument outside (0, 1] (changing nothing). Not between ring.begin() and ring.submit().
    void setTraceScale(float scale);
    float traceScale() const { return modern_.traceScale(); }
    // The GPU timers every frame is timed with (the bench reads every completed frame from them).
    gfx::GpuTimers& timers() { return timers_; }
    // Records all passes; output() is GENERAL afterwards.
    void render(VkCommandBuffer cmd, const show::TimelineFrame& frame);
    gfx::Image& output() { return compositor_.output(); }
    // The 2026 stage (tests read its tracer and memory).
    ModernStage& modern() { return modern_; }
    void setCrt(bool crt) { crt_ = crt; }
    bool crt() const { return crt_; }
    // The whole HUD, the title included when the settings asked for it (key H in the window).
    void setHud(bool hud) { hud_ = hud; }
    bool hud() const { return hud_; }

    struct Stats {
        bool retroRendered = false; // whether the last render() re-rendered the retro frame
        std::uint32_t retroFrame =
            0;                       // the retro frame the last render() showed; 0 when the stage was skipped
        bool modernRendered = false; // whether the last render() ran the modern stage
        // The modern stage's history on the last render(): None and 0 when the stage did not run.
        HistoryReset historyReset = HistoryReset::None;
        std::uint32_t framesSinceReset = 0;
        // The seed handed to the modern stage (HistoryState::frameSeed); 0 when the stage did not run.
        std::uint32_t frameSeed = 0;
        // The samples per pixel the modern stage traced (frameSpp: more on the window's reset frames); 0
        // when the stage did not run.
        std::uint32_t spp = 0;
        // GPU milliseconds per pass, in the order the scopes began ("total" first), of the newest frame the
        // GPU completed when this render() was recorded (not this frame's); empty without timestamps.
        std::vector<std::pair<std::string, double>> gpuMs;
        // The subpixel jitter the modern stage traced with (trace pixels; post::subpixelWindow): non-zero
        // only in the window below scale 1 after a history's first frame. 0 when the stage did not trace.
        std::array<float, 2> traceJitter{0.f, 0.f};
    };
    Stats lastStats() const { return stats_; }
    RenderMode mode() const { return mode_; }
    std::uint32_t spp() const { return spp_; }
    std::uint32_t maxBounces() const { return maxBounces_; } // resolved: never 0

private:
    gfx::FrameRing& ring_;
    gfx::GpuTimers timers_; // before the stages that record scopes into it
    bool crt_;
    bool hud_;
    bool hudTitle_;
    RenderMode mode_;
    std::uint32_t spp_;
    std::uint32_t maxBounces_;
    double shutterSeconds_;
    HistoryTracker history_;
    bool modernRanLastFrame_ = false;
    text::TextPass text_; // before the stages that draw with it
    RetroStage retro_;
    ModernStage modern_;
    Compositor compositor_;
    Stats stats_;
};

} // namespace evo::render
