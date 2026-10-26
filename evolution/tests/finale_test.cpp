// FINALE, chapter 9: the 1993 side as a 3x3 grid of every effect round the EVOLUTION scroller. On the CPU:
// the cells tile the screen with 1-pixel gutters, each tile is 3:1 of kFinaleSource, the tiles' home frames
// start at their best moments and advance at their own rates (wrapping inside their chapters), and PORTAL
// continues FINALE's count. On the GPU, through the retro stage: every tile equals the box-downsampled (in
// linear light) solo render of its effect without its text, the centre and the gutters hold FINALE's own
// picture, the 1993 card is typed bottom-left on black cells, and a frame is the same after another frame and
// from a fresh stage; the megamix's thank-you card replaces the grid on exactly its three frames. Runs with the
// validation layer on and fails on any message it reports.

#include "gfx/context.hpp"
#include "retro/boot.hpp"
#include "retro/dots.hpp"
#include "retro/effects.hpp"
#include "retro/finale.hpp"
#include "retro/portal.hpp"
#include "retro/raytrace.hpp"
#include "retro/rotozoom.hpp"
#include "retro/tunnel.hpp"
#include "retro_stage_fixture.hpp"
#include "show/cues.hpp"
#include "show/director.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"
#include "text/font8x8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::EffectFrame;
using evo::retro::EffectInfo;
using evo::retro::PixelRect;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::test::RetroStageFixture;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr int kFinaleChapter = 9;
constexpr std::uint32_t kRate = evo::retro::kFinaleRetroRate;

// A frame of `chapter` at `fraction` of the way through retro frame `retroFrame`.
TimelineFrame at(int chapter, std::uint32_t retroFrame, double fraction = 0.5) {
    const double start = evo::show::chapterStart(chapter);
    const double end = start + evo::show::sampleTimeline(start).chapterDuration;
    return evo::show::sampleTimeline(std::min(start + (retroFrame + fraction) / kRate, end - 1e-6));
}
TimelineFrame finale(std::uint32_t retroFrame, double fraction = 0.5) {
    const TimelineFrame frame = at(kFinaleChapter, retroFrame, fraction);
    if (frame.chapter != kFinaleChapter || frame.effect != Effect::Finale)
        throw std::runtime_error("expected a FINALE frame at retro frame " + std::to_string(retroFrame));
    return frame;
}

void testRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::Finale);
    CHECK(info.grid == &evo::retro::finaleGrid && !info.phosphor);
    CHECK(info.shader == nullptr && info.texture == nullptr && info.retroRate == kRate);
    CHECK(info.text == &evo::retro::finaleText && info.scene == &evo::retro::finaleScene);
    CHECK(info.palette == &evo::retro::finalePalette);
    for (Effect tile : evo::retro::kFinaleTiles)
        CHECK(evo::retro::effectFor(tile).grid == nullptr);
    // Every 1993 effect of Act I is a tile, once.
    const std::set<Effect> tiles(evo::retro::kFinaleTiles.begin(), evo::retro::kFinaleTiles.end());
    CHECK(tiles.size() == 8);
    for (Effect e : {Effect::Flyby, Effect::Dots, Effect::Plasma, Effect::Tunnel, Effect::Glenz, Effect::Lens,
                     Effect::Raytrace, Effect::City})
        CHECK(tiles.contains(e));
}

// The cells: 106 x 66 with 1-pixel gutters, filling 320 x 200 exactly; the tiles skip the centre.
void testLayout() {
    CHECK(evo::retro::finaleCell(0) == (PixelRect{0, 0, 106, 66}));
    CHECK(evo::retro::finaleCell(8) == (PixelRect{214, 134, 106, 66}));
    CHECK(evo::retro::finaleCell(4) == (PixelRect{107, 67, 106, 66}));
    std::vector<int> covered(320 * 200, 0);
    for (int cell = 0; cell < 9; ++cell) {
        const PixelRect r = evo::retro::finaleCell(cell);
        for (int y = r.y; y < r.y + r.height; ++y)
            for (int x = r.x; x < r.x + r.width; ++x)
                ++covered[static_cast<std::size_t>(y * 320 + x)];
    }
    int gutter = 0;
    for (int c : covered) {
        CHECK(c <= 1);
        gutter += c == 0;
    }
    CHECK(gutter == 2 * 200 + 2 * 320 - 4); // two columns and two rows
    std::set<int> cells;
    for (std::size_t t = 0; t < 8; ++t)
        cells.insert(evo::retro::finaleTileCell(t));
    CHECK(cells.size() == 8 && !cells.contains(evo::retro::kFinaleCentre));
    CHECK(evo::retro::kFinaleSource.width == 3 * 106 && evo::retro::kFinaleSource.height == 3 * 66);
}

