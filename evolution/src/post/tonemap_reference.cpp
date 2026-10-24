#include "post/tonemap_reference.hpp"

#include "rt/random.hpp"

#include <algorithm>
#include <cmath>

namespace evo::post {

namespace {

// Khronos PBR Neutral's constants (pbrNeutral.glsl). Must match shaders/post/neutral.glsl.
constexpr float kStartCompression = 0.8f - 0.04f;
constexpr float kDesaturation = 0.15f;

// A uniform in (0, 1) from the hash's top 23 bits plus a half step: exact in float, never 0 or 1.
float openUnit(std::uint32_t h) {
    return (static_cast<float>(h >> 9) + 0.5f) * (1.f / 8388608.f);
}

} // namespace

std::array<float, 3> pbrNeutral(const std::array<float, 3>& rgb) {
    std::array<float, 3> color = rgb;
    const float x = std::min({color[0], color[1], color[2]});
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    for (float& c : color)
        c -= offset;

    const float peak = std::max({color[0], color[1], color[2]});
    if (peak < kStartCompression)
        return color;

    constexpr float d = 1.f - kStartCompression;
    const float newPeak = 1.f - d * d / (peak + d - kStartCompression);
    const float g = 1.f - 1.f / (kDesaturation * (peak - newPeak) + 1.f);
    for (float& c : color)
        c = (c * (newPeak / peak)) * (1.f - g) + newPeak * g; // mix(color * newPeak / peak, newPeak, g)
    return color;
}

// Exactly 1 at 1 (1.055 - 0.055 rounds below 1 in float, which would make white 254).
float srgbOetf(float v) {
    if (v >= 1.f)
        return 1.f;
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}

std::array<float, 3> tonemapDisplay(const float rgb[3], float exposureEV) {
    const float scale = std::exp2(exposureEV);
    // Negative light is clamped to 0 per component: PBR Neutral is defined on non-negative light (a negative
    // minimum would make its offset negative and lift the other channels).
    std::array<float, 3> hdr{};
    for (int i = 0; i < 3; ++i)
        hdr[i] = std::isfinite(rgb[i]) ? std::max(rgb[i] * scale, 0.f) : 0.f;

    const std::array<float, 3> linear = pbrNeutral(hdr);
    std::array<float, 3> display{};
    for (int i = 0; i < 3; ++i)
        display[i] = srgbOetf(std::clamp(linear[i], 0.f, 1.f));
    return display;
}

float tonemapDither(std::uint32_t x, std::uint32_t y) {
    const std::uint32_t first = rt::pcgHash(x ^ rt::pcgHash(y));
    const std::uint32_t second = rt::pcgHash(first);
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
