#pragma once

// The adaptive trace scale (--trace-scale auto, the default of the window and --bench; docs/PERFORMANCE.md):
// the path tracer traces each chapter at a scale of the output from a table (the scale estimated to hold
// 30 fps at 1280x720 on an RTX 4080), and in the window a controller lowers it while the GPU misses the
// frame budget and raises it again when there is room for 60 fps; the temporal upscaler brings the picture
// to the output size. A change of scale resets the temporal history (as a resize does), so the controller
// moves rarely: at once where the chapter changes (which resets it anyway), within a chapter only after a
// sustained miss or a long stretch of headroom. --bench uses the table alone, so its numbers are
// reproducible.

#include <array>
#include <cstdint>
#include <map>

namespace evo {

inline constexpr float kAutoMinTraceScale = 0.3f;  // the softest picture the controller accepts
inline constexpr float kAutoMaxTraceScale = 0.67f; // the window's former fixed scale (kWindowTraceScale)
inline constexpr float kAutoScaleStep = 0.05f;     // scales are multiples of this, within the bounds
inline constexpr double kAutoBudgetMs = 31.0;      // GPU ms per frame: 30 fps with a margin
inline constexpr std::uint32_t kAutoReferenceWidth = 1280, kAutoReferenceHeight = 720;

// The table: a chapter's trace scale at 1280x720 (chapters as --show-info numbers them; 31 of them).
const std::array<float, 31>& autoTraceScaleTable();

// The trace scale a chapter starts at on a width x height output: its table entry times
// sqrt(1280 * 720 / (width * height)) (a frame's cost goes with its traced pixels), rounded down to
// kAutoScaleStep, within [kAutoMinTraceScale, kAutoMaxTraceScale]. A chapter outside the table gets
// kAutoMaxTraceScale's.
float autoTraceScale(int chapter, std::uint32_t width, std::uint32_t height);

// The window's controller. Pure logic (no GPU): the window asks it for each frame's scale and applies a
// change before recording the frame (Renderer::setTraceScale).
class TraceScaleController {
public:
    TraceScaleController(std::uint32_t width, std::uint32_t height);

    // The scale of the frame about to be recorded: `chapter` is the frame's chapter, `gpuMs` the GPU time of
    // the newest completed frame (Renderer::Stats::gpuMs "total"; <= 0 when unknown, which never moves the
    // scale). On a chapter change the chapter's last scale in this run (or autoTraceScale); then down when
    // the smoothed GPU time stays above 1.1 x kAutoBudgetMs for kDownFrames frames (to the scale its pixels
    // say fits, at least a step), up a step when it stays below 0.5 x the budget (room for 60 fps) for
    // kUpFrames frames.
    float update(int chapter, double gpuMs);
    // A new output size: every chapter starts over from the table.
    void resize(std::uint32_t width, std::uint32_t height);
    float scale() const { return scale_; }

    static constexpr int kSettleFrames = 8; // frames ignored after a change (the old scale still in flight)
    static constexpr int kDownFrames = 30;
    static constexpr int kUpFrames = 180;

private:
    void restart(float scale);

    std::uint32_t width_, height_;
    int chapter_ = -1;
    float scale_ = kAutoMaxTraceScale;
    double smoothed_ = -1.0; // the GPU time's moving average since the last change (< 0: none yet)
    int settle_ = 0, over_ = 0, under_ = 0;
    std::map<int, float> remembered_; // per chapter, the scale it last settled at
};

} // namespace evo
