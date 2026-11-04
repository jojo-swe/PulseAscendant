// The 2026 card face (text/stroke_font.hpp) on the CPU: every glyph the cards use has strokes; the skeletons
// follow the 8x8 bitmap; the stroke width of 'E' at 64 px; the kerning pairs; tabular figures; and the year at
// twice the body's cap height in a 2160p card.

#include "render/epoch_cards.hpp"
#include "show/cues.hpp"
#include "text/font8x8.hpp"
#include "text/stroke_font.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

std::string utf8(char32_t c) {
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

// Every text a card can show without a scene: the cue sheet's lines, the 1993 cards and the credits.
std::vector<std::string> cardTexts() {
    std::vector<std::string> texts = {show::kCreditsLine};
    for (const show::Cue& cue : show::cues) {
        texts.emplace_back(cue.thenCard);
        texts.emplace_back(cue.nowCard);
        texts.push_back(render::thenCardText(cue.effect));
    }
    return texts;
}

void testFaceCoversTheCards() {
    int glyphs = 0;
    for (const std::string& text : cardTexts())
        for (const char32_t code : text::decodeUtf8(text)) {
            if (code == U' ')
                continue;
            const text::StrokeGlyph* g = text::strokeGlyph(code);
            if (g == nullptr)
                std::printf("no stroke glyph for U+%04X in \"%s\"\n", static_cast<unsigned>(code), text.c_str());
            CHECK(g != nullptr && !g->strokes.empty());
            for (const auto& line : g->strokes)
                CHECK(!line.empty());
            ++glyphs;
        }
    std::printf("face: %zu glyphs; the cards' %d glyphs all drawn\n", text::strokeFaceCodes().size(), glyphs);
    CHECK(text::strokeGlyph(U'a') == nullptr); // capitals only: layout draws '?' for it
    CHECK(text::decodeUtf8("\xC2\xB7\xC3\x97\xE2\x86\x92") == std::u32string(U"·×→"));
    CHECK(text::decodeUtf8("A\xFF") == std::u32string(U"A?"));
}

// The 5x7 grid against the bitmap: column 0 is the bitmap's 0-1, 4 its 5-6, 1..3 its 2..4; a point between
// two grid lines may sit on either.
std::vector<int> bitmapColumns(float x) {
    std::vector<int> columns;
    for (const float g : {std::floor(x), std::ceil(x)}) {
        const int i = static_cast<int>(g);
        if (i <= 0)
            columns.insert(columns.end(), {0, 1});
        else if (i >= 4)
            columns.insert(columns.end(), {5, 6});
        else
            columns.push_back(i + 1);
    }
    return columns;
}

void testSkeletonsFollowTheBitmap() {
    int points = 0;
    for (const char32_t code : text::strokeFaceCodes()) {
        const text::StrokeGlyph& g = *text::strokeGlyph(code);
        if (!g.fromBitmap) {
            CHECK(code == U'→'); // the arrow alone: the bitmap writes "->"
            continue;
        }
        const std::vector<std::uint8_t> bitmapCode = text::glyphCodes(utf8(code));
        CHECK((bitmapCode.size() == 1 && bitmapCode[0] != '?') || code == U'?');
        const text::GlyphRows& rows = text::glyph(static_cast<char>(bitmapCode[0]));
        for (const auto& line : g.skeleton)
            for (const text::StrokePoint& p : line) {
                bool ink = false;
                for (const float ry : {std::floor(p.y), std::ceil(p.y)}) {
                    const int row = std::clamp(static_cast<int>(ry), 0, 7);
                    for (const int column : bitmapColumns(p.x))
                        ink = ink || (rows[row] & (0x80u >> column)) != 0;
                }
                if (!ink)
                    std::printf("'%s': (%.1f, %.1f) is on no ink of the bitmap glyph\n", utf8(code).c_str(), p.x,
                                p.y);
                CHECK(ink);
                ++points;
            }
    }
    std::printf("skeletons: %d points, every one on the 8x8 glyph's ink\n", points);
}

// The coverage summed across a row of pixel centres: the width of the ink there.
float inkAcross(const text::StrokeRun& run, float y, float from, float to) {
    float sum = 0.f;
    for (float x = std::floor(from) + 0.5f; x < to; x += 1.f)
        sum += text::strokeCoverage(run.segments, x, y);
    return sum;
}

void testStrokeWidth() {
    const float cap = 64.f, unit = cap / text::kStrokeCapUnits;
    const text::StrokeRun e = text::layoutStrokes("E", cap, 20.f, 200.f);
    // Between the top and middle bars, the stem alone: at 1.5 grid units below the cap line.
    const float y = std::floor(200.f - (text::kStrokeCapUnits - 1.5f) * unit) + 0.5f;
    const float width = inkAcross(e, y, 0.f, 20.f + 2.f * unit);
    const float expected = text::strokeWidthPx(cap);
    std::printf("'E' at %.0f px: stem %.3f px wide, %.3f px expected\n", cap, width, expected);
    CHECK(std::abs(width - expected) <= 0.05f * expected);
    // Optical sizing: heavier relative to the cap at small sizes, tracking looser.
    CHECK(text::strokeWidthPx(12.f) / 12.f > text::strokeWidthPx(48.f) / 48.f);
    CHECK(text::strokeTrackingUnits(12.f) > text::strokeTrackingUnits(48.f));
}

void testKerning() {
    for (const char* pair : {"AV", "TY", "LT", "7,"}) {
        const std::u32string codes = text::decodeUtf8(pair);
        const float kern = text::strokeKerningUnits(codes[0], codes[1]);
        std::printf("kern %s: %.3f units\n", pair, kern);
        CHECK(kern < 0.f && kern >= -text::kMaxKerningUnits);
        // The layout applies it: the pair is narrower than its glyphs set apart.
        const float cap = 30.f, unit = cap / text::kStrokeCapUnits;
        const float apart = text::strokeTextWidth(std::string(1, pair[0]), cap) +
                            text::strokeTextWidth(std::string(1, pair[1]), cap) +
                            text::strokeTrackingUnits(cap) * unit;
        CHECK(std::abs(text::strokeTextWidth(pair, cap) - (apart + kern * unit)) < 1e-3f);
    }
    CHECK(text::strokeKerningUnits(U'H', U'H') == 0.f); // two stems: the normal gap
    CHECK(text::strokeKerningUnits(U'A', U' ') == 0.f);
    for (char32_t a = U'0'; a <= U'9'; ++a)
        for (char32_t b = U'0'; b <= U'9'; ++b)
            CHECK(text::strokeKerningUnits(a, b) == 0.f); // tabular
    // Rounds sit a touch closer than stems; nothing ever kerns apart.
    CHECK(text::strokeKerningUnits(U'O', U'O') <= 0.f && text::strokeKerningUnits(U'O', U'O') > -0.5f);
}

void testTabularFigures() {
    const float cap = 24.f;
    const float one = text::strokeTextWidth("0", cap);
    for (char d = '0'; d <= '9'; ++d)
        CHECK(text::strokeTextWidth(std::string(1, d), cap) == one);
    CHECK(text::strokeTextWidth("1111111", cap) == text::strokeTextWidth("8080808", cap));
    CHECK(text::strokeGlyph(U'1')->left == 0.f && text::strokeGlyph(U'1')->right == 4.f);
    std::printf("tabular figures: every digit %.2f px at %.0f px\n", one, cap);
}

// The year's cap height against the body's, measured from the ink of a 2160p card: each line's ink height
// (the coverage summed down its rows, so it is sub-pixel) less its stroke width.
void testYearSize() {
    const render::NowCardLayout card = render::nowCardLayout("2026 \xC2\xB7 THE STUDIO", 3840, 2160);
    CHECK(card.lines.size() == 2 && card.lines[0] == "2026" && card.lines[1] == "THE STUDIO");
    std::vector<float> rows;
    for (int y = card.y0; y < card.y1; ++y) {
        float most = 0.f;
        for (int x = card.x0; x < card.x1; ++x)
            most = std::max(most, text::strokeCoverage(card.segments, x + 0.5f, y + 0.5f));
        rows.push_back(most);
    }
    // The runs of inked rows: the year, then the body line (the rule is not a stroke).
    std::vector<float> heights;
    float run = 0.f;
    for (const float r : rows) {
        if (r > 0.f) {
            run += r;
        } else if (run > 0.f) {
            heights.push_back(run);
            run = 0.f;
        }
    }
    if (run > 0.f)
        heights.push_back(run);
    CHECK(heights.size() == 2);
    const float year = heights[0] - text::strokeWidthPx(card.yearCap);
    const float body = heights[1] - text::strokeWidthPx(card.bodyCap);
    std::printf("2160p card: year cap %.2f px, body cap %.2f px, ratio %.3f\n", year, body, year / body);
    CHECK(std::abs(year / body - 2.f) <= 0.05f);
}

} // namespace

int main() {
    try {
        testFaceCoversTheCards();
        testSkeletonsFollowTheBitmap();
        testStrokeWidth();
        testKerning();
        testTabularFigures();
        testYearSize();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED: %s (after %d checks)\n", e.what(), checksPassed);
        return 1;
    }
    std::printf("stroke_font: all %d checks passed\n", checksPassed);
    return 0;
}