// The tiles' clocks: from their home starts, at their own rates, wrapping inside their chapters; PORTAL's
// first frame is the one after FINALE's last.
void testTileClocks() {
    for (Effect tile : evo::retro::kFinaleTiles) {
        const std::uint32_t rate = evo::retro::effectFor(tile).retroRate;
        const int home = evo::retro::finaleHomeChapter(tile);
        const double duration = evo::show::sampleTimeline(evo::show::chapterStart(home)).chapterDuration;
        const auto last = static_cast<std::uint32_t>(std::floor(duration * rate)) - 1;
        const std::uint32_t start = evo::retro::finaleHomeStart(tile);
        for (std::uint32_t elapsed = 0; elapsed < 700; elapsed += 7) {
            const EffectFrame source = evo::retro::finaleTileSource(tile, elapsed);
            CHECK(source.effect == tile && source.frame.chapter == home && source.frame.effect == tile);
            CHECK(source.retroFrame >= start && source.retroFrame <= last);
            CHECK(source.retroFrame == start + (elapsed * rate / kRate) % (last - start + 1));
            // The frame is the home chapter at that retro frame.
            CHECK(static_cast<std::uint32_t>(std::floor(source.frame.localSeconds * rate)) ==
                  source.retroFrame);
        }
    }
    const TimelineFrame lastFinale = finale(0);
    const auto finaleFrames = static_cast<std::uint32_t>(std::ceil(lastFinale.chapterDuration * kRate));
    CHECK(evo::retro::finaleElapsed(lastFinale, 17) == 17);
    CHECK(evo::retro::finaleElapsed(at(10, 0), 0) == finaleFrames);
    CHECK(evo::retro::finaleElapsed(at(10, 0), 25) == finaleFrames + 25);
}

void testHomeStarts() {
    for (Effect sub : evo::retro::kFinaleTiles) {
        const int home = evo::retro::finaleHomeChapter(sub);
        const double duration = evo::show::sampleTimeline(evo::show::chapterStart(home)).chapterDuration;
        const std::uint32_t rate = evo::retro::effectFor(sub).retroRate;
        // At least 4 s of the home chapter after the start, so a tile loops at most every 4 s.
        CHECK(evo::retro::finaleHomeStart(sub) + 4.0 * rate < duration * rate);
    }
    const auto facts = [](Effect sub) {
        return evo::show::sampleTimeline(evo::show::chapterStart(evo::retro::finaleHomeChapter(sub)));
    };
    const auto framesAt = [](double seconds, std::uint32_t rate) {
        return static_cast<std::uint32_t>(std::floor(seconds * rate));
    };
    // FLYBY at 60 % of its flight, GLENZ on its accent, RAYTRACE halfway, each at its own rate.
    CHECK(
        evo::retro::finaleHomeStart(Effect::Flyby) ==
        framesAt(0.6 * facts(Effect::Flyby).chapterDuration, evo::retro::effectFor(Effect::Flyby).retroRate));
    CHECK(evo::retro::finaleHomeStart(Effect::Glenz) ==
          framesAt(facts(Effect::Glenz).accentSeconds, evo::retro::effectFor(Effect::Glenz).retroRate));
    CHECK(evo::retro::finaleHomeStart(Effect::Raytrace) ==
          framesAt(0.5 * facts(Effect::Raytrace).chapterDuration, evo::retro::kRaytraceRetroRate));
    // LENS: the lens wholly on the screen, high, at the right end of its run.
    const std::uint32_t lensStart = evo::retro::finaleHomeStart(Effect::Lens);
    CHECK(lensStart == framesAt(evo::retro::kFinaleLensStartSeconds, evo::retro::kRotozoomRetroRate));
    const evo::retro::RotozoomView lens =
        evo::retro::rotozoomView(static_cast<double>(lensStart) / evo::retro::kRotozoomRetroRate);
    const int r = evo::retro::kRotozoomLensRadius, rows = 40; // 48 pixels across, 40 rows down
    std::printf("finale: LENS starts at home retro frame %u, the lens at (%d, %d)\n", lensStart, lens.lensX,
                lens.lensY);
    CHECK(lens.lensX - r >= 0 && lens.lensX + r <= 320 && lens.lensY - rows >= 0 && lens.lensY + rows <= 200);
    CHECK(lens.lensX >= 240 && lens.lensY <= 70);
    const TimelineFrame tunnel = evo::retro::finaleHomeFrame(Effect::Tunnel, 0);
    CHECK(evo::retro::tunnelTubeFront(tunnel, evo::retro::finaleHomeStart(Effect::Tunnel)) ==
          evo::retro::kTunnelTubeNear);
    const TimelineFrame dots = evo::retro::finaleHomeFrame(Effect::Dots, 0);
    const evo::retro::DotsMorph year =
        evo::retro::dotsMorph(dots, evo::retro::finaleHomeStart(Effect::Dots) + 15);
    CHECK(year.from == evo::retro::DotsShape::Year && year.to == evo::retro::DotsShape::Year);
}

