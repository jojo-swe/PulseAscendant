#include "app/auto_scale.hpp"

#include <algorithm>
#include <cmath>

namespace evo {

namespace {

// Rounds down to a multiple of kAutoScaleStep (with a little slack for float), within the bounds; at or above
// the largest scale, that scale.
float quantised(float scale) {
    if (scale >= kAutoMaxTraceScale - 1e-4f)
        return kAutoMaxTraceScale;
    const float steps = std::floor(scale / kAutoScaleStep + 1e-3f);
    return std::clamp(steps * kAutoScaleStep, kAutoMinTraceScale, kAutoMaxTraceScale);
}

} // namespace

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
        0.45f, // 7 RAYTRACE
        0.35f, // 8 VECTOR CITY
        0.55f, // 9 FINALE
        0.67f, // 10 PORTAL
        0.67f, // 11 STUDIO NIGHT
        0.35f, // 12 ASCENT
        0.3f,  // 13 CITY NIGHT
        0.67f, // 14 NEBULA
        0.3f,  // 15 GLASS CATHEDRAL
        0.5f,  // 16 PARTICLE OCEAN
        0.67f, // 17 SPHERE STORM
        0.67f, // 18 SINGULARITY
        0.67f, // 19 RETURN
        0.67f, // 20 REBOOT
        0.67f, // 21 GALAXY
        0.67f, // 22 PLASMA PLANET
        0.67f, // 23 WORMHOLE
        0.67f, // 24 CRYSTAL CITY
        0.55f, // 25 LENS OF GOD
        0.5f,  // 26 RAYTRACE INFINITY
        0.45f, // 27 METROPOLIS
        0.67f, // 28 ASCENDANT
        0.67f, // 29 CREDITS
        0.67f, // 30 THE END
    };
    return table;
}

float autoTraceScale(int chapter, std::uint32_t width, std::uint32_t height) {
    const auto& table = autoTraceScaleTable();
    const float base = chapter >= 0 && chapter < static_cast<int>(table.size())
                           ? table[static_cast<std::size_t>(chapter)]
                           : kAutoMaxTraceScale;
    const double pixels = std::max(1.0, double(width) * double(height));
    const double reference = double(kAutoReferenceWidth) * double(kAutoReferenceHeight);
    return quantised(static_cast<float>(base * std::sqrt(reference / pixels)));
}

TraceScaleController::TraceScaleController(std::uint32_t width, std::uint32_t height)
    : width_(width), height_(height) {}

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
        restart(known != remembered_.end() ? known->second : autoTraceScale(chapter, width_, height_));
        return scale_;
    }
    if (settle_ > 0) {
        --settle_;
        return scale_;
    }
    if (!(gpuMs > 0.0) || !std::isfinite(gpuMs))
        return scale_;
    smoothed_ = smoothed_ < 0.0 ? gpuMs : smoothed_ + 0.1 * (gpuMs - smoothed_);
    over_ = smoothed_ > 1.1 * kAutoBudgetMs ? over_ + 1 : 0;
    under_ = smoothed_ < 0.5 * kAutoBudgetMs ? under_ + 1 : 0;
    if (over_ >= kDownFrames && scale_ > kAutoMinTraceScale) {
        // The pixels that fit the budget, a little under: at least one step down.
        const float fits = scale_ * static_cast<float>(std::sqrt(kAutoBudgetMs / smoothed_)) * 0.95f;
        const float lower = std::min(quantised(fits), quantised(scale_ - kAutoScaleStep));
        remembered_[chapter] = lower;
        restart(lower);
    } else if (under_ >= kUpFrames && scale_ < kAutoMaxTraceScale) {
        const float higher = quantised(scale_ + kAutoScaleStep + 1e-4f);
        remembered_[chapter] = higher;
        restart(higher);
    }
    return scale_;
}

} // namespace evo
