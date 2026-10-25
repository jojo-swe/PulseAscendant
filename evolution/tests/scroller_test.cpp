// The sine scroller: its glyph positions on the CPU, CREDITS (chapter 29) and FLYBY's greeting (chapter 1)
// in the registry, and their 320x200 palette index images (and colours) from the retro stage on the GPU. Runs
// with the validation layer on and fails on any message it reports.

#include "gfx/context.hpp"
#include "render/renderer.hpp"
#include "retro/effects.hpp"
#include "retro/flyby.hpp"
#include "retro/palette.hpp"
#include "retro/scroller.hpp"
#include "retro_stage_fixture.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"
#include "text/font8x8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using evo::render::RetroStage;
using evo::retro::scrollerItems;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::test::RetroStageFixture;
using evo::text::Item;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr std::uint32_t kWidth = RetroStage::kWidth, kHeight = RetroStage::kHeight;
constexpr int kFlybyChapter = 1;
constexpr int kCreditsChapter = 29;

// The retro frame of t seconds at 35 Hz.
constexpr std::uint32_t frameAt(double seconds) {
    return static_cast<std::uint32_t>(seconds * evo::retro::kScrollerRetroRate);
}

// A frame of `chapter` at `local` seconds into it.
TimelineFrame chapterFrame(int chapter, Effect effect, double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(chapter) + local);
    if (frame.chapter != chapter || frame.effect != effect)
        throw std::runtime_error("expected chapter " + std::to_string(chapter) + " at local time " +
                                 std::to_string(local));
    return frame;
}

// The middle of retro frame `retroFrame`.
double middleOf(std::uint32_t retroFrame) {
    return (retroFrame + .5) / evo::retro::kScrollerRetroRate;
}

bool same(const std::vector<Item>& a, const std::vector<Item>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].scale != b[i].scale || a[i].rgba != b[i].rgba ||
            a[i].text != b[i].text)
            return false;
    return true;
}