// A tile's own frame without its text, as the grid shows it.
const EffectInfo& withoutText(Effect effect) {
    static const std::array<EffectInfo, evo::show::kEffectCount> entries = [] {
        std::array<EffectInfo, evo::show::kEffectCount> all{};
        for (std::size_t e = 0; e < evo::show::kEffectCount; ++e) {
            all[e] = evo::retro::effectFor(static_cast<Effect>(e));
            all[e].text = nullptr;
        }
        return all;
    }();
    return entries[static_cast<std::size_t>(effect)];
}

double toLinear(int c) {
    const double v = c / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}
double toByte(double linear) {
    const double c = linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
    return 255 * std::clamp(c, 0.0, 1.0);
}
int channel(const std::vector<std::uint8_t>& rgb, int x, int y, int c) {
    return rgb[(static_cast<std::size_t>(y) * 320 + static_cast<std::size_t>(x)) * 3 +
               static_cast<std::size_t>(c)];
}

// The largest difference between a cell of the grid and the 3x3 box means of a solo picture over
// kFinaleSource.
double tileError(const std::vector<std::uint8_t>& grid, const std::vector<std::uint8_t>& solo,
                 const PixelRect& cell) {
    const PixelRect from = evo::retro::kFinaleSource;
    double worst = 0;
    for (int v = 0; v < cell.height; ++v)
        for (int u = 0; u < cell.width; ++u)
            for (int c = 0; c < 3; ++c) {
                double sum = 0;
                for (int dy = 0; dy < 3; ++dy)
                    for (int dx = 0; dx < 3; ++dx)
                        sum += toLinear(channel(solo, from.x + 3 * u + dx, from.y + 3 * v + dy, c));
                worst =
                    std::max(worst, std::fabs(channel(grid, cell.x + u, cell.y + v, c) - toByte(sum / 9)));
            }
    return worst;
}

void testImages(evo::gfx::Context& ctx) {
    RetroStageFixture grid(ctx, true);
    RetroStageFixture solo(ctx, true, withoutText);
    for (std::uint32_t r : {20u, 333u}) {
        const evo::test::RetroPicture picture = grid.renderPicture(finale(r));
        CHECK(grid.stage().picturesDrawn() == 9); // eight tiles and FINALE's own picture
        for (std::size_t t = 0; t < evo::retro::kFinaleTiles.size(); ++t) {
            const EffectFrame source = evo::retro::finaleTileSource(evo::retro::kFinaleTiles[t], r);
            const evo::test::RetroPicture own = solo.renderPicture(source.frame);
            CHECK(solo.stage().retroFrame() == source.retroFrame);
            const PixelRect cell = evo::retro::finaleCell(evo::retro::finaleTileCell(t));
            const double error = tileError(picture.rgb, own.rgb, cell);
            std::printf("finale: frame %u tile %zu (%s) off by %.2f levels at most\n", r, t,
                        evo::show::effectName(evo::retro::kFinaleTiles[t]), error);
            // The card may cover the bottom-left cell's last rows.
            if (cell.y + cell.height <= evo::retro::kCardY || evo::retro::cardShown(finale(r), r, kRate) < 0)
                CHECK(error <= 1.0);
        }
        // The gutters are black; the centre holds the scroller's warm glyphs on black.
        for (int y = 0; y < 200; ++y)
            CHECK(channel(picture.rgb, 106, y, 0) == 0 && channel(picture.rgb, 213, y, 1) == 0);
        const PixelRect centre = evo::retro::finaleCell(evo::retro::kFinaleCentre);
        int lit = 0, black = 0;
        for (int y = centre.y; y < centre.y + centre.height; ++y)
            for (int x = centre.x; x < centre.x + centre.width; ++x) {
                const int red = channel(picture.rgb, x, y, 0), blue = channel(picture.rgb, x, y, 2);
                lit += red > 200;
                black += red == 0;
                CHECK(blue <= red); // warm or white, never cold
            }
        CHECK(lit > 60 && black > centre.width * centre.height / 2);
    }

    // The card: typed bottom-left in white on black cells while it shows.
    const TimelineFrame any = finale(0);
    std::uint32_t typed = 0;
    while (evo::retro::cardShown(any, typed, kRate) < 10)
        ++typed;
    const evo::test::RetroPicture card = grid.renderPicture(finale(typed));
    const PixelRect rect = evo::retro::cardRect(any, typed, kRate);
    CHECK(rect.x == 4 && rect.y == 188 && rect.width >= 8 * 11);
    int white = 0;
    for (int y = rect.y; y < rect.y + 8; ++y)
        for (int x = rect.x; x < rect.x + 8 * 10; ++x) {
            const int red = channel(card.rgb, x, y, 0);
            CHECK(red == 0 || red == 255);
            white += red == 255;
        }
    CHECK(white > 60);

    // Deterministic: the same retro frame (another moment of it) after another frame, and from a fresh stage.
    for (std::uint32_t r : {57u, 260u}) {
        const std::vector<std::uint8_t> first = grid.renderPicture(finale(r, 0.1)).rgb;
        grid.renderPicture(finale(r + 7));
        CHECK(grid.renderPicture(finale(r, 0.9)).rgb == first);
        RetroStageFixture fresh(ctx, true);
        CHECK(fresh.renderPicture(finale(r, 0.5)).rgb == first);
    }
}

