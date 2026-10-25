// The renderer: the 1993 stage through its palette and the CRT pillarbox, the 2026 placeholder and the era
// compositor. Runs with the validation layer on and fails on any message it reports.

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "image/png.hpp"
#include "render/renderer.hpp"
#include "retro/boot.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "show/timeline.hpp"
#include "text/font8x8.hpp"

#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using evo::render::HistoryReset;
using evo::render::Renderer;
using evo::render::RenderMode;
using evo::render::RenderSettings;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::show::Transition;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

// A read-back frame: top-down rows of RGB.
struct Picture {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgb;

    const std::uint8_t* at(std::uint32_t x, std::uint32_t y) const {
        return &rgb[(std::size_t{y} * width + x) * 3];
    }
    bool black(std::uint32_t x, std::uint32_t y) const {
        const std::uint8_t* p = at(x, y);
        return p[0] == 0 && p[1] == 0 && p[2] == 0;
    }
    bool sameColumn(const Picture& other, std::uint32_t x) const {
        for (std::uint32_t y = 0; y < height; ++y)
            for (int c = 0; c < 3; ++c)
                if (at(x, y)[c] != other.at(x, y)[c])
                    return false;
        return true;
    }
    double mean() const {
        double sum = 0;
        for (std::uint8_t byte : rgb)
            sum += byte;
        return sum / static_cast<double>(rgb.size());
    }
};

// The bounding box of every non-black pixel.
struct Bounds {
    std::uint32_t left = ~0u, top = ~0u, right = 0, bottom = 0;
    bool operator==(const Bounds&) const = default;
};

Bounds contentBounds(const Picture& picture) {
    Bounds bounds;
    for (std::uint32_t y = 0; y < picture.height; ++y)
        for (std::uint32_t x = 0; x < picture.width; ++x)
            if (!picture.black(x, y)) {
                bounds.left = std::min(bounds.left, x);
                bounds.right = std::max(bounds.right, x);
                bounds.top = std::min(bounds.top, y);
                bounds.bottom = std::max(bounds.bottom, y);
            }
    return bounds;
}

// The 4:3 content rectangle the pillarbox must produce (Global Constraints): the output height times 4/3
// wide, or the width times 3/4 high when the output is narrower than 4:3, centred.
Bounds expectedBounds(std::uint32_t width, std::uint32_t height) {
    std::uint32_t contentWidth = width, contentHeight = height;
    if (width * 3 >= height * 4)
        contentWidth = height * 4 / 3;
    else
        contentHeight = width * 3 / 4;
    const std::uint32_t left = (width - contentWidth) / 2, top = (height - contentHeight) / 2;
    return {left, top, left + contentWidth - 1, top + contentHeight - 1};
}

// A renderer with its own ring and readback; every render() submits and waits, then reads back. The HUD is
// off unless asked for, so the geometry checks see only the picture.
class Harness {
public:
    Harness(evo::gfx::Context& ctx, std::uint32_t width, std::uint32_t height, bool crt = true,
            bool hud = false, bool hudTitle = false)
        : Harness(ctx, RenderSettings{width, height, crt, hud, hudTitle}) {}
    Harness(evo::gfx::Context& ctx, const RenderSettings& settings)
        : ctx_(ctx), readback_(std::in_place, ctx, settings.width, settings.height), ring_(ctx),
          renderer_(ctx, ring_, settings) {}

    Picture render(const TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        CHECK(renderer_.output().layout() == VK_IMAGE_LAYOUT_GENERAL);
        readback_->record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return {readback_->width(), readback_->height(), readback_->rgb()};
    }

    void resize(std::uint32_t width, std::uint32_t height) {
        renderer_.resize(width, height);
        readback_.emplace(ctx_, width, height); // every submit was waited for: the old buffer is idle
    }

    Renderer& renderer() { return renderer_; }

private:
    evo::gfx::Context& ctx_;
    std::optional<evo::gfx::Readback> readback_;
    evo::gfx::FrameRing ring_;
    Renderer renderer_; // last: its destructor waits for the ring before its images and pipelines go
};

void save(const Picture& picture, const char* name) {
    const auto path = std::string(EVO_TEST_OUT_DIR) + "/renderer/" + name;
    evo::image::writePng(path, static_cast<int>(picture.width), static_cast<int>(picture.height),
                         picture.rgb.data());
    std::printf("wrote %s\n", path.c_str());
}

// A frame of an effect that never gets a 1993 side (GALAXY, an Act III planetary chapter, 2026 only), so its
// 1993 side is the test pattern; held in 1993, with nothing of 2026 in it.
TimelineFrame retroFrame(int chapter, double localSeconds) {
    CHECK(evo::retro::effectFor(Effect::Galaxy).shader == evo::retro::kTestPatternShader);
    TimelineFrame frame;
    frame.chapter = chapter;
    frame.effect = Effect::Galaxy;
    frame.transition = Transition::Cut;
    frame.localSeconds = localSeconds;
    frame.showSeconds = 30.0 + localSeconds;
    frame.kick = .3f;
    frame.beatPhase = .2f;
    return frame;
}

