#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace evo::text {

// The demo's own 8x8 font: ASCII 32..126 plus a middle dot at code 127 (the cue sheet's "·"). Each glyph is
// eight rows, top first; in a row the most significant bit is the leftmost pixel. Glyphs are at most 7 pixels
// wide (column 7 is the gap to the next character), capitals and digits stand on row 6 (the baseline) and
// descenders reach row 7.
using GlyphRows = std::array<std::uint8_t, 8>;

inline constexpr std::uint8_t kFirstGlyph = 32;
inline constexpr std::uint8_t kMiddleDot = 127;
inline constexpr int kGlyphCount = 96; // codes 32..127

// The rows of a glyph; a code outside 32..127 gives the '?' glyph.
const GlyphRows& glyph(char code);

// The glyph codes of a UTF-8 string: printable ASCII as is, '\n' kept for the layout, U+00B7 "·" as the
// middle dot, U+00D7 "×" as 'x', U+2192 "→" as "->", and '?' for any other character or malformed byte.
std::vector<std::uint8_t> glyphCodes(std::string_view utf8);

// The width in pixels of the widest line of a UTF-8 string drawn at an integer scale: 8 * scale per glyph.
int textWidth(std::string_view utf8, int scale);

} // namespace evo::text
