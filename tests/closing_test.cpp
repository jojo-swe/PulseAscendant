// The 1993 side's closing screens: PORTAL (chapter 10: FINALE's grid switching off cell by cell to the prompt
// `THE SCREEN_`), RETURN (chapter 19: the CRT dark until the accent, the switch-on flash, the POST screen and
// `EVOLUTION.EXE /2026` typed over the last four bars), REBOOT (chapter 20: RETURN's screen and the Enter,
// the reboot script and DOTS' cube from the track's beat 14) and THE END (chapter 30: the last line, then the
// tube switching off on the accent to a line, a dot, its afterglow and black); Act III follows the form of
// its track, so its times come from the show's own data. Their registry entries, scripts, timings and
// scenes on the CPU, and their pictures from the retro stage on the GPU. Runs with the validation layer on
// and fails on any message it reports.

#include "gfx/context.hpp"
#include "retro/boot.hpp"
#include "retro/dos_idle.hpp"
#include "retro/dots.hpp"
#include "retro/effects.hpp"
#include "retro/finale.hpp"
#include "retro/palette.hpp"
#include "retro/portal.hpp"
#include "retro/scene.hpp"
#include "retro/the_end.hpp"
#include "retro_stage_fixture.hpp"
#include "show/cues.hpp"
#include "show/era.hpp"
#include "show/generated/cue_data.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"
#include "text/font8x8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::EffectInfo;
using evo::retro::Palette;
using evo::retro::RetroScene;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::test::RetroPicture;
using evo::test::RetroStageFixture;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr int kWidth = static_cast<int>(RetroStageFixture::kWidth);
constexpr int kHeight = static_cast<int>(RetroStageFixture::kHeight);
constexpr int kPortalChapter = 10, kReturnChapter = 19, kRebootChapter = 20, kTheEndChapter = 30;
constexpr std::uint32_t kRate = 35;

// A frame of `chapter` (showing `effect`) at `local` seconds into it.
TimelineFrame at(int chapter, Effect effect, double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(chapter) + local);
    if (frame.chapter != chapter || frame.effect != effect)
        throw std::runtime_error("expected chapter " + std::to_string(chapter) + " at local time " +
                                 std::to_string(local));
    return frame;
}
TimelineFrame portal(double local) {
    return at(kPortalChapter, Effect::Portal, local);
}
TimelineFrame returning(double local) {
    return at(kReturnChapter, Effect::Return, local);
}
TimelineFrame reboot(double local) {
    return at(kRebootChapter, Effect::Reboot, local);
}
TimelineFrame theEnd(double local) {
    return at(kTheEndChapter, Effect::TheEnd, local);
}

double middleOf(std::uint32_t retroFrame) {
    return (retroFrame + .5) / kRate;
}

// Retro frames that begin inside the chapter.
std::uint32_t frameCount(const TimelineFrame& frame) {
    return static_cast<std::uint32_t>(std::ceil(frame.chapterDuration * kRate));
}

// The middle of a retro frame, or just before the chapter's end for the last, partial one.
double timeIn(const TimelineFrame& any, std::uint32_t retroFrame, double within = .5) {
    return std::min((retroFrame + within) / kRate, any.chapterDuration - 1e-6);
}

std::array<int, 3> rgbOf(std::uint32_t entry) {
    return {static_cast<int>(entry & 0xff), static_cast<int>(entry >> 8 & 0xff),
            static_cast<int>(entry >> 16 & 0xff)};
}

int brightness(std::uint32_t entry) {
    const auto c = rgbOf(entry);
    return c[0] + c[1] + c[2];
}

std::size_t count(const std::vector<std::uint8_t>& image, std::uint8_t index) {
    return static_cast<std::size_t>(std::count(image.begin(), image.end(), index));
}