// A full-frame picture for the geometry checks: the test pattern in chapter 0 (BOOT itself is mostly black).
TimelineFrame pattern(double localSeconds) {
    return retroFrame(0, localSeconds);
}

// The CPU side: every effect has a shader, text, a scene or a resolver (a megamix), a rate and a palette
// whose index 0 is black.
void testEffectRegistry() {
    std::array<std::uint32_t, 256> palette{};
    evo::retro::grayPalette(palette, TimelineFrame{}, 0);
    CHECK(palette[0] == 0xff000000u);
    CHECK(palette[128] == 0xff808080u);
    CHECK(palette[255] == 0xffffffffu);

    for (int i = 0; i < evo::show::chapterCount(); ++i) {
        const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(i) + .5);
        const evo::retro::EffectInfo& info = evo::retro::effectFor(frame.effect);
        CHECK((info.shader != nullptr || info.text != nullptr || info.scene != nullptr ||
               info.resolve != nullptr) &&
              info.retroRate > 0 && info.palette != nullptr);
        palette.fill(0x12345678u);
        info.palette(palette, frame, 7);
        CHECK(palette[0] == 0xff000000u);
    }
}

// (a) The test pattern at t=1, 640x360: a 480x360 4:3 picture with 80-column black bars left and right.
void testPillarbox(evo::gfx::Context& ctx) {
    Harness harness(ctx, 640, 360);
    const Picture picture = harness.render(pattern(1.0));
    save(picture, "pattern-640x360.png");

    for (std::uint32_t y = 0; y < picture.height; ++y) {
        for (std::uint32_t x = 0; x < 80; ++x) {
            CHECK(picture.black(x, y));
            CHECK(picture.black(639 - x, y));
        }
        CHECK(!picture.black(80, y)); // the first content column, on every row
        CHECK(!picture.black(559, y));
    }
    for (std::uint32_t x = 80; x < 560; ++x) {
        CHECK(!picture.black(x, 0)); // the picture fills the whole height
        CHECK(!picture.black(x, 359));
    }
    CHECK(contentBounds(picture) == expectedBounds(640, 360));
    CHECK(harness.renderer().lastStats().retroRendered);
    CHECK(harness.renderer().lastStats().retroFrame == 35);
}

// (b) a tall 400x600 output letterboxes the 4:3 picture to 400x300, centred: rows 0..149
// and 450..599 black. Reached through resize(), as a window dragged tall would.
void testTallOutput(evo::gfx::Context& ctx) {
    Harness harness(ctx, 640, 360);
    harness.render(pattern(1.0));
    harness.resize(400, 600);
    const Picture picture = harness.render(pattern(1.0));
    save(picture, "pattern-400x600.png");

    for (std::uint32_t x = 0; x < picture.width; ++x) {
        for (std::uint32_t y = 0; y < 150; ++y) {
            CHECK(picture.black(x, y));
            CHECK(picture.black(x, 599 - y));
        }
        CHECK(!picture.black(x, 150));
        CHECK(!picture.black(x, 449));
    }
    for (std::uint32_t y = 150; y < 450; ++y) {
        CHECK(!picture.black(0, y));
        CHECK(!picture.black(399, y));
    }
    CHECK(contentBounds(picture) == expectedBounds(400, 600));
}

// (c) the CRT changes the look, never the content rectangle, at any output shape. Odd sizes
// also exercise the partial 8x8 workgroups at the right and bottom edges.
void testCrtKeepsGeometry(evo::gfx::Context& ctx) {
    const std::array<std::array<std::uint32_t, 2>, 3> sizes = {{{640, 360}, {400, 600}, {333, 187}}};
    for (const auto& [width, height] : sizes) {
        Harness crt(ctx, width, height, true);
        Harness plain(ctx, width, height, false);
        const Picture withCrt = crt.render(pattern(1.0));
        const Picture withoutCrt = plain.render(pattern(1.0));
        std::printf("%ux%u: content %u..%u x %u..%u\n", width, height, contentBounds(withCrt).left,
                    contentBounds(withCrt).right, contentBounds(withCrt).top, contentBounds(withCrt).bottom);
        CHECK(contentBounds(withCrt) == expectedBounds(width, height));
        CHECK(contentBounds(withoutCrt) == expectedBounds(width, height));
        CHECK(withCrt.rgb != withoutCrt.rgb); // the look does change
        if (width == 640)
            save(withoutCrt, "pattern-640x360-plain.png");
    }

    // Toggled on a live renderer, as key C does.
    Harness harness(ctx, 640, 360, true);
    const Picture on = harness.render(pattern(1.0));
    harness.renderer().setCrt(false);
    const Picture off = harness.render(pattern(1.0));
    CHECK(!harness.renderer().lastStats().retroRendered); // same retro frame: the CRT pass alone reran
    CHECK(on.rgb != off.rgb);
    CHECK(contentBounds(on) == contentBounds(off));
}

