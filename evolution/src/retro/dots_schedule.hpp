#pragma once

// DOTS' morph schedule (retro/dots.hpp documents it), header-only so that the 2026 side's Particles scene
// (src/modern/scenes/particles.cpp, which does not link the retro effects) morphs on the very same frames.

#include "show/timeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace evo::retro {

inline constexpr std::uint32_t kDotsRetroRate = 35;
inline constexpr double kDotsMorphSeconds = 1.2;

// The schedule in retro frames: morph m (the m-th of cube -> sphere -> torus -> year -> torus -> sphere ->
// cube) blends over first < f < arrival; the next morph never starts before this one's arrival (a short
// chapter gives short or empty morphs).
struct DotsStep {
    std::uint32_t first, arrival;
};

inline std::array<DotsStep, 6> dotsSchedule(const show::TimelineFrame& frame) {
    constexpr std::size_t kMorphs = 6;
    const double duration = std::max(frame.chapterDuration, 0.0);
    const double accent = std::clamp(frame.accentSeconds, 0.3 * duration, 0.7 * duration);
    const double after = duration - accent;
    const double arrivals[kMorphs + 1] = {0,
                                          accent / 3,
                                          2 * accent / 3,
                                          accent,
                                          accent + 0.45 * after,
                                          accent + 0.65 * after,
                                          accent + 0.85 * after};
    const auto toFrame = [](double seconds) {
        return static_cast<std::uint32_t>(std::floor(seconds * kDotsRetroRate));
    };
    const auto longest = static_cast<std::uint32_t>(std::lround(kDotsMorphSeconds * kDotsRetroRate));
    std::array<DotsStep, kMorphs> steps{};
    for (std::size_t m = 0; m < kMorphs; ++m) {
        const std::uint32_t from = toFrame(arrivals[m]), to = toFrame(arrivals[m + 1]);
        const std::uint32_t frames = std::max(2u, std::min(longest, (to - from) * 4 / 5));
        // In a short chapter a morph may get fewer frames than it asks for, but never starts before the last
        // one has arrived.
        steps[m] = {std::max(from, to > frames ? to - frames : 0u), to};
    }
    return steps;
}

} // namespace evo::retro
