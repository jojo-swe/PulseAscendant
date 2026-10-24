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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using evo::render::Renderer;
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

// Points shaderDir() at the build's compiled shaders.
void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

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
        : ctx_(ctx), readback_(std::in_place, ctx, width, height), ring_(ctx),
          renderer_(ctx, ring_, {width, height, crt, hud, hudTitle}) {}

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

// A frame of an effect that keeps the test pattern after step 2, with nothing of 2026 in it.
TimelineFrame retroFrame(int chapter, double localSeconds) {
    TimelineFrame frame;
    frame.chapter = chapter;
    frame.effect = Effect::Dots;
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

// The CPU side: every effect has a shader or text, a rate and a palette whose index 0 is black.
void testEffectRegistry() {
    std::array<std::uint32_t, 256> palette{};
    evo::retro::grayPalette(palette, TimelineFrame{}, 0);
    CHECK(palette[0] == 0xff000000u);
    CHECK(palette[128] == 0xff808080u);
    CHECK(palette[255] == 0xffffffffu);

    for (int i = 0; i < evo::show::chapterCount(); ++i) {
        const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(i) + .5);
        const evo::retro::EffectInfo& info = evo::retro::effectFor(frame.effect);
        CHECK((info.shader != nullptr || info.text != nullptr) && info.retroRate > 0 &&
              info.palette != nullptr);
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

// (b) Review Focus 1: a tall 400x600 output letterboxes the 4:3 picture to 400x300, centred: rows 0..149
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

// (c) Review Focus 4: the CRT changes the look, never the content rectangle, at any output shape. Odd sizes
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

// (e) Review Focus 2: a chapter change at an equal retro frame must not reuse the cached image.
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

// The HUD layout on the CPU: which texts, where, each after its shadow, all inside the margins, at every
// HUD unit.
void testHudItems() {
    using evo::render::hudItems;

    // Boot, era 0: the title at (8u, 8u) at scale u, the 1993 card bottom-left at 2u (16u pixels high), its
    // shadow's bottom row 8u above the edge. Without the title, only the card.
    const TimelineFrame boot = evo::show::sampleTimeline(1.0);
    for (const auto& [width, height, u] : kHudSizes) {
        auto items = hudItems(boot, width, height, true);
        CHECK(items.size() == 4 && shadowed(items, u) && insideHudMargins(items, width, height));
        CHECK(items[1].text == "BOOT" && items[1].x == 8 * u && items[1].y == 8 * u && items[1].scale == u);
        CHECK(items[1].rgba == 0xFFFFFFFFu && items[3].rgba == 0xFFE0E0E0u);
        CHECK(items[3].text == boot.thenCard && items[3].scale == 2 * u && items[3].x == 8 * u);
        CHECK(items[3].y + 16 * u + u == static_cast<float>(height) - 8 * u);
        const auto cardsOnly = hudItems(boot, width, height, false);
        CHECK(cardsOnly.size() == 2 && shadowed(cardsOnly, u));
        CHECK(cardsOnly[1].text == boot.thenCard && cardsOnly[1].x == items[3].x &&
              cardsOnly[1].y == items[3].y && cardsOnly[1].scale == items[3].scale);
    }

    // The 2026 card from era 0.5; an empty card is left out.
    TimelineFrame frame = retroFrame(2, 1.0);
    frame.title = "DOTS";
    frame.thenCard = "1993 THEN";
    frame.nowCard = "2026 NOW";
    frame.era = .5f;
    auto items = hudItems(frame, 640, 360, true);
    CHECK(items.size() == 4 && items[3].text == "2026 NOW");
    frame.era = .49f;
    CHECK(hudItems(frame, 640, 360, true)[3].text == "1993 THEN");
    frame.thenCard = "";
    CHECK(hudItems(frame, 640, 360, true).size() == 2);
    CHECK(hudItems(frame, 640, 360, false).empty());

    // Wipe, while the wipe is under way: 2026 bottom-left, 1993 bottom-right on the same line when they
    // fit (in an output twice as wide as 16:9)...
    frame.transition = Transition::Wipe;
    frame.wipe = .5f;
    frame.thenCard = "1993 \xC2\xB7 PALETTE CYCLING";
    frame.nowCard = "2026 \xC2\xB7 VOLUMETRIC NEBULA";
    for (const auto& [width, height, u] : {HudSize{1280, 360, 1}, HudSize{2560, 720, 2}}) {
        items = hudItems(frame, width, height, true);
        CHECK(items.size() == 6 && shadowed(items, u) && insideHudMargins(items, width, height));
        CHECK(items[3].text == frame.nowCard && items[3].x == 8 * u && items[3].scale == 2 * u);
        CHECK(items[5].text == frame.thenCard && items[5].y == items[3].y);
        CHECK(items[5].x + evo::text::textWidth(frame.thenCard, 2 * u) ==
              static_cast<float>(width) - 8 * u - u);
    }
    // ...and the 1993 card a line up when they would meet.
    items = hudItems(frame, 640, 360, true);
    CHECK(items.size() == 6 && insideHudMargins(items, 640, 360));
    CHECK(items[5].y + 16 + 1 < items[3].y);
    frame.nowCard = "";
    items = hudItems(frame, 640, 360, true);
    CHECK(items.size() == 4 && items[3].text == frame.thenCard && items[3].y == 360 - 25);

    // Before the wipe only the 1993 card, after it only the 2026 card, bottom-left like any other chapter,
    // whatever the era says; both only strictly between.
    frame.nowCard = "2026 \xC2\xB7 VOLUMETRIC NEBULA";
    for (float era : {0.f, 1.f}) {
        frame.era = era;
        frame.wipe = 0.f;
        items = hudItems(frame, 640, 360, true);
        CHECK(items.size() == 4 && shadowed(items, 1) && insideHudMargins(items, 640, 360));
        CHECK(items[3].text == frame.thenCard && items[3].x == 8 && items[3].y == 360 - 25);
        frame.wipe = 1.f;
        items = hudItems(frame, 640, 360, true);
        CHECK(items.size() == 4 && items[3].text == frame.nowCard && items[3].x == 8 &&
              items[3].y == 360 - 25);
    }
    for (float wipe : {.001f, .999f}) {
        frame.wipe = wipe;
        CHECK(hudItems(frame, 640, 360, true).size() == 6);
    }
    // The show's Wipe chapters: one card at their start and end, both in the middle of the wipe.
    for (int i = 0; i < evo::show::chapterCount(); ++i) {
        if (evo::show::cues[i].transition != Transition::Wipe)
            continue;
        const double start = evo::show::chapterStart(i), end = evo::show::chapterStart(i + 1);
        const TimelineFrame first = evo::show::sampleTimeline(start + .01);
        const TimelineFrame last = evo::show::sampleTimeline(end - .01);
        CHECK(first.wipe == 0.f && hudItems(first, 1280, 720, true).size() == 4);
        CHECK(hudItems(first, 1280, 720, true)[3].text == std::string(first.thenCard));
        CHECK(last.wipe == 1.f && hudItems(last, 1280, 720, true).size() == 4);
        CHECK(hudItems(last, 1280, 720, true)[3].text == std::string(last.nowCard));
        double t = start;
        while (t < end && evo::show::sampleTimeline(t).wipe <= 0.f)
            t += .01;
        const TimelineFrame during = evo::show::sampleTimeline(t + .05);
        CHECK(during.wipe > 0.f && during.wipe < 1.f && hudItems(during, 1280, 720, true).size() == 6);
        for (const auto& [width, height, u] : kHudSizes) {
            const auto both = hudItems(during, width, height, false);
            CHECK(both.size() == 4 && shadowed(both, u) && insideHudMargins(both, width, height));
        }
    }

    // A card too wide at 2 units is drawn at 1: the longest card at 640x360 (41 glyphs, 656 pixels at 2x),
    // bottom-left with its shadow's bottom row on the margin. A 16:9 output has room for 38 glyphs at 2 units
    // whatever its size, so at 1280x720 it is drawn at 1 unit (scale 2) too.
    frame.transition = Transition::BeatSwap;
    frame.era = 1.f;
    frame.nowCard = "2026 \xC2\xB7 REFRACTION \xC2\xB7 DISPERSION \xC2\xB7 CAUSTICS";
    CHECK(evo::text::textWidth(frame.nowCard, 2) == 656);
    items = hudItems(frame, 640, 360, true);
    CHECK(items.size() == 4 && shadowed(items, 1) && insideHudMargins(items, 640, 360));
    CHECK(items[3].text == frame.nowCard && items[3].scale == 1 && items[2].scale == 1);
    CHECK(items[3].x == 8 && items[3].y == 360 - 8 - 8 - 1);
    items = hudItems(frame, 1280, 720, true);
    CHECK(items[3].scale == 2 && shadowed(items, 2) && insideHudMargins(items, 1280, 720));
    CHECK(items[3].y + 16 + 2 == 720 - 16);
    // The same during a wipe, for either card.
    frame.transition = Transition::Wipe;
    frame.wipe = .5f;
    frame.thenCard = "1993 \xC2\xB7 24 TRIANGLES \xC2\xB7 TRANSPARENT VECTORS";
    items = hudItems(frame, 640, 360, true);
    CHECK(items.size() == 6 && shadowed(items, 1) && insideHudMargins(items, 640, 360));
    CHECK(items[3].scale == 1 && items[5].scale == 1 && items[5].y + 8 + 1 < items[3].y);

    // The fallback's boundary: a card exactly as wide at 2 units as the room between the margins (the
    // shadow's unit included) stays at 2 units; one pixel narrower an output and it drops to 1 unit.
    frame.transition = Transition::Cut;
    frame.era = 0.f;
    frame.thenCard = "1993 THEN"; // 9 glyphs
    for (const auto& [ignored, height, u] : kHudSizes) {
        const auto limit =
            static_cast<std::uint32_t>(evo::text::textWidth(frame.thenCard, 2 * u) + 16 * u + u);
        items = hudItems(frame, limit, height, false);
        CHECK(items.size() == 2 && items[1].scale == 2 * u && insideHudMargins(items, limit, height));
        CHECK(items[1].x + evo::text::textWidth(frame.thenCard, 2 * u) + u ==
              static_cast<float>(limit - 8 * u));
        items = hudItems(frame, limit - 1, height, false);
        CHECK(items.size() == 2 && items[1].scale == u && insideHudMargins(items, limit - 1, height));
        CHECK(items[1].y + 8 * u + u == static_cast<float>(height - 8 * u));
    }

    // Every card of the show at every HUD unit: at 2 units (16u pixels high) where it fits between the
    // margins, else at 1 unit; bottom-left on the margins, inside them.
    for (const evo::show::Cue& cue : evo::show::cues)
        for (const char* card : {cue.thenCard, cue.nowCard}) {
            if (*card == '\0')
                continue;
            TimelineFrame one = retroFrame(cue.effect == Effect::Boot ? 0 : 2, 1.0);
            one.thenCard = card;
            for (const auto& [width, height, u] : kHudSizes) {
                items = hudItems(one, width, height, false);
                CHECK(items.size() == 2 && shadowed(items, u) && insideHudMargins(items, width, height));
                const bool fits = evo::text::textWidth(card, 2 * u) <= static_cast<int>(width) - 17 * u;
                CHECK(items[1].scale == (fits ? 2 * u : u));
                CHECK(items[1].x == 8 * u &&
                      items[1].y + 8 * items[1].scale + u == static_cast<float>(height - 8 * u));
            }
        }

    // Every chapter of the show at every HUD unit, at its start and in its middle, with and without the
    // title.
    for (int i = 0; i < evo::show::chapterCount(); ++i)
        for (double into : {.5, (evo::show::chapterStart(i + 1) - evo::show::chapterStart(i)) / 2}) {
            const TimelineFrame sample = evo::show::sampleTimeline(evo::show::chapterStart(i) + into);
            for (const auto& [width, height, u] : kHudSizes) {
                const auto withTitle = hudItems(sample, width, height, true);
                const auto cardsOnly = hudItems(sample, width, height, false);
                CHECK(insideHudMargins(withTitle, width, height) && shadowed(withTitle, u));
                CHECK(withTitle.size() == cardsOnly.size() + 2); // every chapter has a title
                CHECK(std::none_of(cardsOnly.begin(), cardsOnly.end(),
                                   [&](const evo::text::Item& item) { return item.text == sample.title; }));
            }
        }
}

// The HUD drawn: only the title and card areas change, at least 8 pixels inside the output; H (setHud)
// takes it away again without re-rendering the retro frame. Without hudTitle (the exports) only the card.
void testHud(evo::gfx::Context& ctx) {
    Harness harness(ctx, 640, 360, true, true, true);
    const TimelineFrame boot = evo::show::sampleTimeline(1.0);
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
                counted.card += y >= 360 - 8 - 17;
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

// The HUD at 1280x720 (unit 2): the title at scale 2 from (16, 16), the card 32 pixels high, nothing within
// 16 pixels of an edge.
void testHudScaled(evo::gfx::Context& ctx) {
    Harness withHud(ctx, 1280, 720, false, true, true);
    Harness withoutHud(ctx, 1280, 720, false, false);
    const TimelineFrame boot = evo::show::sampleTimeline(1.0);
    const Picture with = withHud.render(boot);
    const Picture without = withoutHud.render(boot);
    std::uint32_t top = ~0u, bottom = 0;
    for (std::uint32_t y = 0; y < 720; ++y)
        for (std::uint32_t x = 0; x < 1280; ++x)
            if (std::memcmp(with.at(x, y), without.at(x, y), 3) != 0) {
                CHECK(x >= 16 && y >= 16 && x < 1280 - 16 && y < 720 - 16);
                if (y >= 360) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
    // The card: 32 rows of glyph cell and 2 of shadow, its last row 16 pixels above the edge (glyph rows may
    // be empty at the top or bottom, so within the cell).
    CHECK(top >= 720 - 16 - 34 && bottom <= 720 - 16 - 1 && bottom - top >= 24);
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

void testRejectsEmptySize(evo::gfx::Context& ctx) {
    evo::gfx::FrameRing ring(ctx);
    bool threw = false;
    try {
        Renderer renderer(ctx, ring, {0, 100});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    try {
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
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
