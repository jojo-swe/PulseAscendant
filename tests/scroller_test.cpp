// The sine scroller: its glyph positions on the CPU, CREDITS (chapter 29: the credits on bar lines, THE END?
// on the accent, the bass-driven wave) and FLYBY's greeting (chapter 1) in the registry, and their 320x200
// palette index images (and colours) from the retro stage on the GPU. Runs with the validation layer on and
// fails on any message it reports.

#include "gfx/context.hpp"
#include "render/epoch_cards.hpp"
#include "render/renderer.hpp"
#include "retro/effects.hpp"
#include "retro/flyby.hpp"
#include "retro/palette.hpp"
#include "retro/scroller.hpp"
#include "retro_stage_fixture.hpp"
#include "show/cues.hpp"
#include "show/generated/cue_data.hpp"
#include "show/music_curves.hpp"
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
    const std::string_view credits =
        "   EVOLUTION  ...  A LONG LINE OF TEXT THAT RUNS FOR SEVERAL SCREENS, AS A SCROLLER DOES  ...";
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
    CHECK(credits.shader == nullptr && credits.retroRate == 35 && credits.phosphor);
    CHECK(credits.phosphorSeconds == evo::retro::kPhosphorFastSeconds);
    CHECK(credits.palette == &evo::retro::creditsPalette);
    CHECK(credits.text == &evo::retro::creditsText);
    CHECK(credits.background == &evo::retro::creditsBackground);
    CHECK(credits.scene == &evo::retro::creditsScene);

    const evo::retro::EffectInfo& flyby = evo::retro::effectFor(Effect::Flyby);
    CHECK(flyby.shader == nullptr && flyby.scene == &evo::retro::flybyScene);
    CHECK(flyby.retroRate == 35 && flyby.palette == &evo::retro::flybyPalette);
    CHECK(flyby.text == &evo::retro::flybyText);
    CHECK(evo::retro::kFlybyText == "WE WERE THERE IN 1993 ...");

    // CREDITS holds 1993 for the whole chapter, so its scroller is what the chapter shows.
    const TimelineFrame frame = chapterFrame(kCreditsChapter, Effect::Credits, 4);
    CHECK(evo::show::cues[kCreditsChapter].eraFixed == 0 && frame.era == 0.f);
    // FLYBY's: the greeting low over the ship and the stars, white (255) in FLYBY's palette.
    CHECK(same(evo::retro::flybyText(frame, 140),
               scrollerItems(evo::retro::kFlybyText, 140, 60, 6, 30, 171, 255, true)));

    // Pure: the same retro frame gives the same items whatever else of the frame changes within it.
    TimelineFrame noisy = chapterFrame(kCreditsChapter, Effect::Credits, middleOf(140) + .01);
    noisy.kick = 1;
    noisy.energy = 1;
    noisy.bass = 1;
    noisy.beatPhase = .7f;
    noisy.showSeconds += 100;
    noisy.localSeconds += 9;
    for (std::uint32_t retroFrame : {0u, 1u, 140u, 900u}) {
        CHECK(same(evo::retro::creditsText(noisy, retroFrame), evo::retro::creditsText(frame, retroFrame)));
        CHECK(same(evo::retro::flybyText(noisy, retroFrame), evo::retro::flybyText(frame, retroFrame)));
        evo::retro::Palette a{}, b{};
        evo::retro::creditsPalette(a, frame, retroFrame);
        evo::retro::creditsPalette(b, noisy, retroFrame);
        CHECK(a == b);
    }
}

// The glyphs of every credit on screen at a retro frame, by credit: their x (unrounded first glyph) for the
// test's own bookkeeping.
std::string joined(const std::vector<Item>& items) {
    std::string text;
    for (const Item& item : items)
        text += item.text;
    return text;
}