// The thank-you card, through the retro stage frame by frame as an export sees it: the 60 fps frames
// 10420-10422 show exactly the card's lines in white on black (every glyph pixel of the 8x8 font white, every
// other pixel black), centred and inside the screen; 10419 and 10423 show the grid, as a fresh stage draws it.
void testThankYouCard(evo::gfx::Context& ctx) {
    // The card's picture on the CPU.
    const std::vector<evo::text::Item> items = evo::retro::finaleCardText(finale(0), 0);
    CHECK(items.size() == evo::retro::kFinaleCardLines.size());
    std::vector<std::uint8_t> expected(320 * 200, 0);
    int top = 200, bottom = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const evo::text::Item& item = items[i];
        CHECK(item.text == evo::retro::kFinaleCardLines[i].text);
        CHECK(static_cast<int>(item.scale) == evo::retro::kFinaleCardLines[i].scale);
        const int scale = static_cast<int>(item.scale);
        const int width = evo::text::textWidth(item.text, scale);
        CHECK(std::abs(2 * static_cast<int>(item.x) + width - 320) <= 1); // centred
        CHECK(item.x >= 8 && item.x + width <= 312);
        top = std::min(top, static_cast<int>(item.y));
        bottom = std::max(bottom, static_cast<int>(item.y) + 8 * scale);
        for (std::size_t g = 0; g < item.text.size(); ++g) {
            const evo::text::GlyphRows& rows = evo::text::glyph(item.text[g]);
            for (int row = 0; row < 8; ++row)
                for (int col = 0; col < 8; ++col)
                    if ((rows[static_cast<std::size_t>(row)] >> (7 - col)) & 1)
                        for (int dy = 0; dy < scale; ++dy)
                            for (int dx = 0; dx < scale; ++dx) {
                                const int x = static_cast<int>(item.x) + (static_cast<int>(g) * 8 + col) * scale + dx;
                                const int y = static_cast<int>(item.y) + row * scale + dy;
                                expected[static_cast<std::size_t>(y * 320 + x)] = 255;
                            }
        }
    }
    CHECK(std::abs((top + bottom) - 200) <= 1); // the block centred vertically
    CHECK(std::string(items[0].text) + " " + items[1].text == "YOU PAUSED. GOOD EYE.");
    CHECK(std::string(items[2].text) + " " + items[3].text == "WE ONLY HAD ONE FRAME TO SAY THANK YOU.");

    RetroStageFixture stage(ctx, true);
    for (long i = 10418; i <= 10424; ++i) {
        const TimelineFrame frame = evo::show::sampleTimeline(i / 60.0);
        CHECK(frame.effect == Effect::Finale);
        const bool card = i >= 10420 && i <= 10422;
        CHECK(evo::show::thankYouCard(frame) == card);
        const evo::test::RetroPicture picture = stage.renderPicture(frame);
        if (card) {
            for (std::size_t p = 0; p < expected.size(); ++p)
                for (std::size_t c = 0; c < 3; ++c)
                    CHECK(picture.rgb[p * 3 + c] == expected[p]);
        } else {
            RetroStageFixture fresh(ctx, true);
            CHECK(fresh.renderPicture(frame).rgb == picture.rgb);
            std::size_t differ = 0;
            for (std::size_t p = 0; p < expected.size(); ++p)
                differ += picture.rgb[p * 3] != expected[p];
            CHECK(differ > 320 * 200 / 4); // the grid, not the card
        }
    }
    std::printf("finale: the thank-you card on frames 10420-10422 only\n");
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testLayout();
        testTileClocks();
        testHomeStarts();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testImages(ctx);
            testThankYouCard(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("finale_test: %d checks passed\n", checksPassed);
    return 0;
}