// Whether the image shows `text` in the 8x8 font at (x, y), scale `scale`: `ink` where the glyph is set,
// `paper` elsewhere in its cells (paper < 0: anything but ink).
bool showsText(const std::vector<std::uint8_t>& image, const std::string& text, int x, int y, int scale,
               std::uint8_t ink, int paper) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const evo::text::GlyphRows& rows = evo::text::glyph(text[i]);
        for (int row = 0; row < 8 * scale; ++row)
            for (int col = 0; col < 8 * scale; ++col) {
                const bool set = rows[row / scale] & (0x80u >> (col / scale));
                const int px = x + static_cast<int>(i) * 8 * scale + col, py = y + row;
                const std::uint8_t got = image[static_cast<std::size_t>(py) * kWidth + px];
                if (set ? got != ink : (paper >= 0 ? got != paper : got == ink))
                    return false;
            }
    }
    return true;
}

// The show time of a retro frame's start in a chapter.
double showTime(const TimelineFrame& any, std::uint32_t retroFrame) {
    return evo::show::chapterStart(any.chapter) + static_cast<double>(retroFrame) / kRate;
}
// Whether a show time falls in the retro frame (within one frame of it).
bool within(const TimelineFrame& any, std::uint32_t retroFrame, double show) {
    return std::fabs(showTime(any, retroFrame) + 0.5 / kRate - show) <= 1.0 / kRate;
}
// The first retro frame whose screen satisfies `test`.
template <typename Screen, typename Test>
std::uint32_t firstFrame(const TimelineFrame& any, Screen screen, Test test) {
    for (std::uint32_t f = 0; f < frameCount(any); ++f)
        if (test(screen(any, f)))
            return f;
    throw std::runtime_error("the screen never shows what the test waits for");
}
std::string row(const std::vector<std::string>& screen, std::size_t r) {
    return screen.size() > r ? screen[r] : std::string();
}

// --- RETURN ----------------------------------------------------------------------------------------------

void testReturnRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::Return);
    CHECK(info.shader == nullptr && info.phosphor && info.retroRate == kRate);
    CHECK(info.text == &evo::retro::returnText && info.background == &evo::retro::returnBackground);
    CHECK(info.palette == &evo::retro::returnPalette);
    // STUDIO NIGHT keeps the DOS listing.
    CHECK(evo::retro::effectFor(Effect::StudioNight).text == &evo::retro::dosIdleText);
}

// Dark until the accent, the flash on it fading over a beat, then the POST screen and the command typed on
// the beat, its last key on the chapter's last beat (412.38 s, within a retro frame of the join at 412.40).
void testReturnScript() {
    const TimelineFrame any = returning(1);
    const std::uint32_t flash = evo::retro::returnFlashFrame(any);
    CHECK(within(any, flash, 401.69));
    Palette palette{};
    for (std::uint32_t f = 0; f < flash; ++f) {
        CHECK(evo::retro::returnScreen(any, f).empty() && evo::retro::returnText(any, f).empty());
        CHECK(evo::retro::returnBackground(any, f) == 0);
    }
    int last = 256;
    for (std::uint32_t f = flash; f < flash + 30; ++f) {
        evo::retro::returnPalette(palette, any, f);
        const int level = rgbOf(palette[evo::retro::kReturnFlash])[0];
        CHECK(level <= last);
        last = level;
        if (f == flash)
            CHECK(level == 255);
    }
    CHECK(last == 0); // gone within the beat after the accent
    const auto screen = [](const TimelineFrame& frame, std::uint32_t f) {
        return evo::retro::returnScreen(frame, f);
    };
    const std::uint32_t post =
        firstFrame(any, screen, [](const std::vector<std::string>& s) { return !s.empty(); });
    CHECK(post > flash && (post - flash) < 0.6 * kRate);
    CHECK(row(screen(any, post), 0) == evo::retro::kBootBiosLine);
    const std::uint32_t lastKey = evo::retro::returnLastKeyFrame(any);
    CHECK(within(any, lastKey, 412.40));
    CHECK(lastKey == frameCount(any) - 1); // the chapter's last retro frame
    CHECK(row(screen(any, lastKey), 2) == "C:\\DEMO>EVOLUTION.EXE /2026");
    CHECK(row(screen(any, lastKey - 1), 2) == "C:\\DEMO>EVOLUTION.EXE /202");
    // The first key 16 beats before the last, the keys of EVOLUTION.EXE on the beats.
    const std::uint32_t first =
        firstFrame(any, screen, [](const std::vector<std::string>& s) { return row(s, 2) == "C:\\DEMO>E"; });
    const double beats = (showTime(any, lastKey) - showTime(any, first)) / 0.4245;
    std::printf("return: flash %.3f s, first key %.3f s, last key %.3f s (%.1f beats)\n",
                showTime(any, flash), showTime(any, first), showTime(any, lastKey), beats);
    CHECK(std::fabs(beats - 16) < 0.6);
    // The four-bar typing is the whole of the last four bars: the prompt waits from before them.
    CHECK(row(screen(any, first - 1), 2) == "C:\\DEMO>");
}

