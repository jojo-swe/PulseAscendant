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

namespace {

float luminanceOf(const std::array<float, 3>& c) {
    return c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f;
}

float clamp01(float v) {
    return std::clamp(v, 0.f, 1.f);
}

// The sRGB EOTF of an encoded value in [0, 1] (color.glsl's srgbEotf).
float srgbEotf(float v) {
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float unpackChannel(std::uint32_t rgba, int channel) {
    return static_cast<float>((rgba >> (8 * channel)) & 0xffu) / 255.f;
}

constexpr float kMidGrey = 0.18f;

} // namespace

float sCurve(float x, float k) {
    if (x <= kMidGrey) {
        const float t = x / kMidGrey;
        return kMidGrey * t * (1.f + k * (t - 1.f));
    }
    const float u = (1.f - x) / (1.f - kMidGrey);
    return 1.f - (1.f - kMidGrey) * u * (1.f + k * (u - 1.f));
}

std::array<float, 3> oklab(const std::array<float, 3>& c) {
    float l = 0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2];
    float m = 0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2];
    float s = 0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2];
    l = std::pow(std::max(l, 0.f), 1.f / 3.f);
    m = std::pow(std::max(m, 0.f), 1.f / 3.f);
    s = std::pow(std::max(s, 0.f), 1.f / 3.f);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
            1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

GradePaletteTable gradePaletteTable(const GradePalette& palette) {
    GradePaletteTable table{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const std::array<float, 3> linear = {srgbEotf(unpackChannel(palette[i], 0)),
                                             srgbEotf(unpackChannel(palette[i], 1)),
                                             srgbEotf(unpackChannel(palette[i], 2))};
        const std::array<float, 3> lab = oklab(linear);
        table.lab[i] = {lab[0], lab[1], lab[2], 0.f};
        table.color[i] = palette[i];
    }
    return table;
}

float grainNoise(std::uint32_t x, std::uint32_t y, std::uint32_t frame) {
    const std::uint32_t first = rt::pcgHash(x ^ rt::pcgHash(y ^ rt::pcgHash(frame ^ 0x6772616eu)));
    const std::uint32_t second = rt::pcgHash(first);
    return (openUnit(first) + openUnit(second) - 1.f) * 2.4494897f;
}

std::array<float, 3> gradeLinear(const std::array<float, 3>& display, const Grade& grade, std::uint32_t x,
                                 std::uint32_t y) {
    std::array<float, 3> c = display;
    if (grade.contrast != 0.f) {
        const float yl = luminanceOf(c);
        if (yl > 0.f) {
            const float scale = sCurve(yl, grade.contrast) / yl;
            for (float& v : c)
                v = std::min(v * scale, 1.f);
        }
    }
    if (grade.lift[0] != 0.f || grade.lift[1] != 0.f || grade.lift[2] != 0.f)
        for (int i = 0; i < 3; ++i)
            c[i] = grade.lift[i] + c[i] * (1.f - grade.lift[i]);
    if (grade.splitTone != 0.f) {
        constexpr std::array<float, 3> warm = {1.f, -0.19630872f, -1.f};
        const float tone = grade.splitTone * (1.f - 2.f * luminanceOf(c));
        for (int i = 0; i < 3; ++i)
            c[i] = clamp01(c[i] * (1.f + tone * warm[i]));
    }
    if (grade.shadowDesaturation != 0.f) {
        const float yl = luminanceOf(c);
        const float shadow = 1.f - std::min(2.f * yl, 1.f);
        const float keep = 1.f - grade.shadowDesaturation * shadow * shadow;
        for (float& v : c)
            v = clamp01(yl + (v - yl) * keep);
    }
    if (grade.grain != 0.f) {
        const float n = 1.f + grade.grain * grainNoise(x, y, grade.grainFrame);
        for (float& v : c)
            v = clamp01(v * n);
    }
    return c;
}

std::array<float, 3> gradeDisplay(const float rgb[3], float exposureEV, const Grade& grade,
                                  const GradePaletteTable* palette, std::uint32_t x, std::uint32_t y,
                                  std::uint32_t width, std::uint32_t height) {
    const float scale = std::exp2(exposureEV);
    std::array<float, 3> hdr{};
    for (int i = 0; i < 3; ++i)
        hdr[i] = std::isfinite(rgb[i]) ? std::max(rgb[i] * scale, 0.f) : 0.f;
    if (grade.vignette != 0.f) {
        const float w = static_cast<float>(width), h = static_cast<float>(height);
        const float u = (2.f * static_cast<float>(x) + 1.f) / w - 1.f;
        const float v = (2.f * static_cast<float>(y) + 1.f) / h - 1.f;
        const float falloff = 1.f - grade.vignette * ((u * u * w * w + v * v * h * h) / (w * w + h * h));
        for (float& c : hdr)
            c *= falloff;
    }
    std::array<float, 3> linear = pbrNeutral(hdr);
    for (float& c : linear)
        c = clamp01(c);
    linear = gradeLinear(linear, grade, x, y);
    std::array<float, 3> display{};
    for (int i = 0; i < 3; ++i)
        display[i] = srgbOetf(linear[i]);
    if (grade.palette != 0.f) {
        const std::array<float, 3> lab = oklab(linear);
        std::size_t best = 0;
        float bestDistance = 1e30f;
        for (std::size_t i = 0; i < 256; ++i) {
            const float d0 = lab[0] - palette->lab[i][0], d1 = lab[1] - palette->lab[i][1],
                        d2 = lab[2] - palette->lab[i][2];
            const float distance = d0 * d0 + d1 * d1 + d2 * d2;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        for (int i = 0; i < 3; ++i)
            display[i] = display[i] * (1.f - grade.palette) + unpackChannel(palette->color[best], i) * grade.palette;
    }
    return display;
}

std::array<std::uint8_t, 3> tonemapGraded(const float rgb[3], float exposureEV, const Grade& grade,
                                          const GradePaletteTable* palette, std::uint32_t x, std::uint32_t y,
                                          std::uint32_t width, std::uint32_t height) {
    const std::array<float, 3> display = gradeDisplay(rgb, exposureEV, grade, palette, x, y, width, height);
    const float dither = tonemapDither(x, y) * (grade.palette != 0.f ? 1.f - grade.palette : 1.f);
    std::array<std::uint8_t, 3> bytes{};
    for (int i = 0; i < 3; ++i)
        bytes[i] =
            static_cast<std::uint8_t>(std::clamp(std::floor(display[i] * 255.f + dither + 0.5f), 0.f, 255.f));
    return bytes;
}

} // namespace evo::post