// The positions: where the first glyph is when, the wave, the culling and the loop.
void testPositions() {
    constexpr float speed = 60, amplitude = 24, wavelength = 40, baseY = 150;
    const auto items = [&](std::string_view text, std::uint32_t retroFrame) {
        return scrollerItems(text, retroFrame, speed, amplitude, wavelength, baseY);
    };

    // t = 0: the first glyph stands at x = 320, just off the right edge, so nothing is drawn yet.
    CHECK(items("ABC", 0).empty());
    // One retro frame later it has moved 60 / 35 pixels: x = round(318.29) = 318, one glyph partly in.
    auto at = items("ABC", 1);
    CHECK(at.size() == 1 && at[0].x == 318 && at[0].text == "A");

    // t = 4: the first glyph at 320 - 240 = 80, the others 8 pixels apart, each its own one-character item
    // in index 15 at scale 1, on the wave y = 150 + 24 sin((x + phase) / 40) with phase = 120.
    at = items("ABC", frameAt(4));
    CHECK(at.size() == 3);
    CHECK(std::abs(at[0].x - 80) <= 1 && at[0].x == 80);
    for (std::size_t i = 0; i < at.size(); ++i) {
        CHECK(at[i].text == std::string(1, "ABC"[i]));
        CHECK(at[i].x == 80 + 8 * static_cast<float>(i));
        CHECK(at[i].y == static_cast<float>(std::round(150 + 24 * std::sin((at[i].x + 120) / 40.0))));
        CHECK(at[i].scale == 1 && at[i].rgba == 0xFF00000Fu);
    }
    std::printf("scroller: at t = 4 the glyphs stand at (%g, %g), (%g, %g), (%g, %g)\n", at[0].x, at[0].y,
                at[1].x, at[1].y, at[2].x, at[2].y);

    // Over a long run of a long text: whole pixels, inside the image, on the band of the wave, 8 apart.
    const std::string_view credits = evo::retro::kCreditsText;
    float lowest = baseY, highest = baseY;
    for (std::uint32_t retroFrame = 0; retroFrame < 2000; ++retroFrame) {
        const std::vector<Item> glyphs = items(credits, retroFrame);
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            const Item& glyph = glyphs[i];
            CHECK(glyph.x == std::round(glyph.x) && glyph.y == std::round(glyph.y));
            CHECK(glyph.x > -8 && glyph.x < 320 && glyph.y > -8 && glyph.y < 200);
            CHECK(glyph.y >= baseY - amplitude && glyph.y <= baseY + amplitude);
            CHECK(glyph.text.size() == 1 && glyph.text != " ");
            if (i > 0)
                CHECK(std::fmod(glyph.x - glyphs[i - 1].x, 8.f) == 0 && glyph.x > glyphs[i - 1].x);
            lowest = std::min(lowest, glyph.y);
            highest = std::max(highest, glyph.y);
        }
    }
    CHECK(lowest == baseY - amplitude && highest == baseY + amplitude); // the wave uses its whole band

    // The loop: "ABC" is 24 pixels wide, so the scroll repeats every 320 + 24 = 344 pixels. Retro frame 200
    // (342.9 pixels on) shows the last glyph about to leave; frame 201 (344.6, 0.6 into the next pass) the
    // first glyph back at the right edge; 602 frames (1032 pixels, three passes) later all is as before.
    at = items("ABC", 200);
    CHECK(at.size() == 1 && at[0].text == "C" && at[0].x == -7);
    at = items("ABC", 201);
    CHECK(at.size() == 1 && at[0].text == "A" && at[0].x == 319);
    for (std::uint32_t retroFrame : {0u, 1u, 140u, 199u, 200u, 201u, 333u})
        CHECK(same(items("ABC", retroFrame + 602), items("ABC", retroFrame)));
    // A text wider than the screen fills it, every glyph 8 pixels from the last.
    at = scrollerItems("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJ", frameAt(6), speed, 0, wavelength,
                       baseY);
    CHECK(at.size() == 40); // 360 pixels on: glyph 5 at x = 0, glyph 44 at x = 312
    for (std::size_t i = 0; i < at.size(); ++i) {
        CHECK(at[i].x == 8 * static_cast<float>(i));
        CHECK(at[i].text == std::string(1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJ"[5 + i]));
        CHECK(at[i].y == baseY); // no amplitude, no wave
    }

    // Glyphs wholly above or below the image are left out; blanks are never items.
    const std::vector<Item> tall = scrollerItems("ABCDEFGH", frameAt(4), speed, 300, wavelength, 100);
    CHECK(tall.size() < 8);
    for (const Item& glyph : tall)
        CHECK(glyph.y > -8 && glyph.y < 200);
    CHECK(scrollerItems("A B", frameAt(4), speed, amplitude, wavelength, baseY).size() == 2);
    CHECK(scrollerItems("", frameAt(4), speed, amplitude, wavelength, baseY).empty());
    // Another index when asked.
    CHECK(scrollerItems("A", frameAt(4), speed, amplitude, wavelength, baseY, 255)[0].rgba == 0xFF0000FFu);

    // The shadow: every glyph again in index 0 one pixel right and down, all of them before the glyphs,
    // which are the items without a shadow.
    for (std::uint32_t retroFrame : {frameAt(1), frameAt(4), frameAt(6)}) {
        const std::vector<Item> plain =
            scrollerItems("SHADOW TEST", retroFrame, speed, amplitude, wavelength, baseY, 255);
        const std::vector<Item> shadowed =
            scrollerItems("SHADOW TEST", retroFrame, speed, amplitude, wavelength, baseY, 255, true);
        CHECK(!plain.empty() && shadowed.size() == 2 * plain.size());
        CHECK(same({shadowed.begin() + static_cast<std::ptrdiff_t>(plain.size()), shadowed.end()}, plain));
        for (std::size_t i = 0; i < plain.size(); ++i)
            CHECK(shadowed[i].x == plain[i].x + 1 && shadowed[i].y == plain[i].y + 1 &&
                  shadowed[i].rgba == 0xFF000000u && shadowed[i].text == plain[i].text &&
                  shadowed[i].scale == 1);
    }
}

// The registry entries and CREDITS' palette, and that the providers depend only on the retro frame.
void testRegistry() {
    const evo::retro::EffectInfo& credits = evo::retro::effectFor(Effect::Credits);
    CHECK(credits.shader == nullptr && credits.retroRate == 35);
    CHECK(credits.palette == &evo::retro::creditsPalette);
    CHECK(credits.text == &evo::retro::creditsText);
    CHECK(credits.background == &evo::retro::creditsBackground);

    const evo::retro::EffectInfo& flyby = evo::retro::effectFor(Effect::Flyby);
    CHECK(flyby.shader == nullptr && flyby.scene == &evo::retro::flybyScene);
    CHECK(flyby.retroRate == 35 && flyby.palette == &evo::retro::flybyPalette);
    CHECK(flyby.text == &evo::retro::flybyText);

    // The credits, verbatim.
    CHECK(evo::retro::kCreditsText ==
          "   EVOLUTION  ...  CODE: JOJO  ...  MUSIC: JOYOLO  ...  CONCEPT: JOHN  ...  GREETINGS TO "
          "FUTURE CREW AND EVERYONE WHO WAS THERE IN 1993  ...  REALITY IS WHAT YOU RENDER  ...");
    CHECK(evo::retro::kFlybyText == "WE WERE THERE IN 1993 ...");

    // CREDITS holds 1993 for the whole chapter, so its scroller is what the chapter shows.
    const TimelineFrame frame = chapterFrame(kCreditsChapter, Effect::Credits, 4);
    CHECK(evo::show::cues[kCreditsChapter].eraFixed == 0 && frame.era == 0.f);

    evo::retro::Palette palette{};
    palette.fill(0x12345678u);
    credits.palette(palette, frame, 140);
    CHECK(palette[0] == evo::retro::rgba(0, 0, 0));
    CHECK(palette[1] == evo::retro::rgba(0x00, 0x00, 0x40));
    CHECK(palette[15] == evo::retro::rgba(0xFF, 0xFF, 0xFF));
    for (std::uint32_t entry : palette)
        CHECK((entry >> 24) == 0xff);
    CHECK(credits.background(frame, 140) == 1);

    // CREDITS' text: the EVOLUTION title centred at y = 24, then the scroller at baseY 100.
    const std::vector<Item> text = evo::retro::creditsText(frame, 140);
    CHECK(!text.empty() && text[0].text == "EVOLUTION" && text[0].x == 124 && text[0].y == 24);
    CHECK(text[0].rgba == 0xFF00000Fu && text[0].scale == 1);
    const std::vector<Item> scroller = scrollerItems(evo::retro::kCreditsText, 140, 60, 24, 40, 100);
    CHECK(same({text.begin() + 1, text.end()}, scroller));
    // FLYBY's: the greeting low over the ship and the stars, white (255) in FLYBY's palette.
    CHECK(same(evo::retro::flybyText(frame, 140),
               scrollerItems(evo::retro::kFlybyText, 140, 60, 6, 30, 172, 255, true)));

    // Pure: the same retro frame gives the same items whatever else of the frame changes within it.
    TimelineFrame noisy = chapterFrame(kCreditsChapter, Effect::Credits, middleOf(140) + .01);
    noisy.kick = 1;
    noisy.energy = 1;
    noisy.beatPhase = .7f;
    noisy.showSeconds += 100;
    noisy.localSeconds += 9;
    for (std::uint32_t retroFrame : {0u, 1u, 140u, 900u}) {
        CHECK(same(evo::retro::creditsText(noisy, retroFrame), evo::retro::creditsText(frame, retroFrame)));
        CHECK(same(evo::retro::flybyText(noisy, retroFrame), evo::retro::flybyText(frame, retroFrame)));
    }
}

using Picture = evo::test::RetroPicture;

// Whether the 8x8 cell of a one-glyph item holds its glyph: `ink` where the font is set, and where it is not
// `paper` (any index when paper < 0).
bool glyphAt(const Picture& picture, const Item& item, std::uint8_t ink, int paper) {
    const int x = static_cast<int>(item.x), y = static_cast<int>(item.y);
    const evo::text::GlyphRows& rows = evo::text::glyph(item.text[0]);
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col) {
            const std::uint8_t index = picture.at(x + col, y + row);
            if (rows[row] & (0x80u >> col)) {
                if (index != ink)
                    return false;
            } else if (paper >= 0 && index != paper) {
                return false;
            }
        }
    return true;
}

