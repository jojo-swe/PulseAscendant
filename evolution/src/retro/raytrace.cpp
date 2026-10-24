#include "retro/raytrace.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evo::retro {

void raytracePalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    palette.fill(rgba(0, 0, 0));
    for (int r = 0; r < kRaytraceCubeLevels; ++r)
        for (int g = 0; g < kRaytraceCubeLevels; ++g)
            for (int b = 0; b < kRaytraceCubeLevels; ++b)
                palette[kRaytraceCube + 36 * r + 6 * g + b] =
                    rgba(static_cast<std::uint8_t>(kRaytraceCubeStep * r),
                         static_cast<std::uint8_t>(kRaytraceCubeStep * g),
                         static_cast<std::uint8_t>(kRaytraceCubeStep * b));
    for (int k = 0; k < kRaytraceGreyCount; ++k) {
        const auto v = static_cast<std::uint8_t>(8 + 10 * k);
        palette[kRaytraceGreys + k] = rgba(v, v, v);
    }
}

std::uint32_t raytraceBayer(std::uint32_t x, std::uint32_t y) {
    static constexpr std::uint32_t kMatrix[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return kMatrix[(y & 3u) * 4 + (x & 3u)];
}

// Mirrors shaders/retro/raytrace_quantise.glsl operation for operation. Every quantity is in units of 1/32
// of a colour level, so the Bayer move 51 ((bayer + 0.5) / 16 - 0.5) = 51 (2 bayer - 15) / 32 is whole.
std::uint8_t raytraceQuantise(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint32_t bayer) {
    const int offset = 102 * static_cast<int>(bayer) - 765;
    const int t[3] = {32 * r + offset, 32 * g + offset, 32 * b + offset};
    // The cube: level round(T / 51) = floor((T + 816) / 1632); T + 816 >= 51, and the level is at most 5.
    int q[3];
    int cube = 0;
    for (int c = 0; c < 3; ++c) {
        q[c] = (t[c] + 816) / 1632;
        const int d = t[c] - 1632 * q[c];
        cube += d * d;
    }
    // The grey nearest the mean m = sum / 96: k = round((m - 8) / 10) = floor((sum - 288) / 960), in 0..23.
    const int sum = t[0] + t[1] + t[2];
    const int k = sum < 288 ? 0 : std::min(kRaytraceGreyCount - 1, (sum - 288) / 960);
    const int value = 32 * (8 + 10 * k);
    int grey = 0;
    for (int c = 0; c < 3; ++c)
        grey += (t[c] - value) * (t[c] - value);
    if (grey < cube)
        return static_cast<std::uint8_t>(kRaytraceGreys + k);
    return static_cast<std::uint8_t>(kRaytraceCube + 36 * q[0] + 6 * q[1] + q[2]);
}

std::array<RaytraceSphere, kRaytraceSpheres> raytraceSpheres(double seconds) {
    std::array<RaytraceSphere, kRaytraceSpheres> spheres{};
    spheres[0] = {{0, 1.3, 0}, 1.3};
    for (int i = 1; i < kRaytraceSpheres; ++i) {
        const double angle = 0.6 * seconds + (i - 1) * 2 * std::numbers::pi / 5;
        const double radius = 0.6;
        const double bounce = std::abs(std::sin(1.7 * seconds + 1.3 * i));
        spheres[i] = {{kRaytraceOrbitRadius * std::sin(angle), radius + 0.9 * bounce,
                       -kRaytraceOrbitRadius * std::cos(angle)},
                      radius};
    }
    return spheres;
}

Camera raytraceCamera() {
    return Camera{{0, 2.0, -8.0}, {0, 1.0, 0}, {0, 1, 0}, 46.0};
}

} // namespace evo::retro