void testReturnImages(RetroStageFixture& fixture) {
    const TimelineFrame any = returning(1);
    const std::uint32_t flash = evo::retro::returnFlashFrame(any);
    RetroPicture dark = fixture.renderPicture(returning(timeIn(any, flash - 1)));
    CHECK(std::all_of(dark.rgb.begin(), dark.rgb.end(), [](std::uint8_t c) { return c == 0; }));
    const RetroPicture lit = fixture.renderPicture(returning(timeIn(any, flash)));
    CHECK(count(lit.indices, evo::retro::kReturnFlash) == static_cast<std::size_t>(kWidth * kHeight));
    CHECK(std::all_of(lit.rgb.begin(), lit.rgb.end(), [](std::uint8_t c) { return c == 255; }));
    const std::uint32_t lastKey = evo::retro::returnLastKeyFrame(any);
    const RetroPicture typed = fixture.renderPicture(returning(timeIn(any, lastKey)));
    CHECK(showsText(typed.indices, "C:\\DEMO>EVOLUTION.EXE /2026", 4, 20, 1, 7, -1));
    CHECK(showsText(typed.indices, evo::retro::kBootBiosLine, 4, 4, 1, 7, -1));
}

// --- REBOOT ----------------------------------------------------------------------------------------------

void testRebootRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::Reboot);
    CHECK(info.shader == nullptr && info.phosphor && info.retroRate == kRate);
    CHECK(info.text == &evo::retro::rebootText && info.scene == &evo::retro::rebootScene);
    CHECK(info.palette == &evo::retro::rebootPalette);
}

// REBOOT opens on RETURN's last screen with the Enter, then the script on its beats (the first on the track's
// first tracked beat), the cube from beat 14.
void testRebootScript() {
    const TimelineFrame any = reboot(1);
    const auto screen = [](const TimelineFrame& frame, std::uint32_t f) {
        return evo::retro::rebootScreen(frame, f);
    };
    const TimelineFrame returnAny = returning(1);
    CHECK(screen(any, 0) == evo::retro::returnScreen(returnAny, evo::retro::returnLastKeyFrame(returnAny)));
    const auto beatShow = [&any](int b) {
        return evo::show::chapterStart(any.chapter) + evo::retro::chapterBeat(any, b);
    };
    const auto starts = [&](std::size_t r, const char* text) {
        return firstFrame(any, screen,
                          [&](const std::vector<std::string>& s) { return row(s, r).starts_with(text); });
    };
    CHECK(within(any, starts(3, "Reinitialising reality"), beatShow(0)));
    CHECK(within(any, starts(4, "Ray tracing hardware"), beatShow(4)));
    CHECK(within(any, starts(5, "found"), beatShow(6)));
    CHECK(within(any, starts(6, "LOADING"), beatShow(8)));
    const double firstBeat = evo::show::chapterStart(kRebootChapter) + evo::show::beatGrid(2).beatTime(0);
    CHECK(std::fabs(beatShow(0) - firstBeat) < 1e-6);
    const std::uint32_t cube = evo::retro::rebootCubeFrame(any);
    CHECK(within(any, cube, beatShow(14)));
    CHECK(!screen(any, cube - 1).empty() && screen(any, cube).empty());
    CHECK(row(screen(any, cube - 1), 4) + " " + row(screen(any, cube - 1), 5) ==
          "Ray tracing hardware .................. found");
    RetroScene scene;
    evo::retro::rebootScene(scene, any, cube - 1);
    CHECK(scene.points.empty());
    scene.clear();
    evo::retro::rebootScene(scene, any, cube + 10);
    CHECK(scene.points.size() >= 400);
    // The card: "1993 · REBOOT", in at one bar after the start.
    CHECK(std::string(evo::show::cues[kRebootChapter].thenCard) == "1993 · REBOOT");
    std::uint32_t in = 0;
    while (evo::retro::cardShown(any, in, kRate) < 0)
        ++in;
    CHECK(std::fabs(static_cast<double>(in) / kRate - evo::show::transitionMarks(any).bar) <= 1.0 / kRate);
}