// (d) The retro stage is locked to 35 Hz: t=1.000 and t=1.010 are both retro frame 35 and give identical
// bytes (the second from the cache); t=1.030 is frame 36 and differs. CRT off, so the bytes are the retro
// image's nearest-neighbour scale.
void testRetroFrameLock(evo::gfx::Context& ctx) {
    Harness harness(ctx, 320, 240, false);
    const Picture a = harness.render(pattern(1.000));
    CHECK(harness.renderer().lastStats().retroRendered && harness.renderer().lastStats().retroFrame == 35);
    const Picture b = harness.render(pattern(1.010));
    CHECK(!harness.renderer().lastStats().retroRendered && harness.renderer().lastStats().retroFrame == 35);
    CHECK(a.rgb == b.rgb);
    const Picture c = harness.render(pattern(1.030));
    CHECK(harness.renderer().lastStats().retroRendered && harness.renderer().lastStats().retroFrame == 36);
    CHECK(a.rgb != c.rgb);

    // A fresh renderer gives the same frame however it got there.
    Harness fresh(ctx, 320, 240, false);
    CHECK(fresh.render(pattern(1.010)).rgb == a.rgb);
    CHECK(harness.render(pattern(1.000)).rgb == a.rgb);
}

// (e) a chapter change at an equal retro frame must not reuse the cached image.
void testChapterChangeInvalidates(evo::gfx::Context& ctx) {
    Harness harness(ctx, 320, 240, false);
    const Picture first = harness.render(retroFrame(2, 1.0));
    const Picture second = harness.render(retroFrame(5, 1.0));
    CHECK(harness.renderer().lastStats().retroRendered);
    CHECK(harness.renderer().lastStats().retroFrame == 35);
    CHECK(first.rgb != second.rgb);
}

// (f) A Wipe half way: 2026 left of the edge, 1993 right of it, a 2-pixel white seam between.
void testWipe(evo::gfx::Context& ctx) {
    constexpr std::uint32_t kWidth = 160, kHeight = 90;
    Harness harness(ctx, kWidth, kHeight);

    TimelineFrame modern = retroFrame(3, 2.0);
    modern.era = 1.0f;
    TimelineFrame retro = retroFrame(3, 2.0);
    TimelineFrame wipe = retroFrame(3, 2.0);
    wipe.transition = Transition::Wipe;
    wipe.wipe = .5f;
    wipe.era = .5f;

    const Picture now = harness.render(modern);
    const Picture then = harness.render(retro);
    const Picture split = harness.render(wipe);
    save(split, "wipe-160x90.png");
    CHECK(now.rgb != then.rgb);

    // The pixel centres within one pixel of the edge (x = 80) are columns 79 and 80.
    for (std::uint32_t x = 0; x < 79; ++x)
        CHECK(split.sameColumn(now, x));
    for (std::uint32_t x = 81; x < kWidth; ++x)
        CHECK(split.sameColumn(then, x));
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (std::uint32_t x = 79; x <= 80; ++x)
            CHECK(split.at(x, y)[0] == 255 && split.at(x, y)[1] == 255 && split.at(x, y)[2] == 255);
}

// (g) eraCut flashes white: +0.6 on every pixel, the black bars included.
void testEraCutFlash(evo::gfx::Context& ctx) {
    Harness harness(ctx, 160, 90);
    TimelineFrame frame = retroFrame(2, 1.0);
    const Picture plain = harness.render(frame);
    frame.eraCut = 1.0f;
    const Picture flash = harness.render(frame);
    std::printf("eraCut: mean %.1f -> %.1f, bar pixel %d\n", plain.mean(), flash.mean(), flash.at(0, 0)[0]);
    CHECK(flash.mean() > plain.mean() + 100.0);
    CHECK(plain.black(0, 0));
    CHECK(flash.at(0, 0)[0] >= 152 && flash.at(0, 0)[0] <= 154); // 0.6 * 255 = 153
}

// The output sizes the HUD is checked at, with their HUD units.
struct HudSize {
    std::uint32_t width, height;
    int unit;
};
constexpr std::array<HudSize, 4> kHudSizes = {
    {{640, 360, 1}, {1280, 720, 2}, {2560, 1440, 4}, {3840, 2160, 6}}};

// Every pixel of every item, shadows included, at least 8 units inside a width x height output.
bool insideHudMargins(const std::vector<evo::text::Item>& items, std::uint32_t width, std::uint32_t height) {
    const float margin = 8.0f * static_cast<float>(evo::render::hudUnit(height));
    for (const evo::text::Item& item : items) {
        const int scale = static_cast<int>(item.scale);
        if (item.x < margin || item.y < margin ||
            item.x + static_cast<float>(evo::text::textWidth(item.text, scale)) >
                static_cast<float>(width) - margin ||
            item.y + 8.0f * static_cast<float>(scale) > static_cast<float>(height) - margin)
            return false;
    }
    return true;
}