bool wholeInside(const Item& item, int width = 8) {
    return item.x >= 0 && item.y >= 0 && item.x + static_cast<float>(width) <= kWidth &&
           item.y + 8 <= kHeight;
}

void testImages(evo::gfx::Context& ctx) {
    RetroStageFixture fixture(ctx, true);

    // CREDITS at t = 4: deep blue with the title and the scroller in white, every glyph where its item says.
    const std::uint32_t retroFrame = frameAt(4);
    const TimelineFrame credits = chapterFrame(kCreditsChapter, Effect::Credits, middleOf(retroFrame));
    bool rendered = false;
    const Picture picture = fixture.renderPicture(credits, &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == retroFrame);
    std::size_t blue = 0, white = 0;
    for (std::uint8_t index : picture.indices) {
        CHECK(index == 1 || index == 15);
        (index == 1 ? blue : white) += 1;
    }
    std::printf("credits: %zu white and %zu deep blue pixels at t = 4\n", white, blue);

    const std::vector<Item> items = evo::retro::creditsText(credits, retroFrame);
    // The title.
    for (std::size_t i = 0; i < items[0].text.size(); ++i) {
        Item letter = items[0];
        letter.x += 8 * static_cast<float>(i);
        letter.text = items[0].text.substr(i, 1);
        CHECK(glyphAt(picture, letter, 15, 1));
    }
    // The scroller's first letter, 'E' (glyph 3 after three blanks) at x = 80 + 24, and every other glyph
    // wholly inside the image.
    const Item& first = items[1];
    CHECK(first.text == "E" && first.x == 104);
    CHECK(first.y == static_cast<float>(std::round(100 + 24 * std::sin((104 + 120) / 40.0))));
    CHECK(glyphAt(picture, first, 15, 1));
    std::size_t checked = 0;
    for (std::size_t i = 1; i < items.size(); ++i)
        if (wholeInside(items[i])) {
            CHECK(glyphAt(picture, items[i], 15, 1));
            ++checked;
        }
    std::printf("credits: %zu scroller glyphs checked, the first at (%g, %g)\n", checked, first.x, first.y);
    CHECK(checked >= 20);
    // The palette-mapped colours: #000040 behind, white in front.
    for (std::size_t i = 0; i < picture.indices.size(); ++i) {
        const bool front = picture.indices[i] == 15;
        CHECK(picture.rgb[i * 3] == (front ? 0xFF : 0) && picture.rgb[i * 3 + 1] == (front ? 0xFF : 0) &&
              picture.rgb[i * 3 + 2] == (front ? 0xFF : 0x40));
    }

    // The same retro frame at another time inside it, after another frame: the identical images.
    fixture.render(chapterFrame(kCreditsChapter, Effect::Credits, middleOf(retroFrame + 30)), &rendered);
    CHECK(rendered);
    TimelineFrame again = chapterFrame(kCreditsChapter, Effect::Credits, (retroFrame + .95) / 35);
    again.kick = 1;
    const Picture repeat = fixture.renderPicture(again, &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == retroFrame);
    CHECK(repeat.indices == picture.indices && repeat.rgb == picture.rgb);

    // FLYBY at t = 3, before its portal: the ship and the stars with the greeting in white over them.
    const std::uint32_t flybyFrame = frameAt(3);
    const TimelineFrame flyby = chapterFrame(kFlybyChapter, Effect::Flyby, middleOf(flybyFrame));
    CHECK(flyby.era == 0.f);
    const Picture overlay = fixture.renderPicture(flyby, &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == flybyFrame);
    const std::vector<Item> greeting = evo::retro::flybyText(flyby, flybyFrame);
    CHECK(!greeting.empty() && greeting.size() % 2 == 0);
    const std::size_t glyphs = greeting.size() / 2; // the shadows, then the glyphs
    checked = 0;
    for (std::size_t i = glyphs; i < greeting.size(); ++i)
        if (wholeInside(greeting[i])) {
            CHECK(greeting[i].y >= evo::retro::kFlybyBaseY - evo::retro::kFlybyAmplitude &&
                  greeting[i].y <= evo::retro::kFlybyBaseY + evo::retro::kFlybyAmplitude);
            CHECK(glyphAt(overlay, greeting[i], 255, -1));
            ++checked;
        }
    // Each shadow pixel is black (index 0) unless a glyph's ink covers it.
    std::size_t shadowPixels = 0;
    for (std::size_t i = 0; i < glyphs; ++i) {
        const Item& shadow = greeting[i];
        if (!wholeInside(shadow))
            continue;
        const evo::text::GlyphRows& rows = evo::text::glyph(shadow.text[0]);
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col)
                if (rows[row] & (0x80u >> col)) {
                    const std::uint8_t index =
                        overlay.at(static_cast<int>(shadow.x) + col, static_cast<int>(shadow.y) + row);
                    CHECK(index == 0 || index == 255);
                    shadowPixels += index == 0;
                }
    }
    std::printf("flyby: %zu greeting glyphs checked at t = 3, %zu shadow pixels\n", checked, shadowPixels);
    CHECK(checked >= 10 && shadowPixels >= 100);

    // In FLYBY only the greeting writes 255: every 255 pixel lies on the ink of one of its glyphs (partly
    // visible glyphs included), so no stray white leaks out of the text pass or the scene.
    const auto onInk = [&](int x, int y) {
        for (std::size_t i = glyphs; i < greeting.size(); ++i) {
            // Floored like the text pass places a glyph (a glyph partly off the left edge has x < 0).
            const int col = x - static_cast<int>(std::floor(greeting[i].x));
            const int row = y - static_cast<int>(std::floor(greeting[i].y));
            if (col >= 0 && col < 8 && row >= 0 && row < 8 &&
                (evo::text::glyph(greeting[i].text[0])[row] & (0x80u >> col)))
                return true;
        }
        return false;
    };
    std::size_t greetingWhite = 0;
    for (int y = 0; y < static_cast<int>(kHeight); ++y)
        for (int x = 0; x < static_cast<int>(kWidth); ++x)
            if (overlay.at(x, y) == 255) {
                CHECK(onInk(x, y));
                ++greetingWhite;
            }
    std::printf("flyby: %zu white pixels, all on the greeting's ink\n", greetingWhite);
    CHECK(greetingWhite >= 100);
}