void testRebootImages(RetroStageFixture& fixture) {
    const TimelineFrame any = reboot(1);
    const RetroPicture prompt = fixture.renderPicture(reboot(2.0));
    CHECK(showsText(prompt.indices, "C:\\DEMO>EVOLUTION.EXE /2026", 4, 20, 1, 7, -1));
    CHECK(showsText(prompt.indices, "Reinitialising reality", 4, 28, 1, 7, -1));
    const std::uint32_t cube = evo::retro::rebootCubeFrame(any);
    const RetroPicture dots = fixture.renderPicture(reboot(timeIn(any, cube + 20)));
    std::size_t ramp = 0;
    for (std::uint8_t i : dots.indices)
        ramp += i >= evo::retro::kDotsRamp && i < evo::retro::kDotsRamp + evo::retro::kDotsShades;
    CHECK(ramp > 300);
    CHECK(count(dots.indices, 7) == 0);                        // the prompt is gone
    CHECK(showsText(dots.indices, "1993", 4, 188, 1, 15, -1)); // the card
}

// --- THE END ---------------------------------------------------------------------------------------------

void testTheEndRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::TheEnd);
    CHECK(info.shader == nullptr && info.phosphor && info.retroRate == kRate);
    CHECK(info.text == &evo::retro::theEndText && info.scene == &evo::retro::theEndScene);
    CHECK(info.palette == &evo::retro::theEndPalette);
}

// The caption types in centred, then the power-off: the off frame on the accent (the beat the form of Act
// III's track names, at least two bars and a second before the end), a line of at most 2 rows by two frames
// later, the dot, its afterglow fading through two bars, then black to the end.
void testTheEndPowerOff() {
    const TimelineFrame any = theEnd(1);
    const evo::show::TransitionMarks marks = evo::show::transitionMarks(any);
    CHECK(std::fabs(marks.accent - evo::show::generated::chapterAccents[kTheEndChapter]) < 1e-6);
    const std::uint32_t off = evo::retro::theEndOffFrame(any);
    CHECK(within(any, off, marks.accent));
    const std::vector<evo::text::Item> before = evo::retro::theEndText(any, off - 1);
    CHECK(!before.empty() && before[0].text == "REALITY IS WHAT YOU RENDER");
    CHECK(before[0].x == evo::retro::kTheEndX && before[0].y == evo::retro::kTheEndY);
    CHECK(evo::retro::kTheEndX * 2 + 8 * 27 == 320); // centred with its cursor
    CHECK(evo::retro::theEndTyped(any, 0).empty());
    CHECK(evo::retro::theEndText(any, off).empty());
    const auto light = [&any](std::uint32_t f) { return evo::retro::theEndLight(any, f); };
    CHECK(light(off - 1).width == 0);
    CHECK(light(off).height <= 6 && light(off).width == 208);
    CHECK(light(off + 2).height <= 2 && light(off + 2).width == 208); // the line by +2 frames
    CHECK(light(off + 3).width < 208 && light(off + 4).width < light(off + 3).width);
    const std::uint32_t dot = evo::retro::theEndDotFrame(any), black = evo::retro::theEndBlackFrame(any);
    CHECK(light(dot).width == 2 && light(dot).height == 2);
    const double bars = static_cast<double>(black - dot) / kRate / marks.bar;
    std::printf("the end: off %.3f s, dot %.3f s, black %.3f s (%.2f bars of afterglow)\n",
                showTime(any, off), showTime(any, dot), showTime(any, black), bars);
    CHECK(std::fabs(bars - 2) < 0.05);
    int level = 256;
    for (std::uint32_t f = dot; f < black; ++f) {
        const int now = evo::retro::theEndLevel(any, f);
        CHECK(now <= level && now > 0);
        level = now;
    }
    for (std::uint32_t f = black; f < frameCount(any); ++f) {
        CHECK(evo::retro::theEndLevel(any, f) == 0 && light(f).width == 0);
        CHECK(evo::retro::theEndText(any, f).empty());
    }
    CHECK(showTime(any, black) < evo::show::showDuration() - 1);
}

