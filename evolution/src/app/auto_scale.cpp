#include "app/auto_scale.hpp"

#include <algorithm>
#include <cmath>

namespace evo {

namespace {

// Rounds down to a multiple of kAutoScaleStep (with a little slack for float), within the bounds; at or above
// the largest scale, that scale.
float quantised(float scale, float lowest = kAutoMinTraceScale) {
    if (scale >= kAutoMaxTraceScale - 1e-4f)
        return kAutoMaxTraceScale;
    const float steps = std::floor(scale / kAutoScaleStep + 1e-3f);
    return std::clamp(steps * kAutoScaleStep, lowest, kAutoMaxTraceScale);
}

bool sixty(int targetFps) {
    return targetFps == kAutoTargetFps60;
}

} // namespace

double autoBudgetMs(int targetFps) {
    return sixty(targetFps) ? kAutoBudgetMs60 : kAutoBudgetMs;
}

float autoMinTraceScale(int targetFps) {
    return sixty(targetFps) ? kAutoMinTraceScale60 : kAutoMinTraceScale;
}

const std::array<float, 31>& autoTraceScaleTable() {
    // At 1280x720: the largest scale estimated to hold 30 fps on an RTX 4080 (docs/PERFORMANCE.md: the
    // measured 1440p frame times, the fast mode's work per pixel, a frame's cost going with its traced
    // pixels). To be replaced per chapter by the largest scale whose `--bench --width 1280 --height 720
    // --trace-scale S` reaches 30 fps there. The 1993-only chapters (BOOT, CREDITS, THE END) trace nothing.
    static const std::array<float, 31> table = {
        0.67f, // 0 BOOT
        0.67f, // 1 FLYBY
        0.67f, // 2 DOTS
        0.67f, // 3 PLASMA
        0.4f,  // 4 TUNNEL
        0.67f, // 5 GLENZ
        0.67f, // 6 LENS
        0.4f,  // 7 RAYTRACE
        0.35f, // 8 VECTOR CITY
        0.55f, // 9 FINALE
        0.67f, // 10 PORTAL
        0.67f, // 11 STUDIO NIGHT
        0.3f,  // 12 ASCENT
        0.3f,  // 13 CITY NIGHT
        0.67f, // 14 NEBULA
        0.3f,  // 15 GLASS CATHEDRAL
        0.45f, // 16 PARTICLE OCEAN
        0.67f, // 17 SPHERE STORM
        0.67f, // 18 SINGULARITY
        0.67f, // 19 RETURN
        0.67f, // 20 REBOOT
        0.67f, // 21 GALAXY
        0.67f, // 22 PLASMA PLANET
        0.67f, // 23 WORMHOLE
        0.67f, // 24 CRYSTAL CITY
        0.5f,  // 25 LENS OF GOD
        0.45f, // 26 RAYTRACE INFINITY
        0.4f,  // 27 METROPOLIS
        0.67f, // 28 ASCENDANT
        0.67f, // 29 CREDITS
        0.67f, // 30 THE END
    };
    return table;
}

const std::array<float, 31>& autoTraceScaleTable60() {
    // At 1280x720: the largest scale estimated to hold 60 fps (kAutoBudgetMs60) on an RTX 4080
    // (docs/PERFORMANCE.md, round 3). The chapters measured on the RTX by the 30 fps table's model, with the
    // fast mode's work per pixel after round 3 (the half-rate volume layer); the others from their traces per
    // pixel at the measured chapters' median cost per trace. To be replaced per chapter by the largest scale
    // whose `--bench --width 1280 --height 720 --trace-scale S` reaches 60 fps there.
    static const std::array<float, 31> table = {
        0.67f, // 0 BOOT
        0.4f,  // 1 FLYBY
        0.35f, // 2 DOTS
        0.5f,  // 3 PLASMA
        0.3f,  // 4 TUNNEL
        0.4f,  // 5 GLENZ
        0.55f, // 6 LENS
        0.35f, // 7 RAYTRACE
        0.3f,  // 8 VECTOR CITY
        0.35f, // 9 FINALE
        0.4f,  // 10 PORTAL
        0.45f, // 11 STUDIO NIGHT
        0.25f, // 12 ASCENT
        0.25f, // 13 CITY NIGHT
        0.5f,  // 14 NEBULA
        0.25f, // 15 GLASS CATHEDRAL
        0.35f, // 16 PARTICLE OCEAN
        0.67f, // 17 SPHERE STORM
        0.67f, // 18 SINGULARITY
        0.4f,  // 19 RETURN
        0.4f,  // 20 REBOOT
        0.67f, // 21 GALAXY
        0.45f, // 22 PLASMA PLANET
        0.67f, // 23 WORMHOLE
        0.4f,  // 24 CRYSTAL CITY
        0.35f, // 25 LENS OF GOD
        0.4f,  // 26 RAYTRACE INFINITY
        0.25f, // 27 METROPOLIS
        0.67f, // 28 ASCENDANT
        0.67f, // 29 CREDITS
        0.67f, // 30 THE END
    };
    return table;
}

float autoTraceScale(int chapter, std::uint32_t width, std::uint32_t height, int targetFps) {
    const auto& table = sixty(targetFps) ? autoTraceScaleTable60() : autoTraceScaleTable();
    const float base = chapter >= 0 && chapter < static_cast<int>(table.size())
                           ? table[static_cast<std::size_t>(chapter)]
                           : kAutoMaxTraceScale;
    const double pixels = std::max(1.0, double(width) * double(height));
    const double reference = double(kAutoReferenceWidth) * double(kAutoReferenceHeight);
    return quantised(static_cast<float>(base * std::sqrt(reference / pixels)), autoMinTraceScale(targetFps));
}

TraceScaleController::TraceScaleController(std::uint32_t width, std::uint32_t height, int targetFps)
    : width_(width), height_(height), targetFps_(targetFps) {}

void TraceScaleController::resize(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
    remembered_.clear();
    chapter_ = -1;
}

void TraceScaleController::restart(float scale) {
    scale_ = scale;
    smoothed_ = -1.0;
    settle_ = kSettleFrames;
    over_ = 0;
    under_ = 0;
}

float TraceScaleController::update(int chapter, double gpuMs) {
    if (chapter != chapter_) {
        chapter_ = chapter;
        const auto known = remembered_.find(chapter);
        restart(known != remembered_.end() ? known->second : autoTraceScale(chapter, width_, height_, targetFps_));
        return scale_;
    }
    if (settle_ > 0) {
        --settle_;
        return scale_;
    }
    if (!(gpuMs > 0.0) || !std::isfinite(gpuMs))
        return scale_;
    smoothed_ = smoothed_ < 0.0 ? gpuMs : smoothed_ + 0.1 * (gpuMs - smoothed_);
    const double budget = autoBudgetMs(targetFps_);
    const float lowest = autoMinTraceScale(targetFps_);
    over_ = smoothed_ > 1.1 * budget ? over_ + 1 : 0;
    under_ = smoothed_ < 0.5 * budget ? under_ + 1 : 0;
    if (over_ >= kDownFrames && scale_ > lowest) {
        // The pixels that fit the budget, a little under: at least one step down.
        const float fits = scale_ * static_cast<float>(std::sqrt(budget / smoothed_)) * 0.95f;
        const float lower = std::min(quantised(fits, lowest), quantised(scale_ - kAutoScaleStep, lowest));
        remembered_[chapter] = lower;
        restart(lower);
    } else if (under_ >= kUpFrames && scale_ < kAutoMaxTraceScale) {
        const float higher = quantised(scale_ + kAutoScaleStep + 1e-4f, lowest);
        remembered_[chapter] = higher;
        restart(higher);
    }
    return scale_;
}

} // namespace evo