// FLYBY's greeting stays clear of the 1993 epoch card: over the whole chapter its lowest row, drop shadow
// included, ends above the card's top row at every 16:9 output size (the 320x200 picture fills the output's
// height, so retro row r starts at output row r * height / 200). The card is drawn bottom-left at its own
// size, so a greeting at the very bottom of the frame ran through it.
void testGreetingClearsCard() {
    const TimelineFrame flyby = chapterFrame(kFlybyChapter, Effect::Flyby, 3);
    CHECK(flyby.era == 0.f);
    int bottom = 0; // one past the greeting's lowest retro row
    const auto frames =
        static_cast<std::uint32_t>(std::ceil(flyby.chapterDuration * evo::retro::kScrollerRetroRate));
    for (std::uint32_t retroFrame = 0; retroFrame < frames; ++retroFrame)
        for (const Item& item : evo::retro::flybyText(flyby, retroFrame))
            bottom = std::max(bottom, static_cast<int>(std::floor(item.y)) + 8);
    for (const auto& [width, height] :
         {std::pair{640u, 360u}, std::pair{1280u, 720u}, std::pair{1920u, 1080u}, std::pair{3840u, 2160u}}) {
        const std::vector<Item> hud = evo::render::hudItems(flyby, width, height, false);
        CHECK(!hud.empty());
        float cardTop = static_cast<float>(height);
        for (const Item& item : hud)
            cardTop = std::min(cardTop, item.y);
        const double greetingBottom = static_cast<double>(bottom) * height / kHeight;
        std::printf("flyby: greeting ends at output row %.1f, the card starts at %.0f (%ux%u)\n",
                    greetingBottom, cardTop, width, height);
        CHECK(greetingBottom <= cardTop);
    }
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testPositions();
        testRegistry();
        testGreetingClearsCard();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testImages(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("scroller_test: %d checks passed\n", checksPassed);
    return 0;
}