// Each text item follows its shadow: the same text and scale in black, one unit right and down.
bool shadowed(const std::vector<evo::text::Item>& items, int unit) {
    if (items.size() % 2 != 0)
        return false;
    const auto u = static_cast<float>(unit);
    for (std::size_t i = 0; i < items.size(); i += 2)
        if (items[i].rgba != 0xFF000000u || items[i].text != items[i + 1].text ||
            items[i].scale != items[i + 1].scale || items[i].x != items[i + 1].x + u ||
            items[i].y != items[i + 1].y + u)
            return false;
    return true;
}

// The HUD unit: round(height / 360), at least 1.
void testHudUnit() {
    using evo::render::hudUnit;
    for (const HudSize& size : kHudSizes)
        CHECK(hudUnit(size.height) == size.unit);
    CHECK(hudUnit(1) == 1 && hudUnit(179) == 1 && hudUnit(200) == 1 && hudUnit(539) == 1);
    CHECK(hudUnit(540) == 2 && hudUnit(1080) == 3 && hudUnit(1600) == 4);
}

// The HUD's text items on the CPU: the window's chapter title alone (the epoch cards are drawn by
// EpochCards, tests/epoch_cards_test.cpp), after its shadow, inside the margins, at every HUD unit.
void testHudItems() {
    using evo::render::hudItems;

    const TimelineFrame boot = evo::show::sampleTimeline(2.5);
    for (const auto& [width, height, u] : kHudSizes) {
        const auto items = hudItems(boot, width, height, true);
        CHECK(items.size() == 2 && shadowed(items, u) && insideHudMargins(items, width, height));
        CHECK(items[1].text == "BOOT" && items[1].x == 8 * u && items[1].y == 8 * u && items[1].scale == u);
        CHECK(items[1].rgba == 0xFFFFFFFFu);
        CHECK(hudItems(boot, width, height, false).empty()); // the exports: no title; the cards are no items
    }
    TimelineFrame untitled = boot;
    untitled.title = "";
    CHECK(hudItems(untitled, 640, 360, true).empty());
    // Every chapter of the show has a title, and only the title is an item.
    for (int i = 0; i < evo::show::chapterCount(); ++i) {
        const TimelineFrame sample = evo::show::sampleTimeline(evo::show::chapterStart(i) + .5);
        for (const auto& [width, height, u] : kHudSizes) {
            const auto withTitle = hudItems(sample, width, height, true);
            CHECK(withTitle.size() == 2 && withTitle[1].text == sample.title);
            CHECK(insideHudMargins(withTitle, width, height) && shadowed(withTitle, u));
        }
    }
}

// The HUD drawn: only the title and the 1993 card's area (bottom-left in the picture) change, at least 8
// pixels inside the output; H (setHud) takes it away again without re-rendering the retro frame. Without
// hudTitle (the exports) only the card.
void testHud(evo::gfx::Context& ctx) {
    Harness harness(ctx, 640, 360, true, true, true);
    const TimelineFrame boot = evo::show::sampleTimeline(2.5); // BOOT with its 1993 card typed
    const Picture with = harness.render(boot);
    save(with, "boot-640x360-hud.png");
    harness.renderer().setHud(false);
    const Picture without = harness.render(boot);
    CHECK(!harness.renderer().lastStats().retroRendered);
    Harness exportHarness(ctx, 640, 360, true, true, false);
    const Picture cardsOnly = exportHarness.render(boot);

    // Pixels that differ from the HUD-less picture: where they are, and how many in the title rows.
    struct Changes {
        std::size_t changed = 0, title = 0, card = 0;
    };
    const evo::render::ThenCardLayout card =
        evo::render::thenCardLayout(evo::render::thenCardText(Effect::Boot), 640, 360);
    const auto changes = [&](const Picture& picture) {
        Changes counted;
        for (std::uint32_t y = 0; y < 360; ++y)
            for (std::uint32_t x = 0; x < 640; ++x) {
                const std::uint8_t* a = picture.at(x, y);
                const std::uint8_t* b = without.at(x, y);
                if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2])
                    continue;
                ++counted.changed;
                CHECK(x >= 8 && y >= 8 && x < 640 - 8 && y < 360 - 8);
                counted.title += y < 8 + 9;
                counted.card += static_cast<int>(x) >= card.x0 && static_cast<int>(x) < card.x1 &&
                                static_cast<int>(y) >= card.y0 && static_cast<int>(y) < card.y1;
            }
        return counted;
    };
    const Changes window = changes(with);
    const Changes exported = changes(cardsOnly);
    std::printf("hud: %zu pixels changed, %zu in the title, %zu in the card; %zu without the title\n",
                window.changed, window.title, window.card, exported.changed);
    CHECK(window.title > 20 && window.card > 100 && window.title + window.card == window.changed);
    CHECK(exported.title == 0 && exported.card == window.card && exported.changed == exported.card);
    // The title's first glyph, white on the black bar: "B" at (8, 8).
    const evo::text::GlyphRows& b = evo::text::glyph('B');
    for (std::uint32_t row = 0; row < 7; ++row)
        for (std::uint32_t col = 0; col < 7; ++col)
            if (b[row] & (0x80u >> col)) {
                const std::uint8_t* p = with.at(8 + col, 8 + row);
                CHECK(p[0] == 255 && p[1] == 255 && p[2] == 255);
            }
}