void testTheEndImages(RetroStageFixture& fixture) {
    const TimelineFrame any = theEnd(1);
    const std::uint32_t off = evo::retro::theEndOffFrame(any);
    const RetroPicture text = fixture.renderPicture(theEnd(timeIn(any, off - 1)));
    CHECK(showsText(text.indices, "REALITY IS WHAT YOU RENDER", evo::retro::kTheEndX, evo::retro::kTheEndY, 1,
                    evo::retro::kTheEndText, 0));
    const RetroPicture line = fixture.renderPicture(theEnd(timeIn(any, off + 2)));
    CHECK(count(line.indices, evo::retro::kTheEndLight) == 208 * 2);
    CHECK(count(line.indices, 0) == static_cast<std::size_t>(kWidth * kHeight - 208 * 2));
    // The last frame of the show: black, glow and all.
    const RetroPicture last = fixture.renderPicture(theEnd(any.chapterDuration - 1e-4));
    CHECK(std::all_of(last.rgb.begin(), last.rgb.end(), [](std::uint8_t c) { return c == 0; }));
}

// --- PORTAL ----------------------------------------------------------------------------------------------

void testPortalRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::Portal);
    CHECK(info.grid == &evo::retro::portalGrid && !info.phosphor && info.retroRate == kRate);
    CHECK(info.palette == &evo::retro::finalePalette && info.text == &evo::retro::portalText);
    CHECK(info.scene == &evo::retro::portalScene);
}

// The cells switch off on beats 2..10 round the edge and the centre last, the prompt types on beat 11, all
// before 187.85 s; until a cell's beat the grid is FINALE's, running on.
void testPortalTimeline() {
    const TimelineFrame any = portal(1);
    std::uint32_t previous = 0;
    for (std::size_t i = 0; i < evo::retro::kPortalOrder.size(); ++i) {
        const int cell = evo::retro::kPortalOrder[i];
        const std::uint32_t off = evo::retro::portalOffFrame(any, cell);
        CHECK(off > previous);
        previous = off;
        CHECK(within(any, off,
                     evo::show::chapterStart(kPortalChapter) +
                         evo::retro::chapterBeat(any, evo::retro::kPortalFirstOff + static_cast<int>(i))));
    }
    CHECK(evo::retro::kPortalOrder.back() == evo::retro::kFinaleCentre);
    const std::uint32_t prompt = evo::retro::portalPromptFrame(any);
    CHECK(prompt > previous + evo::retro::kPortalCollapseFrames);
    std::uint32_t done = prompt;
    while (evo::retro::portalPrompt(any, done) != "THE SCREEN")
        ++done;
    std::printf("portal: cells off from %.3f s to %.3f s, prompt %.3f s, typed by %.3f s\n",
                showTime(any, evo::retro::portalOffFrame(any, 0)), showTime(any, previous),
                showTime(any, prompt), showTime(any, done));
    CHECK(showTime(any, done) < 187.85);
    // Before the first beat, the grid layers are FINALE's tiles at the running count.
    const std::vector<evo::retro::GridLayer> layers = evo::retro::portalGrid(any, 3);
    for (std::size_t t = 0; t < evo::retro::kFinaleTiles.size(); ++t) {
        const evo::retro::EffectFrame want =
            evo::retro::finaleTileSource(evo::retro::kFinaleTiles[t], evo::retro::finaleElapsed(any, 3));
        CHECK(layers[t].source.effect == want.effect && layers[t].source.retroFrame == want.retroFrame);
        CHECK(layers[t].to == evo::retro::finaleCell(evo::retro::finaleTileCell(t)));
    }
    // From the centre's end on, a single layer: PORTAL's own screen.
    const std::vector<evo::retro::GridLayer> last = evo::retro::portalGrid(any, prompt);
    CHECK(last.size() == 1 && last[0].source.effect == Effect::Portal);
}

