#pragma once

#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "show/timeline.hpp"

#include <cstdint>

namespace evo::retro {

// LENS (chapter 6): the classic rotozoomer with a bouncing lens over it (shaders/retro/rotozoom.comp).
//
// The texture: an original 128x128 tile built on the CPU once (rotozoomTexture) and read by the shader from
// an R8_UINT storage image the retro stage owns (EffectInfo::texture). A checkerboard of bevelled 16x16
// tiles in two colours with the word EVOLUTION stamped across its middle from the demo's 8x8 font, each
// glyph row doubled (7x14-texel letters, 8 texels apart), in a chrome gradient with a dark outline and a drop
// shadow. Its texels are palette indices.
//
// The rotozoom: at time t = pc.retroSeconds the texture is turned by angle(t) and scaled so that a texel is
// 1 / zoom(t) pixels wide and 1 / (1.2 zoom(t)) rows tall (square on the 4:3 screen), with texture point
// centre(t) at the screen's middle. As in 1993 the per-pixel work is integer: the shader turns these into
// 16.16 fixed-point texture steps per pixel across and per row down (rotozoomView), and texel (x, y) of the
// screen reads texture ((u >> 16) & 127, (v >> 16) & 127), wrapping round the tile.
//   angle(t)  = 0.95 sin(0.33 t) + 0.08 t                    radians: a swing, turning slowly
//   zoom(t)   = 0.7 + 0.4 sin(0.41 t - 1.2)                  texels per pixel across, 0.3..1.1
//   centre(t) = (64 + 23 t, 64 + 40 sin(0.29 t))             texels
//
// The lens: a glass disc 48 pixels in radius (40 rows tall, round on the 4:3 screen) whose centre bounces
// across the screen, at whole pixels:
//   lens(t) = (floor(48 + 224 tri(t / 6.2) + 0.5), floor(159 - 119 |sin(pi t / 2.3)| + 0.5))
// where tri runs 0 -> 1 -> 0 over each unit (so the lens runs to and fro and bounces on the floor). A pixel
// at offset (dx, dy) from the centre lies at r = |(dx, 1.2 dy)| / 48 in the lens. Inside (r < 1) it shows
// the rotozoomed point at offset (dx, dy) q(r), q(r) = 1/2 + (1 - sqrt(1 - r^2)) / 2 (the refraction table: a
// spherical cap's sag), so the middle is magnified 2x and the edge meets the outside without a seam, seen
// through tinted glass (its index + kRotozoomGlass). That point is rounded to 1/256 pixel (24.8 fixed
// point) and goes through the view like a pixel: origin + x stepX + y stepY, the fraction's share >> 8. The
// outer 4 pixels of the radius are the rim, shaded from the highlight ramp by the light from the upper left
// (its outermost pixel darker); a small highlight sits in the upper left of the glass, round (-0.4, -0.4) in
// the lens's units: white within 0.05, the rim ramp's shade 13 within 0.12.
inline constexpr std::uint32_t kRotozoomRetroRate = 35;
inline constexpr const char* kRotozoomShader = "retro_rotozoom.comp.spv";
inline constexpr std::uint32_t kRotozoomSize = 128; // the texture tile, texels each way
inline constexpr int kRotozoomLensRadius = 48;      // pixels across

// The palette:
//   0           black
//   16..79      the texture's 64 colours (the texels are these indices): kRotozoomTileA + 0..23 the first
//               checker colour (blue), kRotozoomTileB + 0..23 the second (violet), each from its bevel's
//               dark edge to its light edge; kRotozoomInk + 0..13 the logo's chrome, one per texel row of
//               the letters; kRotozoomOutline and kRotozoomShadow round and under the letters
//   80..143     the same 64 seen through the lens (index + kRotozoomGlass): lighter and bluer
//   144..159    the rim's highlight ramp, dark to white
//   255         white, the highlight's core
inline constexpr std::uint8_t kRotozoomTexture = 16;
inline constexpr std::uint8_t kRotozoomColours = 64;
inline constexpr std::uint8_t kRotozoomTileA = kRotozoomTexture;
inline constexpr std::uint8_t kRotozoomTileB = kRotozoomTexture + 24;
inline constexpr std::uint8_t kRotozoomTileShades = 24;
inline constexpr std::uint8_t kRotozoomInk = kRotozoomTexture + 48;
inline constexpr std::uint8_t kRotozoomOutline = kRotozoomTexture + 62;
inline constexpr std::uint8_t kRotozoomShadow = kRotozoomTexture + 63;
inline constexpr std::uint8_t kRotozoomGlass = kRotozoomColours; // added to a texel seen through the lens
inline constexpr std::uint8_t kRotozoomRim = 144;
inline constexpr std::uint8_t kRotozoomRimShades = 16;
inline constexpr std::uint8_t kRotozoomWhite = 255;

// The logo: kRotozoomLogo stamped with its first glyph's top-left texel at (kRotozoomLogoX, kRotozoomLogoY),
// glyph i's column c and row r of the font on texels (x + 8 i + c, y + 2 r) and (x + 8 i + c, y + 2 r + 1).
inline constexpr const char* kRotozoomLogo = "EVOLUTION";
inline constexpr int kRotozoomLogoX = 28;
inline constexpr int kRotozoomLogoY = 57;

// The texture, rows top first (kRotozoomSize x kRotozoomSize palette indices).
EffectTexture rotozoomTexture();

// One frame's view as the shader builds it from t: texture point (u, v) of pixel (x, y) is
// origin + x * stepX + y * stepY, in 16.16 fixed point (wrapping); the lens centre in whole pixels. The
// shader computes the same in single precision, so a texel boundary can fall a pixel differently there.
struct RotozoomView {
    std::int32_t originU, originV;
    std::int32_t stepXU, stepXV; // per pixel across
    std::int32_t stepYU, stepYV; // per row down
    std::int32_t lensX, lensY;
};
RotozoomView rotozoomView(double seconds);

// The EffectInfo parts of LENS (see retro/effects.hpp).
void rotozoomPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
