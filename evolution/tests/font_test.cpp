// The 8x8 font and its atlas, CPU side: invariants of the pixel art (not the art itself), the UTF-8 mapping
// of the cue sheet's texts onto the glyphs, and the atlas layout.

#include "text/atlas.hpp"
#include "text/font8x8.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

using evo::text::glyph;

int setPixels(const evo::text::GlyphRows& rows) {
    int count = 0;
    for (std::uint8_t row : rows)
        count += std::popcount(row);
    return count;
}

// The glyphs that may reach into column 7, the gap between characters (bit 0, as the MSB is column 0).
constexpr std::string_view kMayUseGap = "MW@mw";
constexpr std::string_view kDescenders = "gjpqy,;_";

void testEveryGlyphIsDrawn() {
    CHECK(setPixels(glyph(' ')) == 0);
    for (int c = 33; c <= 127; ++c)
        if (setPixels(glyph(static_cast<char>(c))) < 4)
            throw std::runtime_error("glyph " + std::to_string(c) + " has fewer than 4 set pixels");
    checksPassed += 95;
}

void testGapColumn() {
    for (int c = 32; c <= 127; ++c) {
        if (kMayUseGap.find(static_cast<char>(c)) != std::string_view::npos)
            continue;
        for (std::uint8_t row : glyph(static_cast<char>(c)))
            if (row & 1u)
                throw std::runtime_error("glyph " + std::to_string(c) + " uses column 7, the gap");
    }
    ++checksPassed;
}

// Capitals, digits and the lowercase letters without a descender stand on the baseline (row 6): row 7 is
// empty. The descenders reach it.
void testBaseline() {
    for (int c = 32; c <= 127; ++c) {
        const bool descends = kDescenders.find(static_cast<char>(c)) != std::string_view::npos;
        CHECK((glyph(static_cast<char>(c))[7] != 0) == descends);
    }
    for (char c : std::string_view("AHMZ09")) // the capitals and digits are 7 rows high
        CHECK(glyph(c)[0] != 0 && glyph(c)[6] != 0);
}

// Mirrored about the middle of the columns the glyph uses.
bool horizontallySymmetric(const evo::text::GlyphRows& rows) {
    std::uint8_t used = 0;
    for (std::uint8_t row : rows)
        used |= row;
    const int left = std::countl_zero(used), right = 7 - std::countr_zero(used); // columns, MSB = 0
    for (std::uint8_t row : rows)
        for (int x = left; x <= right; ++x) {
            const bool a = row & (0x80u >> x), b = row & (0x80u >> (left + right - x));
            if (a != b)
                return false;
        }
    return true;
}

void testShapes() {
    CHECK(horizontallySymmetric(glyph('I')));
    CHECK(horizontallySymmetric(glyph('O')));
    CHECK(horizontallySymmetric(glyph('H')));
    CHECK(!horizontallySymmetric(glyph('F')));

    int rows = 0;
    for (std::uint8_t row : glyph('-'))
        rows += row != 0;
    CHECK(rows == 1);

    for (char a = '0'; a <= '9'; ++a)
        for (char b = static_cast<char>(a + 1); b <= '9'; ++b)
            CHECK(glyph(a) != glyph(b));
    CHECK(glyph('O') != glyph('0'));
    CHECK(glyph('l') != glyph('1') && glyph('I') != glyph('l'));

    // Outside the font: the '?' glyph.
    CHECK(glyph('\n') == glyph('?'));
    CHECK(glyph(static_cast<char>(200)) == glyph('?'));
}

void testGlyphCodes() {
    using evo::text::glyphCodes;
    const auto codes = [](std::string_view text) { return glyphCodes(text); };
    const auto bytes = [](std::string_view text) {
        return std::vector<std::uint8_t>(text.begin(), text.end());
    };

    CHECK(codes("C:\\> dir") == bytes("C:\\> dir"));
    // The cue sheet's three non-ASCII characters: the middle dot has its own glyph, 127.
    CHECK(codes("1993 \xC2\xB7 320\xC3\x97"
                "200") ==
          (std::vector<std::uint8_t>{'1', '9', '9', '3', ' ', 127, ' ', '3', '2', '0', 'x', '2', '0', '0'}));
    CHECK(codes("1993 \xE2\x86\x92 2026") == bytes("1993 -> 2026"));
    CHECK(codes("caf\xC3\xA9") == bytes("caf?")); // any other character
    CHECK(codes("a\xFF"
                "b\xC3") == bytes("a?b?")); // malformed UTF-8
    CHECK(codes("two\nlines") == bytes("two\nlines"));
    CHECK(codes("tab\there") == bytes("tab?here"));

    CHECK(evo::text::textWidth("", 1) == 0);
    CHECK(evo::text::textWidth("ABC", 1) == 24);
    CHECK(evo::text::textWidth("1993 \xE2\x86\x92 2026", 2) == 12 * 16);
    CHECK(evo::text::textWidth("ab\nlonger\nc", 1) == 48);
}

// 16 glyphs per row, code 32 in the top-left cell, a set pixel is 255.
void testAtlas() {
    using namespace evo::text;
    const std::vector<std::uint8_t> pixels = atlasPixels();
    CHECK(pixels.size() == std::size_t{kAtlasWidth} * kAtlasHeight);
    CHECK(kAtlasWidth == 128 && kAtlasHeight == 48);
    for (int code = 32; code <= 127; ++code) {
        const GlyphRows& rows = glyph(static_cast<char>(code));
        const std::uint32_t cellX = (code - 32) % 16 * 8, cellY = (code - 32) / 16 * 8;
        for (std::uint32_t y = 0; y < 8; ++y)
            for (std::uint32_t x = 0; x < 8; ++x) {
                const std::uint8_t expected = (rows[y] & (0x80u >> x)) ? 255 : 0;
                if (pixels[(cellY + y) * kAtlasWidth + cellX + x] != expected)
                    throw std::runtime_error("atlas pixel of glyph " + std::to_string(code) + " is wrong");
            }
    }
    ++checksPassed;
}

// The whole font as text, for looking at it.
void print() {
    for (int first = 32; first <= 127; first += 16) {
        for (int y = 0; y < 8; ++y) {
            std::string line;
            for (int c = first; c < first + 16; ++c)
                for (int x = 0; x < 8; ++x)
                    line += (glyph(static_cast<char>(c))[y] & (0x80u >> x)) ? '#' : '.';
            std::printf("%s\n", line.c_str());
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        testEveryGlyphIsDrawn();
        testGapColumn();
        testBaseline();
        testShapes();
        testGlyphCodes();
        testAtlas();
        if (argc > 1 && std::string_view(argv[1]) == "--print")
            print();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("font_test: %d checks passed\n", checksPassed);
    return 0;
}