// The HUD at 1280x720 (unit 2), CRT off: the title at scale 2 from (16, 16); the 1993 card at the 8x8 font's
// scale 1 (720 / 480), two cells in from the picture's bottom-left corner; nothing within 16 pixels of an
// edge.
void testHudScaled(evo::gfx::Context& ctx) {
    Harness withHud(ctx, 1280, 720, false, true, true);
    Harness withoutHud(ctx, 1280, 720, false, false);
    const TimelineFrame boot = evo::show::sampleTimeline(2.5);
    const Picture with = withHud.render(boot);
    const Picture without = withoutHud.render(boot);
    std::uint32_t top = ~0u, bottom = 0, left = ~0u;
    for (std::uint32_t y = 0; y < 720; ++y)
        for (std::uint32_t x = 0; x < 1280; ++x)
            if (std::memcmp(with.at(x, y), without.at(x, y), 3) != 0) {
                CHECK(x >= 16 && y >= 16 && x < 1280 - 16 && y < 720 - 16);
                if (y >= 360) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                    left = std::min(left, x);
                }
            }
    // The card: one 8-pixel glyph row 16 pixels above the picture's bottom, from 16 pixels into it.
    CHECK(top >= 720 - 24 && bottom <= 720 - 17 && left >= 160 + 16 && left < 160 + 24);
    const evo::text::GlyphRows& b = evo::text::glyph('B');
    for (std::uint32_t row = 0; row < 7; ++row)
        for (std::uint32_t col = 0; col < 7; ++col)
            if (b[row] & (0x80u >> col))
                for (std::uint32_t s = 0; s < 4; ++s) {
                    const std::uint8_t* p = with.at(16 + 2 * col + s % 2, 16 + 2 * row + s / 2);
                    CHECK(p[0] == 255 && p[1] == 255 && p[2] == 255);
                }
}

// BOOT through the whole renderer, CRT and HUD off: grey DOS text on black inside the 4:3 box, and the
// flash that cuts to FLYBY fills the box with white (the bars stay black).
void testBoot(evo::gfx::Context& ctx) {
    Harness harness(ctx, 640, 360, false);
    const Picture prompt = harness.render(evo::show::sampleTimeline(3.0));
    save(prompt, "boot-640x360-prompt.png");
    std::size_t grey = 0, other = 0;
    for (std::uint32_t y = 0; y < 360; ++y)
        for (std::uint32_t x = 0; x < 640; ++x) {
            const std::uint8_t* p = prompt.at(x, y);
            if (p[0] == 0xAA && p[1] == 0xAA && p[2] == 0xAA)
                ++grey;
            else if (!prompt.black(x, y))
                ++other;
        }
    std::printf("boot: %zu grey pixels at t=3.0\n", grey);
    CHECK(grey > 0 && other == 0);
    CHECK(contentBounds(prompt).left >= 80 && contentBounds(prompt).right < 560);

    const TimelineFrame any = evo::show::sampleTimeline(1.0);
    const std::uint32_t flash = evo::retro::bootFlashFrame(any);
    const Picture white = harness.render(evo::show::sampleTimeline((flash + .5) / 35.0));
    CHECK(harness.renderer().lastStats().retroFrame == flash);
    CHECK(contentBounds(white) == expectedBounds(640, 360));
    for (std::uint32_t y = 0; y < 360; y += 7)
        for (std::uint32_t x = 80; x < 560; x += 7)
            CHECK(white.at(x, y)[0] == 255 && white.at(x, y)[1] == 255 && white.at(x, y)[2] == 255);
}

// PLASMA's 1993 picture at a retro frame (35 a second): chapter 3, era 0.
TimelineFrame plasmaFrame(std::uint32_t retroFrame) {
    TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(3) + 1.0);
    frame.era = 0.f;
    frame.wipe = 0.f;
    frame.eraCut = 0.f;
    frame.localSeconds = (retroFrame + .5) / 35.0;
    frame.showSeconds = evo::show::chapterStart(3) + frame.localSeconds;
    return frame;
}

