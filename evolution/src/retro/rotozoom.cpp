// LENS's 1993 side: the rotozoomer's texture, its view per frame and its palette. The shader
// (shaders/retro/rotozoom.comp) does the per-pixel work; see retro/rotozoom.hpp for the formulas.

#include "retro/rotozoom.hpp"

#include "text/font8x8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <vector>

namespace evo::retro {

namespace {

constexpr int kSize = static_cast<int>(kRotozoomSize);

// Whether texel (u, v) is ink of the logo (no wrapping: the logo sits well inside the tile).
bool logoInk(int u, int v) {
    const int x = u - kRotozoomLogoX, y = v - kRotozoomLogoY;
    const std::string_view logo = kRotozoomLogo;
    if (x < 0 || y < 0 || x >= 8 * static_cast<int>(logo.size()) || y >= 16)
        return false;
    const text::GlyphRows& rows = text::glyph(logo[static_cast<std::size_t>(x / 8)]);
    return (rows[static_cast<std::size_t>(y / 2)] & (0x80u >> (x % 8))) != 0;
}

// The shade (0 dark .. 23 light) of texel (i, j) of a 16x16 checker tile: a bevel three texels wide, light
// along the top and left, dark along the bottom and right (which win at the corners where they meet), and
// inside a diagonal gradient from light at the top left to dark at the bottom right. Uses all 24 shades.
int tileShade(int i, int j) {
    if (std::max(i, j) >= 13)
        return 15 - std::max(i, j); // 2, 1, 0 towards the edge
    if (std::min(i, j) <= 2)
        return 23 - std::min(i, j);    // 21, 22, 23 towards the edge
    return 20 - (i + j - 6) * 17 / 18; // 20 .. 3
}

int fixed16(double x) {
    return static_cast<int>(std::lround(x * 65536));
}

// Linear from `from` to `to` over `count` entries starting at `first`.
void ramp(Palette& palette, std::size_t first, std::size_t count, const std::array<int, 3>& from,
          const std::array<int, 3>& to) {
    for (std::size_t i = 0; i < count; ++i) {
        const double t = count > 1 ? static_cast<double>(i) / static_cast<double>(count - 1) : 0;
        const auto channel = [&](int c) {
            return static_cast<std::uint8_t>(std::lround(from[c] + (to[c] - from[c]) * t));
        };
        palette[first + i] = rgba(channel(0), channel(1), channel(2));
    }
}

} // namespace

EffectTexture rotozoomTexture() {
    EffectTexture texture{kRotozoomSize, kRotozoomSize,
                          std::vector<std::uint8_t>(std::size_t{kRotozoomSize} * kRotozoomSize)};
    for (int v = 0; v < kSize; ++v)
        for (int u = 0; u < kSize; ++u) {
            int index;
            bool outline = false;
            for (int dv = -1; dv <= 1; ++dv)
                for (int du = -1; du <= 1; ++du)
                    outline = outline || logoInk(u + du, v + dv);
            if (logoInk(u, v)) {
                index = kRotozoomInk + (v - kRotozoomLogoY); // the chrome, row by row
            } else if (outline) {
                index = kRotozoomOutline;
            } else if (logoInk(u - 2, v - 2)) {
                index = kRotozoomShadow;
            } else {
                const int base = (u / 16 + v / 16) % 2 == 0 ? kRotozoomTileA : kRotozoomTileB;
                index = base + tileShade(u % 16, v % 16);
            }
            texture.texels[static_cast<std::size_t>(v * kSize + u)] = static_cast<std::uint8_t>(index);
        }
    return texture;
}

RotozoomView rotozoomView(double t) {
    const double angle = 0.95 * std::sin(0.33 * t) + 0.08 * t;
    const double zoom = 0.7 + 0.4 * std::sin(0.41 * t - 1.2);
    const auto wrap = [](double x) { return x - 128 * std::floor(x / 128); };
    const double centreU = wrap(64 + 23 * t), centreV = wrap(64 + 40 * std::sin(0.29 * t));
    RotozoomView view{};
    view.stepXU = fixed16(zoom * std::cos(angle));
    view.stepXV = fixed16(zoom * std::sin(angle));
    view.stepYU = fixed16(-1.2 * zoom * std::sin(angle));
    view.stepYV = fixed16(1.2 * zoom * std::cos(angle));
    view.originU = fixed16(centreU) - 160 * view.stepXU - 100 * view.stepYU;
    view.originV = fixed16(centreV) - 160 * view.stepXV - 100 * view.stepYV;
    const double phase = t / 6.2 - std::floor(t / 6.2);
    const double tri = phase < 0.5 ? 2 * phase : 2 - 2 * phase;
    view.lensX = static_cast<std::int32_t>(std::floor(48 + 224 * tri + 0.5));
    view.lensY = static_cast<std::int32_t>(
        std::floor(159 - 119 * std::abs(std::sin(std::numbers::pi * t / 2.3)) + 0.5));
    return view;
}

void rotozoomPalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    palette.fill(rgba(0, 0, 0));
    // The checker: deep blue to sky, and plum to pink; darker than the chrome, so the logo stands out.
    ramp(palette, kRotozoomTileA, kRotozoomTileShades, {6, 10, 44}, {84, 150, 232});
    ramp(palette, kRotozoomTileB, kRotozoomTileShades, {34, 4, 42}, {214, 104, 196});
    // The chrome, row by row: bright top, gold, a darker horizon, bright again below it, warm at the foot.
    ramp(palette, kRotozoomInk, 5, {255, 255, 232}, {255, 214, 120});
    ramp(palette, kRotozoomInk + 5, 3, {248, 186, 92}, {235, 160, 80});
    ramp(palette, kRotozoomInk + 8, 6, {255, 244, 210}, {255, 190, 96});
    palette[kRotozoomOutline] = rgba(20, 12, 40);
    palette[kRotozoomShadow] = rgba(6, 4, 16);
    // Through the glass: every texture colour lighter and bluer.
    for (std::size_t i = 0; i < kRotozoomColours; ++i) {
        const std::uint32_t c = palette[kRotozoomTexture + i];
        const auto tint = [&](int shift, int add) {
            return static_cast<std::uint8_t>(std::min(255, static_cast<int>(c >> shift & 0xff) + add));
        };
        palette[kRotozoomTexture + kRotozoomGlass + i] = rgba(tint(0, 26), tint(8, 42), tint(16, 70));
    }
    // The rim's highlight ramp, dark blue-grey to white.
    ramp(palette, kRotozoomRim, kRotozoomRimShades, {20, 30, 60}, {255, 255, 255});
    palette[kRotozoomWhite] = rgba(255, 255, 255);
}

} // namespace evo::retro
