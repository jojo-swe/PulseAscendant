#pragma once

// The "EVOLUTION" logo as particle targets (step 5, Task 17; ASCENDANT reuses them, Task 24): the 8x8 font's
// lit pixels of the word (text/font8x8.cpp, copied here so the GPU twin, shaders/modern/logo_targets.glsl,
// can hold the very same table; scene_particles_test checks it against text::glyph), extruded kLogoDepth
// voxels deep and filled by jittered sampling.
//
// Layout: the nine glyphs side by side make a 72 x 8 bitmap (column 8 g + c is bit 0x80 >> c of glyph g's
// row). Its kLogoPixels lit pixels are numbered row-major (rows top to bottom, columns left to right). A
// field of particles fills kLogoPixels * kLogoDepth voxels: particle id lands in voxel v = id % voxels, which
// is lit pixel v % kLogoPixels in layer v / kLogoPixels (layer 0 at the front), at sample k = id / voxels of
// that voxel: an R2 low-discrepancy point in 32-bit fixed point offset by a hash of v, so every voxel fills
// evenly and the CPU and the GPU agree bit for bit. Units are `cell`, the pixel's size: x right, y up,
// centred on the lit pixels' bounding box, the front face at z = +3 cells and the back at -3.

#include "modern/math.hpp"

#include <array>
#include <bit>
#include <cstdint>

namespace evo::modern {

inline constexpr const char* kLogoText = "EVOLUTION";
inline constexpr std::uint32_t kLogoGlyphs = 9, kLogoRows = 8, kLogoColumns = 8 * kLogoGlyphs;
inline constexpr std::uint32_t kLogoDepth = 6;

// kLogoBitmap[row][glyph]: the glyphs' rows, top first, most significant bit leftmost.
inline constexpr std::array<std::array<std::uint8_t, kLogoGlyphs>, kLogoRows> kLogoBitmap{{
    {0xFE, 0xC6, 0x7C, 0xC0, 0xC6, 0xFC, 0xFC, 0x7C, 0xC6},
    {0xC0, 0xC6, 0xC6, 0xC0, 0xC6, 0x30, 0x30, 0xC6, 0xE6},
    {0xC0, 0xC6, 0xC6, 0xC0, 0xC6, 0x30, 0x30, 0xC6, 0xF6},
    {0xFC, 0xC6, 0xC6, 0xC0, 0xC6, 0x30, 0x30, 0xC6, 0xDE},
    {0xC0, 0x6C, 0xC6, 0xC0, 0xC6, 0x30, 0x30, 0xC6, 0xCE},
    {0xC0, 0x38, 0xC6, 0xC0, 0xC6, 0x30, 0x30, 0xC6, 0xC6},
    {0xFE, 0x10, 0x7C, 0xFE, 0x7C, 0x30, 0xFC, 0x7C, 0xC6},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
}};

constexpr std::uint32_t countLogoPixels() {
    std::uint32_t n = 0;
    for (const auto& row : kLogoBitmap)
        for (const std::uint8_t bits : row)
            n += static_cast<std::uint32_t>(std::popcount(bits));
    return n;
}
inline constexpr std::uint32_t kLogoPixels = countLogoPixels();
static_assert(kLogoPixels == 234, "logo_targets.glsl's kLogoPixels");
// The lit pixels' bounding box is columns 0..70 and rows 0..6: its centre (logo_targets.glsl's constants).
inline constexpr float kLogoCentreColumn = 35.f, kLogoCentreRow = 3.f;

struct LogoPixel {
    std::uint32_t column, row;
};

// The j-th lit pixel (j < kLogoPixels).
constexpr LogoPixel logoPixel(std::uint32_t j) {
    std::uint32_t seen = 0;
    for (std::uint32_t i = 0; i < kLogoRows * kLogoGlyphs; ++i) {
        const std::uint32_t bits = kLogoBitmap[i / kLogoGlyphs][i % kLogoGlyphs];
        const auto n = static_cast<std::uint32_t>(std::popcount(bits));
        if (j < seen + n) {
            std::uint32_t k = j - seen;
            for (std::uint32_t c = 0; c < 8; ++c)
                if ((bits & (0x80u >> c)) != 0) {
                    if (k == 0)
                        return {(i % kLogoGlyphs) * 8 + c, i / kLogoGlyphs};
                    --k;
                }
        }
        seen += n;
    }
    return {0, 0};
}

// lowbias32, and a 32-bit fraction's top 24 bits as a float in [0, 1) (the shaders' logoHash, logoUnit).
constexpr std::uint32_t logoHash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

constexpr float logoUnit(std::uint32_t x) {
    return static_cast<float>(x >> 8) * (1.f / 16777216.f);
}

// Particle `id`'s target, in units where a pixel is `cell` wide (see the layout above).
inline Vec3 logoTarget(std::uint32_t id, float cell) {
    constexpr std::uint32_t voxels = kLogoPixels * kLogoDepth;
    const std::uint32_t v = id % voxels, k = id / voxels;
    const LogoPixel pixel = logoPixel(v % kLogoPixels);
    const std::uint32_t layer = v / kLogoPixels;
    const std::uint32_t h = logoHash(v);
    const float jx = logoUnit(h + k * 0xD1B54A32u);
    const float jy = logoUnit(logoHash(h) + k * 0xABC98388u);
    const float jz = logoUnit(logoHash(h ^ 0x68E31DA4u) + k * 0x8CB92BA7u);
    return {(static_cast<float>(pixel.column) + jx - 0.5f - kLogoCentreColumn) * cell,
            (kLogoCentreRow - static_cast<float>(pixel.row) - jy + 0.5f) * cell,
            (3.f - static_cast<float>(layer) - jz) * cell};
}

} // namespace evo::modern