// Two frames in flight on one renderer: A recorded and submitted, then B recorded and submitted before A is
// waited for, each with its own readback. Both must equal the same frames rendered one at a time: nothing B
// writes on the CPU (palette, glyph instances, descriptor sets) may reach A, and B's passes must wait for A's
// on the GPU. HUD and title on, so both frames also draw text.
void testTwoFramesInFlight(evo::gfx::Context& ctx) {
    constexpr std::uint32_t kWidth = 320, kHeight = 180;
    const auto serial = [&](const TimelineFrame& frame) {
        Harness harness(ctx, kWidth, kHeight, true, true, true);
        return harness.render(frame).rgb;
    };
    struct Pair {
        const char* name;
        TimelineFrame a, b;
        std::uint32_t retroA; // A's retro frame
    };
    // PLASMA at retro frames 70 and 71: the palette rotates, so the retro image re-renders with a different
    // palette. Then PLASMA followed by BOOT while it types (its DOS text re-rendered into the other slot's
    // glyph buffer).
    const TimelineFrame boot = evo::show::sampleTimeline(2.4);
    const std::array<Pair, 2> pairs = {{{"plasma 70, 71", plasmaFrame(70), plasmaFrame(71), 70},
                                        {"plasma 72, boot", plasmaFrame(72), boot, 72}}};
    CHECK(pairs[0].a.effect == Effect::Plasma && boot.effect == Effect::Boot);
    for (const Pair& pair : pairs) {
        const std::vector<std::uint8_t> expectedA = serial(pair.a);
        const std::vector<std::uint8_t> expectedB = serial(pair.b);
        CHECK(expectedA != expectedB);

        evo::gfx::Readback readbackA(ctx, kWidth, kHeight);
        evo::gfx::Readback readbackB(ctx, kWidth, kHeight);
        evo::gfx::FrameRing ring(ctx);
        Renderer renderer(ctx, ring, {kWidth, kHeight, true, true, true});

        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, pair.a);
        CHECK(renderer.lastStats().retroRendered && renderer.lastStats().retroFrame == pair.retroA);
        readbackA.record(cmd, renderer.output());
        const std::uint64_t doneA = ring.submit();

        cmd = ring.begin(); // the other slot: does not wait for A
        renderer.render(cmd, pair.b);
        CHECK(renderer.lastStats().retroRendered && renderer.lastStats().retroFrame != pair.retroA);
        readbackB.record(cmd, renderer.output());
        const std::uint64_t doneB = ring.submit();

        ring.wait(doneB);
        ring.wait(doneA);
        const bool sameA = readbackA.rgb() == expectedA, sameB = readbackB.rgb() == expectedB;
        std::printf("in flight (%s): A %s, B %s\n", pair.name, sameA ? "matches" : "DIFFERS",
                    sameB ? "matches" : "DIFFERS");
        CHECK(sameA && sameB);
    }
}

// An ASCENT frame: a 2026-only chapter that shows the placeholder (STUDIO NIGHT, the first 2026-only
// chapter, path traces the Studio around the CRT since Task 10, so it runs the retro stage too), so the
// colourful placeholder fills the whole output.
TimelineFrame ascent(double localSeconds) {
    const int chapter = 12;
    TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(chapter) + localSeconds);
    CHECK(frame.effect == Effect::Ascent && frame.era == 1.f);
    return frame;
}

RenderSettings modeSettings(std::uint32_t width, std::uint32_t height, RenderMode mode) {
    RenderSettings settings{width, height};
    settings.mode = mode;
    return settings;
}

// (a) A frame runs only the stages it shows: BOOT records no modern pass, ASCENT no retro pass (the
// placeholder samples nothing of 1993), in both modes.
void testStagesSkipped(evo::gfx::Context& ctx) {
    for (RenderMode mode : {RenderMode::Export, RenderMode::Interactive}) {
        Harness harness(ctx, modeSettings(160, 90, mode));
        harness.render(evo::show::sampleTimeline(1.0));
        CHECK(harness.renderer().lastStats().retroRendered && !harness.renderer().lastStats().modernRendered);
        CHECK(harness.renderer().lastStats().historyReset == HistoryReset::None);
        harness.render(ascent(2.0));
        CHECK(!harness.renderer().lastStats().retroRendered && harness.renderer().lastStats().modernRendered);
    }
}

// (b) a skipped stage's stale image never reaches the output. A colourful 2026 frame, then
// BOOT at t = 1, equals a fresh renderer's BOOT at t = 1 byte for byte; BOOT, then ASCENT with the
// retro stage skipped, equals a fresh ASCENT. With the CRT and the HUD on, as an export draws.
void testSkippedStageNeverShows(evo::gfx::Context& ctx) {
    const RenderSettings settings{320, 180, true, true};
    const TimelineFrame boot = evo::show::sampleTimeline(1.0);
    const TimelineFrame night = ascent(2.0);

    Harness freshBoot(ctx, settings), freshNight(ctx, settings);
    const Picture bootAlone = freshBoot.render(boot);
    const Picture nightAlone = freshNight.render(night);
    CHECK(bootAlone.rgb != nightAlone.rgb);

    Harness modernFirst(ctx, settings);
    modernFirst.render(night);
    CHECK(modernFirst.render(boot).rgb == bootAlone.rgb);
    CHECK(!modernFirst.renderer().lastStats().modernRendered);

    Harness retroFirst(ctx, settings);
    retroFirst.render(boot);
    CHECK(retroFirst.render(night).rgb == nightAlone.rgb);
    CHECK(!retroFirst.renderer().lastStats().retroRendered);
}