// The collapse: a third, 4 rows, a 2-row line, then one row narrowing to a dot; the light kept (gain = area
// ratio, at most 64), centred in the cell.
void testPortalCollapse() {
    const evo::retro::PixelRect cell = evo::retro::finaleCell(0);
    const int heights[6] = {22, 4, 2, 1, 1, 1};
    for (std::uint32_t n = 0; n < evo::retro::kPortalCollapseFrames; ++n) {
        const evo::retro::Collapse c = evo::retro::portalCollapse(cell, n);
        CHECK(c.rect.height == heights[n]);
        CHECK(c.rect.width > 0 && c.rect.width <= cell.width);
        CHECK(std::abs((c.rect.x * 2 + c.rect.width) - (cell.x * 2 + cell.width)) <= 1);
        CHECK(std::abs((c.rect.y * 2 + c.rect.height) - (cell.y * 2 + cell.height)) <= 1);
        const double area = static_cast<double>(cell.width) * cell.height / (c.rect.width * c.rect.height);
        CHECK(std::fabs(c.gain - std::min(area, 64.0)) < 1e-3);
    }
    CHECK(evo::retro::portalCollapse(cell, 5).rect.width == 2);
    CHECK(evo::retro::portalCollapse(cell, 6).rect.width == 0);
}

void testPortalImages(RetroStageFixture& fixture) {
    const TimelineFrame any = portal(1);
    // A cell two frames after its beat: lit only on its 2-row line.
    const std::uint32_t off = evo::retro::portalOffFrame(any, 0);
    const RetroPicture line = fixture.renderPicture(portal(timeIn(any, off + 2)));
    const evo::retro::PixelRect cell = evo::retro::finaleCell(0);
    for (int y = cell.y; y < cell.y + cell.height; ++y) {
        int lit = 0;
        for (int x = cell.x; x < cell.x + cell.width; ++x)
            lit += line.rgb[(static_cast<std::size_t>(y) * kWidth + x) * 3] > 0;
        CHECK((y == 32 || y == 33) ? lit > 0 : lit == 0);
    }
    // 187.85 s: black but for the prompt in the middle and the card.
    const RetroPicture prompt =
        fixture.renderPicture(portal(187.85 - evo::show::chapterStart(kPortalChapter)));
    CHECK(showsText(prompt.indices, "THE SCREEN", evo::retro::kPortalPromptX, evo::retro::kPortalPromptY, 1,
                    evo::retro::kFinaleWhite, 0));
    std::size_t lit = 0;
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const bool inText = y >= evo::retro::kPortalPromptY && y < evo::retro::kPortalPromptY + 8;
            const bool inCard = y >= evo::retro::kCardY && y < evo::retro::kCardY + 8;
            if (!inText && !inCard)
                lit += prompt.rgb[(static_cast<std::size_t>(y) * kWidth + x) * 3] > 0;
        }
    CHECK(lit == 0);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testReturnRegistry();
        testReturnScript();
        testRebootRegistry();
        testRebootScript();
        testTheEndRegistry();
        testTheEndPowerOff();
        testPortalRegistry();
        testPortalTimeline();
        testPortalCollapse();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            RetroStageFixture fixture(ctx, true);
            testReturnImages(fixture);
            testRebootImages(fixture);
            testTheEndImages(fixture);
            testPortalImages(fixture);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("closing_test: %d checks passed\n", checksPassed);
    return 0;
}