// CREDITS: exactly the three credit lines and THE END?, each entering on a bar line, THE END? stopping
// centred on the accent and holding; the amplitude follows the bass; the paper starts white.
void testCredits() {
    CHECK(evo::retro::kCreditsTitle == "EVOLUTION");
    CHECK(evo::retro::kCreditsLine == "CODE: JOJO · MUSIC: JOYOLO · CONCEPT: JOHN");
    CHECK(evo::retro::kCreditsGreetings.starts_with("GREETINGS TO  FUTURE CREW · "));
    CHECK(evo::retro::kCreditsEnd == "THE END?");
    const TimelineFrame frame = chapterFrame(kCreditsChapter, Effect::Credits, 1);
    const double start = evo::show::chapterStart(kCreditsChapter);
    const int track = evo::show::trackAt(start);
    const evo::show::BeatGridView grid = evo::show::beatGrid(track);
    // Each entry is a tracked bar line.
    for (int c = 0; c < 4; ++c) {
        const double entry = evo::retro::creditEntry(frame, c);
        const double trackTime = start + entry - evo::show::trackStart(track);
        const int beat = grid.beatAt(trackTime + 1e-6);
        CHECK(std::fabs(grid.beatTime(beat) - trackTime) < 1e-6 && grid.bar(beat) == 0);
        std::printf("credits: credit %d enters at %.3f s\n", c, start + entry);
        if (c > 0)
            CHECK(entry > evo::retro::creditEntry(frame, c - 1));
    }
    // Every glyph shown belongs to a credit, in order; nothing else is written. The credits never overlap:
    // each one's last glyph has entered before the next enters.
    const std::string_view texts[3] = {evo::retro::kCreditsTitle, evo::retro::kCreditsLine,
                                       evo::retro::kCreditsGreetings};
    for (int c = 0; c + 1 < 4; ++c) {
        const double width = 8.0 * static_cast<double>(evo::retro::creditGlyphs(texts[c]).size());
        CHECK(evo::retro::creditEntry(frame, c) + width / evo::retro::kCreditsSpeed <
              evo::retro::creditEntry(frame, c + 1));
    }
    // A glyph's entry: the frame where the first glyph of a credit is first on screen is its bar's frame.
    for (int c = 0; c < 3; ++c) {
        const auto entryFrame =
            static_cast<std::uint32_t>(std::ceil(evo::retro::creditEntry(frame, c) * 35 - 1e-9));
        std::string before, after;
        for (const Item& item : evo::retro::creditsText(frame, entryFrame + 2))
            after += item.text;
        CHECK(after.find(std::string(evo::retro::creditGlyphs(texts[c])[0])) != std::string::npos);
    }
    // THE END? lands centred on the accent (the beat the form of Act III's track names, three beats after
    // the bar line it enters on) and holds there, flat, for at least a bar.
    const double accentBeat = evo::show::generated::chapterAccents[kCreditsChapter];
    CHECK(std::fabs(start + frame.accentSeconds - accentBeat) < 1e-6);
    CHECK(frame.chapterDuration - frame.accentSeconds > frame.beatSeconds * 4);
    const auto accent = static_cast<std::uint32_t>(std::floor(frame.accentSeconds * 35));
    for (std::uint32_t f = accent + 1; f < static_cast<std::uint32_t>(frame.chapterDuration * 35); f += 13) {
        const std::vector<Item> items = evo::retro::creditsText(frame, f);
        CHECK(joined(items) == "THE END?" || joined(items).ends_with("THEEND?"));
        const Item& last = items.back();
        CHECK(last.text == "?" && last.x == static_cast<float>(evo::retro::kCreditsEndX + 7 * 8));
        CHECK(last.y == evo::retro::kCreditsBaseY);
    }
    const double before = evo::retro::creditsEndX(frame, frame.accentSeconds - 1.0 / 35);
    CHECK(before > evo::retro::kCreditsEndX && before - evo::retro::kCreditsEndX < 2); // easing in, slowly
    CHECK(evo::retro::creditsEndX(frame, frame.accentSeconds) == evo::retro::kCreditsEndX);
    // While it travels it never moves backwards, nor catches up with the greetings' last glyph ahead of it.
    double x = evo::retro::creditsEndX(frame, evo::retro::creditEntry(frame, 3));
    CHECK(std::fabs(x - 320) < 1e-9);
    const double greetingsWidth =
        8.0 * static_cast<double>(evo::retro::creditGlyphs(evo::retro::kCreditsGreetings).size());
    for (double t = evo::retro::creditEntry(frame, 3); t < frame.accentSeconds; t += 0.01) {
        const double next = evo::retro::creditsEndX(frame, t + 0.01);
        CHECK(next <= x + 1e-9);
        const double tail = 320 - evo::retro::kCreditsSpeed * (t + 0.01 - evo::retro::creditEntry(frame, 2)) +
                            greetingsWidth - 8;
        CHECK(next >= tail + 16);
        x = next;
    }
    // The amplitude is the bass at the retro frame, within its range.
    double low = 1e9, high = 0;
    for (std::uint32_t f = 0; f < 900; f += 3) {
        const double a = evo::retro::creditsAmplitude(frame, f);
        CHECK(a >= evo::retro::kCreditsAmplitude &&
              a <= evo::retro::kCreditsAmplitude + evo::retro::kCreditsBassAmp);
        low = std::min(low, a);
        high = std::max(high, a);
    }
    std::printf("credits: amplitude %.1f to %.1f pixels\n", low, high);
    CHECK(high - low > 6);
    // From white: the paper is white on the first frame, black by the first beat.
    evo::retro::Palette palette{};
    evo::retro::creditsPalette(palette, frame, 0);
    CHECK(palette[evo::retro::kCreditsPaper] == evo::retro::rgba(255, 255, 255));
    evo::retro::creditsPalette(palette, frame, 20);
    CHECK(palette[evo::retro::kCreditsPaper] == evo::retro::rgba(0, 0, 0));
    CHECK(palette[evo::retro::kCreditsWhite] == evo::retro::rgba(255, 255, 255));
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

    // CREDITS at t = 4: the credits line in white over the stars on black, every glyph where its item says.
    const std::uint32_t retroFrame = frameAt(4);
    const TimelineFrame credits = chapterFrame(kCreditsChapter, Effect::Credits, middleOf(retroFrame));
    bool rendered = false;
    const Picture picture = fixture.renderPicture(credits, &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == retroFrame);
    const std::vector<Item> items = evo::retro::creditsText(credits, retroFrame);
    std::size_t checked = 0;
    for (const Item& item : items)
        if (wholeInside(item) && item.text.size() == 1) {
            CHECK(glyphAt(picture, item, 15, -1));
            ++checked;
        }
    std::printf("credits: %zu glyphs checked at t = 4\n", checked);
    CHECK(checked >= 20);
    std::size_t stars = 0;
    for (std::uint8_t index : picture.indices) {
        CHECK(index == 1 || index == 15 || (index >= 2 && index <= 4)); // the paper, text, stars
        stars += index >= 2 && index <= 4;
    }
    CHECK(stars > 40);

    // The same retro frame at another time inside it, after another frame: the identical images.
    fixture.render(chapterFrame(kCreditsChapter, Effect::Credits, middleOf(retroFrame + 30)), &rendered);
    CHECK(rendered);
    TimelineFrame again = chapterFrame(kCreditsChapter, Effect::Credits, (retroFrame + .95) / 35);
    again.kick = 1;
    const Picture repeat = fixture.renderPicture(again, &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == retroFrame);
    CHECK(repeat.indices == picture.indices && repeat.rgb == picture.rgb);

    // Once the greetings have left (15 frames after the accent): THE END? alone, centred, flat, white.
    const auto landed = static_cast<std::uint32_t>(std::floor(credits.accentSeconds * 35)) + 15;
    const Picture end =
        fixture.renderPicture(chapterFrame(kCreditsChapter, Effect::Credits, middleOf(landed)));
    for (const Item& item : evo::retro::creditsText(credits, landed))
        if (wholeInside(item))
            CHECK(glyphAt(end, item, 15, -1));
    std::size_t ink = 0;
    for (int y = 96; y < 104; ++y)
        for (int x = static_cast<int>(evo::retro::kCreditsEndX);
             x < static_cast<int>(evo::retro::kCreditsEndX) + 64; ++x)
            ink += end.at(x, y) == 15;
    CHECK(ink > 60 &&
          ink == static_cast<std::size_t>(std::count(end.indices.begin(), end.indices.end(), 15)));

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
// included, ends above the card's top row at every 16:9 output size (the card is laid out in the CRT's 4:3
// rectangle, render/epoch_cards.hpp, so retro row r starts at row r * rectHeight / 200 of it). The card is
// drawn bottom-left at its own size, so a greeting at the very bottom of the picture would run through it.
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
        const evo::render::ThenCardLayout card =
            evo::render::thenCardLayout(evo::render::thenCardText(Effect::Flyby), width, height);
        CHECK(card.rectHeight > 0 && !card.codes.empty());
        const auto cardTop = static_cast<double>(card.originY);
        const double greetingBottom = static_cast<double>(bottom) * card.rectHeight / kHeight;
        std::printf("flyby: greeting ends at picture row %.1f, the card starts at %.0f (%ux%u)\n",
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
        testCredits();
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