// The window keeps the hidden 2026 side of a BeatSwap warm, an export skips it; both show the
// same 1993 picture.
void testInteractiveKeepsHiddenModernWarm(evo::gfx::Context& ctx) {
    TimelineFrame dots = evo::show::sampleTimeline(evo::show::chapterStart(2) + 1.0);
    CHECK(dots.transition == Transition::BeatSwap);
    dots.era = 0.f;
    dots.eraCut = 0.f;
    Harness exported(ctx, modeSettings(160, 90, RenderMode::Export));
    Harness window(ctx, modeSettings(160, 90, RenderMode::Interactive));
    const Picture a = exported.render(dots);
    const Picture b = window.render(dots);
    CHECK(!exported.renderer().lastStats().modernRendered);
    CHECK(window.renderer().lastStats().modernRendered);
    CHECK(a.rgb == b.rgb);
}

// The history state the renderer hands the modern stage: a reset on the first modern frame, frames counted
// after it, nothing while the modern stage is skipped, and a reset when it runs again.
void testHistoryResets(evo::gfx::Context& ctx) {
    Harness harness(ctx, modeSettings(160, 90, RenderMode::Interactive));
    const auto stats = [&] { return harness.renderer().lastStats(); };
    harness.render(ascent(2.0));
    CHECK(stats().historyReset == HistoryReset::FirstFrame && stats().framesSinceReset == 0);
    harness.render(ascent(2.0 + 1.0 / 60));
    CHECK(stats().historyReset == HistoryReset::None && stats().framesSinceReset == 1);
    harness.render(evo::show::sampleTimeline(1.0)); // BOOT: no modern pass
    CHECK(!stats().modernRendered && stats().historyReset == HistoryReset::None);
    CHECK(stats().framesSinceReset == 0); // zeroed while the modern stage is skipped
    harness.render(ascent(2.0 + 2.0 / 60));
    CHECK(stats().historyReset == HistoryReset::Skipped && stats().framesSinceReset == 0);
    harness.render(ascent(5.0));
    CHECK(stats().historyReset == HistoryReset::TimeJump);
    harness.resize(192, 108);
    harness.render(ascent(5.0 + 1.0 / 60));
    CHECK(stats().historyReset == HistoryReset::Resize);
    harness.render(ascent(5.0 + 2.0 / 60));
    CHECK(stats().historyReset == HistoryReset::None && stats().framesSinceReset == 1);
    CHECK(harness.renderer().maxBounces() == 4);

    // A resize A -> B -> A with no frame in between still resets; resizing to the current size does not.
    harness.resize(160, 90);
    harness.resize(192, 108);
    harness.render(ascent(5.0 + 3.0 / 60));
    CHECK(stats().historyReset == HistoryReset::Resize);
    harness.resize(192, 108);
    harness.render(ascent(5.0 + 4.0 / 60));
    CHECK(stats().historyReset == HistoryReset::None);
}

// The stats of a skipped stage are zero, not the last frame it rendered: retroFrame while the retro stage
// does not run (ASCENT), framesSinceReset while the modern stage does not (BOOT, in an export).
void testSkippedStageStats(evo::gfx::Context& ctx) {
    Harness harness(ctx, modeSettings(160, 90, RenderMode::Export));
    const auto stats = [&] { return harness.renderer().lastStats(); };
    harness.render(plasmaFrame(70));
    CHECK(stats().retroRendered && stats().retroFrame == 70 && !stats().modernRendered);
    CHECK(stats().framesSinceReset == 0);
    harness.render(ascent(2.0));
    harness.render(ascent(2.0 + 1.0 / 60));
    CHECK(!stats().retroRendered && stats().retroFrame == 0);
    CHECK(stats().modernRendered && stats().framesSinceReset == 1);
    harness.render(plasmaFrame(71));
    CHECK(stats().retroRendered && stats().retroFrame == 71);
    CHECK(!stats().modernRendered && stats().framesSinceReset == 0);
}

TimelineFrame studioNight(double fraction) {
    const int chapter = 11;
    const double start = evo::show::chapterStart(chapter), end = evo::show::chapterStart(chapter + 1);
    TimelineFrame frame = evo::show::sampleTimeline(start + fraction * (end - start));
    CHECK(frame.effect == Effect::StudioNight);
    return frame;
}

