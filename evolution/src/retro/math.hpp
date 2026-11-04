#pragma once

#include <algorithm>
#include <cstdint>
#include <numbers>

namespace evo::retro {

// The small scalar helpers the CPU effect code shares (CITY, DOTS, FLYBY, PORTAL).

constexpr double radians(double degrees) {
    return degrees * std::numbers::pi / 180;
}

// 3x^2 - 2x^3 on x clamped to [0, 1].
constexpr double smoothstep(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

// A number in [0, 1) from 32 hash bits.
constexpr double unit(std::uint32_t hash) {
    return static_cast<double>(hash) / 4294967296.0;
}

} // namespace evo::retro
