#pragma once

// "HI. YES, YOU." as ASCENDANT's waypoint (the hold one bar before D). The lit pixels of that line in the
// 8x8 font (text/font8x8.cpp), copied here so shaders/modern/greeting_targets.glsl can hold the same table.
// The layout is the logo's (logo_targets.hpp): 13 glyphs side by side, lit pixels numbered row-major,
// extruded kGreetingDepth voxels and filled by the same jittered samples. The lit pixels occupy columns
// 0..99 and rows 0..7, so the formation is centred on column 49.5 and row 3.5.

#include "modern/scenes/logo_targets.hpp"

#include <array>
#include <bit>
#include <cstdint>

namespace evo::modern {

inline constexpr const char* kGreetingText = "HI. YES, YOU.";
inline constexpr std::uint32_t kGreetingGlyphs = 13, kGreetingRows = 8;
inline constexpr std::uint32_t kGreetingColumns = 8 * kGreetingGlyphs;
inline constexpr std::uint32_t kGreetingDepth = 6;

// kGreetingBitmap[row][glyph]: the glyphs' rows, top first, most significant bit leftmost.
inline constexpr std::array<std::array<std::uint8_t, kGreetingGlyphs>, kGreetingRows> kGreetingBitmap{{
    {0xC6, 0xFC, 0x00, 0x00, 0xCC, 0xFE, 0x7C, 0x00, 0x00, 0xCC, 0x7C, 0xC6, 0x00},
    {0xC6, 0x30, 0x00, 0x00, 0xCC, 0xC0, 0xC6, 0x00, 0x00, 0xCC, 0xC6, 0xC6, 0x00},
    {0xC6, 0x30, 0x00, 0x00, 0xCC, 0xC0, 0xC0, 0x00, 0x00, 0xCC, 0xC6, 0xC6, 0x00},
    {0xFE, 0x30, 0x00, 0x00, 0x78, 0xFC, 0x7C, 0x00, 0x00, 0x78, 0xC6, 0xC6, 0x00},
    {0xC6, 0x30, 0x00, 0x00, 0x30, 0xC0, 0x06, 0x00, 0x00, 0x30, 0xC6, 0xC6, 0x00},
    {0xC6, 0x30, 0x30, 0x00, 0x30, 0xC0, 0xC6, 0x30, 0x00, 0x30, 0xC6, 0xC6, 0x30},
    {0xC6, 0xFC, 0x30, 0x00, 0x30, 0xFE, 0x7C, 0x30, 0x00, 0x30, 0x7C, 0x7C, 0x30},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00},
}};

constexpr std::uint32_t countGreetingPixels() {
    std::uint32_t n = 0;
    for (const auto& row : kGreetingBitmap)
        for (const std::uint8_t bits : row)
            n += static_cast<std::uint32_t>(std::popcount(bits));
    return n;
}
inline constexpr std::uint32_t kGreetingPixels = countGreetingPixels();
static_assert(kGreetingPixels == 225, "greeting_targets.glsl's kGreetingPixels");
inline constexpr float kGreetingCentreColumn = 49.5f, kGreetingCentreRow = 3.5f;

struct GreetingPixel {
    std::uint32_t column, row;
};

constexpr GreetingPixel greetingPixel(std::uint32_t j) {
    std::uint32_t seen = 0;
    for (std::uint32_t i = 0; i < kGreetingRows * kGreetingGlyphs; ++i) {
        const std::uint32_t bits = kGreetingBitmap[i / kGreetingGlyphs][i % kGreetingGlyphs];
        const auto n = static_cast<std::uint32_t>(std::popcount(bits));
        if (j < seen + n) {
            std::uint32_t k = j - seen;
            for (std::uint32_t c = 0; c < 8; ++c)
                if ((bits & (0x80u >> c)) != 0) {
                    if (k == 0)
                        return {(i % kGreetingGlyphs) * 8 + c, i / kGreetingGlyphs};
                    --k;
                }
        }
        seen += n;
    }
    return {0, 0};
}

// Particle `id`'s place in the greeting, in units where a pixel is `cell` wide (logoTarget's scheme).
inline Vec3 greetingTarget(std::uint32_t id, float cell) {
    constexpr std::uint32_t voxels = kGreetingPixels * kGreetingDepth;
    const std::uint32_t v = id % voxels, k = id / voxels;
    const GreetingPixel pixel = greetingPixel(v % kGreetingPixels);
    const std::uint32_t layer = v / kGreetingPixels;
    const std::uint32_t h = logoHash(v);
    const float jx = logoUnit(h + k * 0xD1B54A32u);
    const float jy = logoUnit(logoHash(h) + k * 0xABC98388u);
    const float jz = logoUnit(logoHash(h ^ 0x68E31DA4u) + k * 0x8CB92BA7u);
    return {(static_cast<float>(pixel.column) + jx - 0.5f - kGreetingCentreColumn) * cell,
            (kGreetingCentreRow - static_cast<float>(pixel.row) - jy + 0.5f) * cell,
            (3.f - static_cast<float>(layer) - jz) * cell};
}

} // namespace evo::modern
