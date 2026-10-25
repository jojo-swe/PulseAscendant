#pragma once

// The 2026 card face: the demo's own glyph skeletons on a 5 x 7 grid, re-drawn as rounded strokes of a signed
// distance field. The skeletons follow the 8x8 font (font8x8.hpp) stroke for stroke: its capitals, digits and
// the cards' punctuation with the two-pixel verticals collapsed to one grid column, so the 1993 card and the
// 2026 card are the same letters. On top of the skeletons the face has what the bitmap never had:
//   - rounded joins and caps, and the bitmap's 45-degree chamfers turned into curves;
//   - optical sizing: small sizes are set heavier and looser, large ones lighter and tighter;
//   - kerning, from the skeletons' own left and right profiles (AV, TY, LT, "7," close up);
//   - tabular figures: every digit has the same advance and digits never kern against each other.
//
// Coordinates: a skeleton point is in grid units, x across (0..4 for most glyphs), y down from the cap line
// (0) to the baseline (6); a descender reaches 7. Laid out text is in pixels, y down.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace evo::text {

inline constexpr float kStrokeCapUnits = 6.f; // the cap line to the baseline, in grid units

struct StrokePoint {
    float x, y;
};

struct StrokeGlyph {
    char32_t code = 0;
    // The ink box across, in grid units (the strokes' centre lines); tabular figures all span 0..4.
    float left = 0, right = 0;
    // Open or closed polylines (a closed one repeats its first point), corners already rounded. A single
    // point is a dot.
    std::vector<std::vector<StrokePoint>> strokes;
    // The skeleton as authored, before the corners were rounded (what follows the 8x8 glyph).
    std::vector<std::vector<StrokePoint>> skeleton;
    // Whether the 8x8 font has the same character, which the skeleton then follows (every glyph but '→').
    bool fromBitmap = true;
};

// The face's glyph of a code point; nullptr when it has none (layout draws '?' instead).
const StrokeGlyph* strokeGlyph(char32_t code);
// Every code point the face draws.
std::vector<char32_t> strokeFaceCodes();
// UTF-8 to code points; a malformed byte becomes '?'.
std::u32string decodeUtf8(std::string_view utf8);

// Optical sizing at a cap height in pixels: the stroke width in pixels, and the tracking (the space added
// after each glyph's ink, in grid units).
float strokeWidthPx(float capPx);
float strokeTrackingUnits(float capPx);
// The space's advance in grid units (before tracking).
inline constexpr float kStrokeSpaceUnits = 2.2f;

// The kerning between two code points in grid units, never positive: from the profiles of the two skeletons
// (half the mean excess white over the rows both have ink in, never closer than a fraction of the normal gap
// at the nearest row, at most kMaxKerningUnits). 0 between two digits (tabular figures), next to a space, and
// for glyphs that share no row.
inline constexpr float kMaxKerningUnits = 1.5f;
float strokeKerningUnits(char32_t left, char32_t right);

// One rounded stroke of laid-out text: the capsule round the segment a-b, in pixels.
struct StrokeSegment {
    float ax, ay, bx, by;
    float radius;
};

// Laid-out text. `width` runs from the pen's start to the right end of the last glyph's ink centre line
// (add strokeWidthPx(capPx) / 2 at each end for the ink).
struct StrokeRun {
    std::vector<StrokeSegment> segments;
    float width = 0;
};

// Lays a single line out at a cap height with its pen at (x, baselineY).
StrokeRun layoutStrokes(std::string_view utf8, float capPx, float x, float baselineY);
// The run's width without laying it out.
float strokeTextWidth(std::string_view utf8, float capPx);

// The coverage of the strokes at a point (a pixel centre is at +0.5): the union of the capsules with a
// one-pixel ramp at the edge, clamp(0.5 - (distance - radius), 0, 1) for the nearest. The card shader computes
// the same.
float strokeCoverage(std::span<const StrokeSegment> segments, float x, float y);

} // namespace evo::text
