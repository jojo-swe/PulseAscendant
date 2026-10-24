#pragma once

#include "show/timeline.hpp"

#include <cstdint>
#include <optional>

namespace evo::render {

// How a renderer renders: Interactive (the window) keeps temporal history between frames and traces 1 spp
// per frame; Export (--capture-dir, --render-stream) makes every frame a pure function of the show time.
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
// 2026 while era > 0 and 1993 while era < 1; Wipe shows 2026 while wipe > 0 and 1993 while wipe < 1.
// Runs: the retro stage when it is visible or the modern stage samples it (the Studio's CRT); the modern
// stage when it is visible, and in the window also through a BeatSwap, Portal or Wipe chapter while it is
// hidden, so its history stays warm for the next swap (Decision 7). Exports skip it strictly.
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

// What the modern stage needs to know about its history on a frame.
struct HistoryState {
    HistoryReset reset = HistoryReset::FirstFrame;
    std::uint32_t framesSinceReset = 0;
};

// Decides, frame by frame, whether the temporal history continues. A reset on the first frame (and after
// invalidate()), a resize, a change of mode or spp, a chapter change, a shot change, a time jump, and when
// the modern stage did not run in the previous frame; the first matching reason in that order is reported.
class HistoryTracker {
public:
    // Called once per frame in which the modern stage runs; `skippedSinceLast` = it did not run last frame.
    HistoryReset advance(const HistoryKey& key, bool skippedSinceLast);
    std::uint32_t framesSinceReset() const { return framesSinceReset_; } // 0 on the frame that reset
    void invalidate() { last_.reset(); }                                 // next advance() returns FirstFrame

private:
    std::optional<HistoryKey> last_;
    std::uint32_t framesSinceReset_ = 0;
};

} // namespace evo::render