// The window prepares the Studio's GPU side, then resizes: the path tracer and the denoiser follow the new
// size. The first frame after the resize resets the history (Resize) and so equals a fresh renderer's first
// frame at that size and time byte for byte, and the next one carries on (no reset), validation clean.
void testStudioResize(evo::gfx::Context& ctx) {
    Harness harness(ctx, modeSettings(160, 90, RenderMode::Interactive));
    const auto stats = [&] { return harness.renderer().lastStats(); };
    harness.renderer().prepareAll();
    const TimelineFrame start = studioNight(0.3);
    harness.render(start);
    harness.render(evo::show::sampleTimeline(start.showSeconds + 1.0 / 60));
    CHECK(stats().modernRendered && stats().historyReset == HistoryReset::None);
    harness.resize(192, 108);
    const TimelineFrame after = evo::show::sampleTimeline(start.showSeconds + 2.0 / 60);
    const Picture resized = harness.render(after);
    CHECK(stats().historyReset == HistoryReset::Resize && stats().framesSinceReset == 0);
    const VkExtent2D extent = harness.renderer().output().extent();
    CHECK(extent.width == 192 && extent.height == 108);
    CHECK(resized.width == 192 && resized.height == 108);
    Harness fresh(ctx, modeSettings(192, 108, RenderMode::Interactive));
    CHECK(fresh.render(after).rgb == resized.rgb);
    harness.render(evo::show::sampleTimeline(after.showSeconds + 1.0 / 60));
    CHECK(stats().historyReset == HistoryReset::None && stats().framesSinceReset == 1);
}

// A prepared renderer (the GPU side built up front, as the window and the exports do) and a lazily built one
// (on its first Studio frame) render the same first Studio frame, byte for byte.
void testPreparedEqualsLazy(evo::gfx::Context& ctx) {
    const TimelineFrame frame = studioNight(0.4);
    Harness prepared(ctx, modeSettings(160, 90, RenderMode::Export));
    prepared.renderer().prepareAll();
    Harness lazy(ctx, modeSettings(160, 90, RenderMode::Export));
    const Picture a = prepared.render(frame);
    const Picture b = lazy.render(frame);
    CHECK(prepared.renderer().lastStats().modernRendered && lazy.renderer().lastStats().modernRendered);
    CHECK(a.rgb == b.rgb);
}

// prepare(times) builds the GPU side of exactly the scenes those times show: none for 1993 and placeholder
// chapters, the Studio (and with it the shared tracer) for a Studio chapter, once.
void testPrepareTimes(evo::gfx::Context& ctx) {
    Harness harness(ctx, modeSettings(160, 90, RenderMode::Export));
    evo::render::ModernStage& stage = harness.renderer().modern();
    const std::vector<double> plasma{evo::show::chapterStart(3) + 1.0, evo::show::chapterStart(4) - 1.0};
    harness.renderer().prepare(plasma);
    CHECK(stage.tracer() == nullptr && stage.gpuBytes() == 0);
    const std::vector<double> mixed{plasma[0], studioNight(0.2).showSeconds, studioNight(0.9).showSeconds};
    harness.renderer().prepare(mixed);
    CHECK(stage.tracer() != nullptr && stage.sceneGpu(evo::modern::SceneId::Studio) != nullptr);
    for (std::size_t id = 0; id < evo::modern::kSceneCount; ++id) // no other scene
        if (static_cast<evo::modern::SceneId>(id) != evo::modern::SceneId::Studio)
            CHECK(stage.sceneGpu(static_cast<evo::modern::SceneId>(id)) == nullptr);
    const VkDeviceSize bytes = stage.gpuBytes();
    harness.renderer().prepare(mixed); // once
    CHECK(stage.gpuBytes() == bytes);
    harness.renderer().prepareAll(); // every scene of the show: at least the Studio's bytes
    CHECK(stage.gpuBytes() >= bytes);
}

void testRejectsEmptySize(evo::gfx::Context& ctx) {
    evo::gfx::FrameRing ring(ctx);
    bool threw = false;
    try {
        Renderer renderer(ctx, ring, {0, 100});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        RenderSettings settings{16, 16};
        settings.spp = 0;
        Renderer renderer(ctx, ring, settings);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        testEffectRegistry();
        testHudUnit();
        testHudItems();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testPillarbox(ctx);
            testTallOutput(ctx);
            testCrtKeepsGeometry(ctx);
            testRetroFrameLock(ctx);
            testChapterChangeInvalidates(ctx);
            testWipe(ctx);
            testEraCutFlash(ctx);
            testHud(ctx);
            testHudScaled(ctx);
            testBoot(ctx);
            testTwoFramesInFlight(ctx);
            testStagesSkipped(ctx);
            testSkippedStageNeverShows(ctx);
            testInteractiveKeepsHiddenModernWarm(ctx);
            testHistoryResets(ctx);
            testSkippedStageStats(ctx);
            testStudioResize(ctx);
            testPreparedEqualsLazy(ctx);
            testPrepareTimes(ctx);
            testRejectsEmptySize(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        // Also counts what the layer reported while the objects and the Context were destroyed.
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("renderer_test: %d checks passed\n", checksPassed);
    return 0;
}
