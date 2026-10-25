#pragma once

#include "post/denoise_settings.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace evo::render {

// How a renderer renders: Interactive (the window) keeps temporal history between frames and traces --spp
// samples per frame (1 by default); Export (--capture-dir, --render-stream) makes every frame a pure function
// of the show time.
enum class RenderMode { Interactive, Export };

// What a frame needs. The visible flags say whether the compositor may show a stage's image at all (they may
// include a stage that ends up covering no pixel, never leave out one that shows); the run flags say whether
// the stage renders this frame. The compositor never reads the image of a stage that is not visible, so a
// stage that did not run can never leak a stale picture into the output.
struct StagePlan {
    bool retroVisible, modernVisible; // may the compositor show it (over-inclusion allowed, never under)
    bool retro, modern;               // does the stage run this frame
};

// Visible, as the compositor blends: Cut and BeatSwap show 2026 from era 0.5 and 1993 below it; Portal shows
// 2026 while era > 0 and 1993 while era < 1; Wipe shows 2026 while wipe > 0 and 1993 while wipe < 1 (its
// refractive edge shows 1993). FINALE (show::holdsModern) shows 2026 only, whatever the era.
// Runs: the retro stage when it is visible or the modern stage samples it (the Studio's CRT); the modern
// stage when it is visible, and in the window also through a BeatSwap, Portal or Wipe chapter while it is
// hidden, so its history stays warm for the next swap. Exports skip it strictly. In FINALE the retro stage
// therefore runs exactly while the Studio is shown, not during the megamix's cuts to other scenes; the
// renderer passes modernSamplesRetro for the scene the frame shows (show::sceneFrame).
StagePlan planStages(const show::TimelineFrame& frame, RenderMode mode, bool modernSamplesRetro);

// The path tracer's bounce limit: `requested`, or the mode's default for 0.
inline constexpr std::uint32_t kInteractiveMaxBounces = 4;
inline constexpr std::uint32_t kExportMaxBounces = 6;
std::uint32_t resolveMaxBounces(RenderMode mode, std::uint32_t requested);

// Why the modern stage's temporal history starts over on a frame (None: it continues).
enum class HistoryReset : std::uint8_t {
    None,
    FirstFrame,
    Resize,
    Settings,
    Chapter,
    Shot,
    TimeJump,
    Skipped
};

// A step in show time larger than this, or any step back, is a jump (a seek); a repeated time is not.
inline constexpr double kTimeJumpSeconds = 0.5;

// What the history depends on, per frame in which the modern stage runs.
struct HistoryKey {
    int chapter;
    std::uint32_t shot; // the camera shot within the chapter
    double showSeconds;
    std::uint32_t width, height;
    RenderMode mode;
    std::uint32_t spp;
};

// What the modern stage needs to know about a frame besides the timeline: whether its temporal history
// continues, and how to sample it. The renderer fills in its mode, the spp this frame traces (frameSpp
// below: 4 and 2 on the window's first two frames of a history) and resolved bounce limit (never 0), and the
// frame seed: frameSeedFor(showSeconds, 0) in Export, whatever frames came before (a pure function of the
// show time), and frameSeedFor(showSeconds, framesSinceReset) in Interactive, so the window's first frame
// after a reset uses the export seed (modern/path_tracer.hpp) and equals an export at spp max(spp, 4).
struct HistoryState {
    HistoryReset reset = HistoryReset::FirstFrame;
    std::uint32_t framesSinceReset = 0;
    RenderMode mode = RenderMode::Export;
    std::uint32_t spp = 1;
    std::uint32_t maxBounces = kExportMaxBounces;
    std::uint32_t frameSeed = 0;
    // The export shutter's open time per frame in seconds (0.5 / fps for a 180 degree shutter), 0 for none:
    // the stage traces up to modern::kMaxShutterSlices time slices of it in Export mode only. The window has
    // no shutter, whatever this says.
    double shutterSeconds = 0;
    // Export only: the stage submits the frame's GPU work so far before each time slice (FrameRing::split),
    // so no single submit traces the whole frame. The same commands run in the same order, so the frame's
    // bytes do not change. The window never splits, whatever this says.
    bool submitPerSlice = false;
};

// Decides, frame by frame, whether the temporal history continues. A reset on the first frame (and after
// invalidate()), a resize (a changed size, or markResize()), a change of mode or spp, a chapter change, a
// shot change, a time jump, and when the modern stage did not run in the previous frame; the first matching
// reason in that order is reported.
class HistoryTracker {
public:
    // Called once per frame in which the modern stage runs; `skippedSinceLast` = it did not run last frame.
    HistoryReset advance(const HistoryKey& key, bool skippedSinceLast);
    std::uint32_t framesSinceReset() const { return framesSinceReset_; } // 0 on the frame that reset
    // Next advance() returns FirstFrame.
    void invalidate() {
        last_.reset();
        resized_ = false;
    }
    // The output was resized: the next advance() reports Resize (after FirstFrame) even when the size it
    // sees equals the last one, as after a resize A -> B -> A with no frame in between.
    void markResize() { resized_ = true; }

private:
    std::optional<HistoryKey> last_;
    bool resized_ = false; // markResize() since the last advance()
    std::uint32_t framesSinceReset_ = 0;
};

// The window traces more samples on the first frames of a history, so a seek, a cut or a resize starts clean
//: max(spp, kResetSpp[framesSinceReset]) while framesSinceReset < 2, then spp. Exports
// always trace spp. The HistoryKey keeps the configured spp, so the extra samples never reset anything.
inline constexpr std::array<std::uint32_t, 2> kResetSpp{4, 2};
std::uint32_t frameSpp(RenderMode mode, std::uint32_t spp, std::uint32_t framesSinceReset);

// From this many samples per pixel exports clamp fireflies and filter the surface lighting lightly: only
// kExportFineAtrousIterations a-trous iterations (steps 1 and 2), which still remove the speckle of many
// small lights and of caustics that 16-64 spp leave, while the depth, normal and luminance stops (the
// luminance stop scaled by the local deviation, smaller at more samples) keep edges and detail.
inline constexpr std::uint32_t kExportFilterSpp = 16;
inline constexpr std::uint32_t kExportFineAtrousIterations = 2;

// The denoiser settings of a frame: the window keeps history and always runs 3 a-trous iterations (5 at
// history length <= 3); exports keep none and run 3 (5, every pixel's history being one frame) below
// kExportFilterSpp, and from it the firefly filter (k = 4, never in the window) and
// kExportFineAtrousIterations in all. Both: 4 iterations on the volume layer.
post::DenoiseSettings denoiseSettingsFor(const HistoryState& state);

} // namespace evo::render
