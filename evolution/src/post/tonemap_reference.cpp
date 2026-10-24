#include "post/tonemap_reference.hpp"

#include <algorithm>
#include <cmath>

namespace evo::post {

namespace {

// Stephen Hill's ACES fit (BakingLab, ACES.hlsl), row-major: out[i] = sum_j m[i][j] * in[j]. Must match
// shaders/post/aces.glsl.
// sRGB -> AP1, with the RRT's saturation adjustment folded in.
constexpr float kInput[3][3] = {
    {0.59719f, 0.35458f, 0.04823f},
    {0.07600f, 0.90834f, 0.01566f},
    {0.02840f, 0.13383f, 0.83777f},
};
// AP1 -> sRGB, with the ODT's saturation adjustment folded in.
constexpr float kOutput[3][3] = {
    {1.60475f, -0.53108f, -0.07367f},
    {-0.10208f, 1.10813f, -0.00605f},
    {-0.00327f, -0.07276f, 1.07602f},
};

// The RRT + ODT curve.
float rrtAndOdtFit(float v) {
    const float a = v * (v + 0.0245786f) - 0.000090537f;
    const float b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

// PCG-RXS-M-XS, as pcgHash in shaders/rt/random.glsl.
std::uint32_t pcgHash(std::uint32_t v) {
    const std::uint32_t state = v * 747796405u + 2891336453u;
    const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// A uniform in (0, 1) from the hash's top 23 bits plus a half step: exact in float, never 0 or 1.
float openUnit(std::uint32_t h) {
    return (static_cast<float>(h >> 9) + 0.5f) * (1.f / 8388608.f);
}

} // namespace

// Exactly 1 at 1 (1.055 - 0.055 rounds below 1 in float, which would make white 254).
float srgbOetf(float v) {
    if (v >= 1.f)
        return 1.f;
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}

std::array<float, 3> tonemapDisplay(const float rgb[3], float exposureEV) {
    const float scale = std::exp2(exposureEV);
    float hdr[3];
    for (int i = 0; i < 3; ++i)
        hdr[i] = std::isfinite(rgb[i]) ? rgb[i] * scale : 0.f;

    // Negative light (in AP1, after the input matrix) is clamped to 0: the curve's lower root lies below 0
    // and its other branch rises again, so -1 grey would come out white. Not before the matrix: the
    // placeholder's inverse tonemap feeds negative RGB whose AP1 components are all positive (at least
    // 0.0033).
    float fitted[3];
    for (int i = 0; i < 3; ++i)
        fitted[i] = rrtAndOdtFit(
            std::max(kInput[i][0] * hdr[0] + kInput[i][1] * hdr[1] + kInput[i][2] * hdr[2], 0.f));

    std::array<float, 3> display{};
    for (int i = 0; i < 3; ++i) {
        const float linear =
            kOutput[i][0] * fitted[0] + kOutput[i][1] * fitted[1] + kOutput[i][2] * fitted[2];
        display[i] = srgbOetf(std::clamp(linear, 0.f, 1.f));
    }
    return display;
}

float tonemapDither(std::uint32_t x, std::uint32_t y) {
    const std::uint32_t first = pcgHash(x ^ pcgHash(y));
    const std::uint32_t second = pcgHash(first);
    return 0.5f * (openUnit(first) + openUnit(second)) - 0.5f;
}

std::array<std::uint8_t, 3> tonemapReference(const float rgb[3], float exposureEV, std::uint32_t x,
                                             std::uint32_t y) {
    const std::array<float, 3> display = tonemapDisplay(rgb, exposureEV);
    const float dither = tonemapDither(x, y);
    std::array<std::uint8_t, 3> bytes{};
    for (int i = 0; i < 3; ++i)
        bytes[i] =
            static_cast<std::uint8_t>(std::clamp(std::floor(display[i] * 255.f + dither + 0.5f), 0.f, 255.f));
    return bytes;
}

} // namespace evo::post
