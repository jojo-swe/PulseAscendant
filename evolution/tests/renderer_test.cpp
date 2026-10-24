// The renderer: the 1993 stage through its palette and the CRT pillarbox, the 2026 placeholder and the era
// compositor. Runs with the validation layer on and fails on any message it reports.

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "image/png.hpp"
#include "render/renderer.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// A renderer with its own ring and readback; every render() submits and waits, then reads back.
class Harness {
public:
    Harness(evo::gfx::Context& ctx, std::uint32_t width, std::uint32_t height, bool crt = true)
        : ctx_(ctx), readback_(std::in_place, ctx, width, height), ring_(ctx),
          renderer_(ctx, ring_, {width, height, crt}) {}

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

// The CPU side: every effect has a shader, a rate and a palette whose index 0 is black.
void testEffectRegistry() {
    std::array<std::uint32_t, 256> palette{};
    evo::retro::grayPalette(palette, TimelineFrame{}, 0);
    CHECK(palette[0] == 0xff000000u);
    CHECK(palette[128] == 0xff808080u);
    CHECK(palette[255] == 0xffffffffu);

    for (int i = 0; i < evo::show::chapterCount(); ++i) {
        const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(i) + .5);
        const evo::retro::EffectInfo& info = evo::retro::effectFor(frame.effect);
        CHECK(info.shader != nullptr && info.retroRate > 0 && info.palette != nullptr);
        palette.fill(0x12345678u);
        info.palette(palette, frame, 7);
        CHECK(palette[0] == 0xff000000u);
    }
}

// (a) Boot at t=1, 640x360: a 480x360 4:3 picture with 80-column black bars left and right.
void testPillarbox(evo::gfx::Context& ctx) {
    Harness harness(ctx, 640, 360);
    const TimelineFrame boot = evo::show::sampleTimeline(1.0);
    CHECK(boot.effect == Effect::Boot && boot.era == 0.0f);
    const Picture picture = harness.render(boot);
    save(picture, "boot-640x360.png");

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
    harness.render(evo::show::sampleTimeline(1.0));
    harness.resize(400, 600);
    const Picture picture = harness.render(evo::show::sampleTimeline(1.0));
    save(picture, "boot-400x600.png");

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
        const Picture withCrt = crt.render(evo::show::sampleTimeline(1.0));
        const Picture withoutCrt = plain.render(evo::show::sampleTimeline(1.0));
        std::printf("%ux%u: content %u..%u x %u..%u\n", width, height, contentBounds(withCrt).left,
                    contentBounds(withCrt).right, contentBounds(withCrt).top, contentBounds(withCrt).bottom);
        CHECK(contentBounds(withCrt) == expectedBounds(width, height));
        CHECK(contentBounds(withoutCrt) == expectedBounds(width, height));
        CHECK(withCrt.rgb != withoutCrt.rgb); // the look does change
        if (width == 640)
            save(withoutCrt, "boot-640x360-plain.png");
    }

    // Toggled on a live renderer, as key C does.
    Harness harness(ctx, 640, 360, true);
    const Picture on = harness.render(evo::show::sampleTimeline(1.0));
    harness.renderer().setCrt(false);
    const Picture off = harness.render(evo::show::sampleTimeline(1.0));
    CHECK(!harness.renderer().lastStats().retroRendered); // same retro frame: the CRT pass alone reran
    CHECK(on.rgb != off.rgb);
    CHECK(contentBounds(on) == contentBounds(off));
}

// (d) The retro stage is locked to 35 Hz: t=1.000 and t=1.010 are both retro frame 35 and give identical
// bytes (the second from the cache); t=1.030 is frame 36 and differs. CRT off, so the bytes are the retro
// image's nearest-neighbour scale.
void testRetroFrameLock(evo::gfx::Context& ctx) {
    Harness harness(ctx, 320, 240, false);
    const Picture a = harness.render(evo::show::sampleTimeline(1.000));
    CHECK(harness.renderer().lastStats().retroRendered && harness.renderer().lastStats().retroFrame == 35);
    const Picture b = harness.render(evo::show::sampleTimeline(1.010));
    CHECK(!harness.renderer().lastStats().retroRendered && harness.renderer().lastStats().retroFrame == 35);
    CHECK(a.rgb == b.rgb);
    const Picture c = harness.render(evo::show::sampleTimeline(1.030));
    CHECK(harness.renderer().lastStats().retroRendered && harness.renderer().lastStats().retroFrame == 36);
    CHECK(a.rgb != c.rgb);

    // A fresh renderer gives the same frame however it got there.
    Harness fresh(ctx, 320, 240, false);
    CHECK(fresh.render(evo::show::sampleTimeline(1.010)).rgb == a.rgb);
    CHECK(harness.render(evo::show::sampleTimeline(1.000)).rgb == a.rgb);
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
