// BOOT, chapter 0: the DOS prompt script on its beats on the CPU, and its 320x200 palette index image from
// the retro stage (a text-only effect: cleared, then drawn with the text pass) on the GPU. Runs with the
// validation layer on and fails on any message it reports.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/sync.hpp"
#include "render/retro_stage.hpp"
#include "retro/boot.hpp"
#include "retro/effects.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"
#include "text/font8x8.hpp"
#include "text/text_pass.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::render::RetroStage;
using evo::show::Effect;
using evo::show::TimelineFrame;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr std::uint32_t kWidth = RetroStage::kWidth, kHeight = RetroStage::kHeight;

// Chapter 0 starts at show time 0, so a show time is BOOT's local time.
TimelineFrame boot(double seconds) {
    const TimelineFrame frame = evo::show::sampleTimeline(seconds);
    if (frame.chapter != 0 || frame.effect != Effect::Boot)
        throw std::runtime_error("expected a BOOT frame at " + std::to_string(seconds));
    return frame;
}

// The middle of a retro frame of BOOT.
double middleOf(std::uint32_t retroFrame) {
    return (retroFrame + .5) / evo::retro::kBootRetroRate;
}

// A 320x200 palette index image, row by row.
struct Indices {
    std::vector<std::uint8_t> pixels;

    std::uint8_t at(int x, int y) const { return pixels[static_cast<std::size_t>(y) * kWidth + x]; }
    std::size_t count(std::uint8_t index) const { return std::count(pixels.begin(), pixels.end(), index); }
    std::size_t nonZero() const { return pixels.size() - count(0); }
};

// A retro stage with its own ring and text pass; every render() submits, waits and reads the index image
// back.
class Fixture {
public:
    explicit Fixture(evo::gfx::Context& ctx)
        : ring_(ctx), text_(ctx, ring_), stage_(ctx, ring_, text_),
          buffer_(ctx, {VkDeviceSize{kWidth} * kHeight, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                        "boot test readback"}) {}

    Indices render(const TimelineFrame& frame, bool* rendered = nullptr) {
        VkCommandBuffer cmd = ring_.begin();
        const bool recorded = stage_.render(cmd, frame);
        if (rendered)
            *rendered = recorded;
        evo::gfx::Image& index = stage_.indexImage();
        index.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kWidth, kHeight, 1};
        vkCmdCopyImageToBuffer(cmd, index.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_.handle(), 1,
                               &region);
        evo::gfx::bufferBarrier2(cmd, buffer_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                 VK_ACCESS_2_HOST_READ_BIT);
        ring_.wait(ring_.submit());
        buffer_.invalidate();
        const auto* data = static_cast<const std::uint8_t*>(buffer_.mapped());
        return {{data, data + buffer_.size()}};
    }

    RetroStage& stage() { return stage_; }

private:
    evo::gfx::FrameRing ring_;
    evo::text::TextPass text_;
    RetroStage stage_;
    evo::gfx::Buffer buffer_;
};

// The registry entry: no shader, the text provider, 35 Hz, phosphor and the DOS colours.
void testRegistry() {
    const evo::retro::EffectInfo& info = evo::retro::effectFor(Effect::Boot);
    CHECK(info.shader == nullptr);
    CHECK(info.text == &evo::retro::bootText);
    CHECK(info.background == &evo::retro::bootBackground);
    CHECK(info.retroRate == 35 && info.phosphor);

    evo::retro::Palette palette{};
    info.palette(palette, boot(1.0), 35);
    CHECK(palette[0] == evo::retro::rgba(0, 0, 0));
    CHECK(palette[7] == evo::retro::rgba(0xAA, 0xAA, 0xAA));
    CHECK(palette[8] == evo::retro::rgba(0x55, 0x55, 0x55));
    CHECK(palette[15] == evo::retro::rgba(0xFF, 0xFF, 0xFF));
}

using Lines = std::vector<std::string>;

Lines screenAt(std::uint32_t retroFrame) {
    return evo::retro::bootScreen(boot(1.0), retroFrame);
}

// The first retro frame whose screen satisfies `test`.
template <typename Test> std::uint32_t firstFrame(Test test) {
    for (std::uint32_t f = 0; f < 260; ++f)
        if (test(screenAt(f)))
            return f;
    throw std::runtime_error("the screen never shows what the test waits for");
}

// Within one retro frame of a chapter time: the frame that time falls in.
bool onBeat(std::uint32_t retroFrame, double seconds) {
    return retroFrame == static_cast<std::uint32_t>(std::floor(seconds * 35 + 1e-9));
}

// The script on the beats: the BIOS line, the prompt, the first and last key, the Enter on the accent, the
// detection lines and LOADING each from the retro frame its beat falls in.
void testScript() {
    const TimelineFrame frame = boot(1.0);
    const auto beat = [&frame](int i) { return evo::retro::chapterBeat(frame, i); };
    std::printf("boot: beats 0, 2, 4, 6 at %.3f %.3f %.3f %.3f s, accent %.3f s\n", beat(0), beat(2), beat(4),
                beat(6), frame.accentSeconds);
    CHECK(std::fabs(beat(0) - 0.41) < 0.01 && std::fabs(beat(2) - 1.26) < 0.01);
    CHECK(std::fabs(beat(4) - 2.11) < 0.01 && std::fabs(beat(6) - 2.96) < 0.01);
    CHECK(std::fabs(frame.accentSeconds - 3.05) < 0.01);

    CHECK(screenAt(0).empty()); // black until the first beat
    CHECK(onBeat(firstFrame([](const Lines& s) { return !s.empty(); }), beat(0)));
    CHECK(screenAt(20) == Lines{evo::retro::kBootBiosLine});
    const auto row = [](const Lines& s, std::size_t r) { return s.size() > r ? s[r] : std::string(); };
    CHECK(onBeat(firstFrame([&](const Lines& s) { return row(s, 2) == "C:\\DEMO>"; }), beat(2)));
    CHECK(onBeat(firstFrame([&](const Lines& s) { return row(s, 2) == "C:\\DEMO>E"; }), beat(4)));
    CHECK(onBeat(firstFrame([&](const Lines& s) { return row(s, 2) == "C:\\DEMO>EVOLUTION.EXE"; }), beat(6)));
    // The keys in between come one at a time, in order.
    std::size_t typed = 0;
    for (std::uint32_t f = 0; f < 120; ++f) {
        const std::size_t now = row(screenAt(f), 2).size();
        CHECK(now == typed || now == typed + 1 || typed == 0);
        typed = std::max(typed, now);
    }
    // The Enter: the cursor drops to the next row on the accent.
    const auto rowsWritten = [&frame](std::uint32_t f) {
        return evo::retro::promptRows(evo::retro::promptWritten(evo::retro::bootScript(frame), f)).size();
    };
    std::uint32_t enter = 0;
    while (rowsWritten(enter) < 4)
        ++enter;
    CHECK(onBeat(enter, frame.accentSeconds));
    CHECK(
        onBeat(firstFrame([&](const Lines& s) { return row(s, 3).starts_with("Sound Blaster"); }), beat(7)));
    CHECK(onBeat(firstFrame([&](const Lines& s) { return row(s, 4).ends_with("found"); }), beat(8)));
    CHECK(onBeat(firstFrame([&](const Lines& s) { return row(s, 5).starts_with("Ray tracing"); }), beat(9)));
    CHECK(onBeat(firstFrame([&](const Lines& s) { return row(s, 6) == "not yet"; }), beat(11)));
    CHECK(onBeat(firstFrame([&](const Lines& s) { return row(s, 9).starts_with("LOADING"); }), beat(15)));
    // The leaders type at 30 characters a second: 5 frames in, 5 * 30 / 35 = 4 characters (a space, 3 dots).
    const std::uint32_t label =
        firstFrame([&](const Lines& s) { return row(s, 5).starts_with("Ray tracing"); });
    CHECK(row(screenAt(label), 5) == "Ray tracing hardware");
    CHECK(row(screenAt(label + 5), 5) == "Ray tracing hardware ...");
}

// The detection lines exactly as written, each wrapped at its result like a 40-column screen; the applause
// line fills the slot of rows 7 and 8, and the screen just before the flash is the whole script.
void testDetection() {
    CHECK(std::string(evo::retro::kBootSoundCard) == "Sound Blaster 16 at 220h IRQ 5 DMA 1 ... found");
    CHECK(std::string(evo::retro::kBootRayTracing) == "Ray tracing hardware .................. not yet");
    const TimelineFrame frame = boot(1.0);
    const std::uint32_t flash = evo::retro::bootFlashFrame(frame);
    const Lines full = evo::retro::bootScreen(frame, flash - 1);
    CHECK(full == (Lines{evo::retro::kBootBiosLine, "", "C:\\DEMO>EVOLUTION.EXE",
                         "Sound Blaster 16 at 220h IRQ 5 DMA 1", "... found",
                         "Ray tracing hardware ..................", "not yet",
                         "Applause device .......................", "not found", "LOADING..."}));
    CHECK(full[3] + " " + full[4] == evo::retro::kBootSoundCard);
    CHECK(full[5] + " " + full[6] == evo::retro::kBootRayTracing);
    CHECK(full[7] + " " + full[8] == evo::retro::kBootApplause);
    CHECK(evo::retro::kBootReservedRows == 2);
    for (std::uint32_t retroFrame = 0; retroFrame < flash; ++retroFrame) {
        const Lines screen = evo::retro::bootScreen(frame, retroFrame);
        for (const std::string& line : screen)
            CHECK(line.size() <= evo::retro::kBootColumns);
    }
}

// Where the flash goes: 4 retro frames before the chapter ends (2 white, then black into FLYBY), or at the
// accent if that came later. BOOT's accent (the Enter, about 3 s) comes earlier.
void testFlashFrame() {
    const TimelineFrame frame = boot(1.0);
    const std::uint32_t flash = evo::retro::bootFlashFrame(frame);
    std::printf("boot: duration %.4f s, accent %.3f s, flash at retro frame %u (%.3f s)\n",
                frame.chapterDuration, frame.accentSeconds, flash, flash / 35.0);
    CHECK(frame.accentSeconds < frame.chapterDuration - 4.0 / 35);
    CHECK(flash == static_cast<std::uint32_t>(std::floor((frame.chapterDuration - 4.0 / 35) * 35)));
    CHECK(flash == 247); // for the current show data: 7.057 s, chapter end 7.1731 s
    CHECK((flash + 3.0) / 35 < frame.chapterDuration);

    TimelineFrame late = frame;
    late.accentSeconds = frame.chapterDuration - 1.0 / 35;
    CHECK(evo::retro::bootFlashFrame(late) ==
          static_cast<std::uint32_t>(std::floor(late.accentSeconds * 35)));

    CHECK(evo::retro::bootScreen(frame, flash - 1).back().starts_with("LOADING"));
    CHECK(evo::retro::bootScreen(frame, flash).empty());
    CHECK(evo::retro::bootBackground(frame, flash - 1) == 0);
    CHECK(evo::retro::bootBackground(frame, flash) == 15);
    CHECK(evo::retro::bootBackground(frame, flash + 1) == 15);
    CHECK(evo::retro::bootBackground(frame, flash + 2) == 0);
    CHECK(evo::retro::bootText(frame, flash).empty() && evo::retro::bootText(frame, flash + 2).empty());
}

// The text, the background and the palette are functions of the retro frame: every time inside one gives
// the same result, and they read nothing of the frame but the chapter's fixed facts. Covers frames of the
// script, the two flash frames and the black after them.
void testTextIsPure() {
    const auto same = [](const std::vector<evo::text::Item>& a, const std::vector<evo::text::Item>& b) {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].scale != b[i].scale || a[i].rgba != b[i].rgba ||
                a[i].text != b[i].text)
                return false;
        return true;
    };
    for (std::uint32_t retroFrame : {0u, 17u, 70u, 105u, 175u, 238u, 246u, 247u, 248u, 249u, 250u}) {
        TimelineFrame early = boot(retroFrame / 35.0 + 1e-4);
        TimelineFrame late = boot((retroFrame + .99) / 35.0);
        late.kick = 1;
        late.energy = 1;
        late.showSeconds += 100;
        late.localSeconds += 0.3;
        CHECK(same(evo::retro::bootText(early, retroFrame), evo::retro::bootText(late, retroFrame)));
        CHECK(evo::retro::bootBackground(early, retroFrame) == evo::retro::bootBackground(late, retroFrame));
        evo::retro::Palette a{}, b{};
        b.fill(0x12345678u);
        evo::retro::bootPalette(a, early, retroFrame);
        evo::retro::bootPalette(b, late, retroFrame);
        CHECK(a == b);
    }
    // Grey 8x8 text at scale 1, from the 4-pixel margin, and the cursor as an underscore while it is on.
    const auto items = evo::retro::bootText(boot(0.5), 17);
    CHECK(items.size() == 2);
    CHECK(items[0].text == evo::retro::kBootBiosLine && items[0].x == 4 && items[0].y == 4 &&
          items[0].scale == 1 && (items[0].rgba & 0xffu) == 7);
    CHECK(items[1].text == "_" && items[1].x == 4 && items[1].y == 4 + 16 && (items[1].rgba & 0xffu) == 7);
    // On the beat: on for the first half of every beat, off for the second.
    const TimelineFrame frame = boot(1.0);
    for (int b = 0; b < 15; ++b) {
        const double beat = evo::retro::chapterBeat(frame, b), next = evo::retro::chapterBeat(frame, b + 1);
        const auto inBeat = [&](double u) {
            return static_cast<std::uint32_t>(std::floor((beat + u * (next - beat)) * 35));
        };
        CHECK(evo::retro::beatCursorOn(frame, inBeat(0.2), 35));
        CHECK(!evo::retro::beatCursorOn(frame, inBeat(0.8), 35));
    }
    CHECK(!evo::retro::beatCursorOn(frame, 5, 35)); // before the first beat
}

// The CPU rendering of a string in BOOT's grey at (x, y), compared with a rectangle of the index image.
bool matchesText(const Indices& image, const std::string& text, int x, int y) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const evo::text::GlyphRows& rows = evo::text::glyph(text[i]);
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col) {
                const bool set = rows[row] & (0x80u >> col);
                if (image.at(x + static_cast<int>(i) * 8 + col, y + row) != (set ? 7 : 0))
                    return false;
            }
    }
    return true;
}

void testImages(evo::gfx::Context& ctx) {
    Fixture fixture(ctx);

    // A test pattern frame first, which writes every pixel: BOOT must clear what it leaves.
    TimelineFrame pattern = evo::show::sampleTimeline(30.0);
    pattern.effect = Effect::Galaxy; // Act III, 2026 only: its 1993 side stays the test pattern
    CHECK(evo::retro::effectFor(Effect::Galaxy).shader == evo::retro::kTestPatternShader);
    CHECK(fixture.render(pattern).count(0) < kWidth * kHeight / 2);

    // Black until the first beat; at t = 0.5 the BIOS line and the cursor two rows down.
    CHECK(fixture.render(boot(0.3)).nonZero() == 0);
    const Indices bios = fixture.render(boot(0.5));
    CHECK(bios.count(7) == bios.nonZero());
    CHECK(matchesText(bios, evo::retro::kBootBiosLine, 4, 4));
    for (int x = 0; x < 7; ++x)
        CHECK(bios.at(4 + x, 20 + 7) == 7); // the cursor: row 7 of the third row's first cell

    // t = 3.0: "EVOLUTION.EXE" typed at the prompt.
    const Indices typed = fixture.render(boot(3.0));
    CHECK(matchesText(typed, "C:\\DEMO>EVOLUTION.EXE", 4, 20));

    // t = 5.3: the detection lines.
    const Indices cards = fixture.render(boot(5.3));
    std::printf("boot: t=3.0 has %zu set pixels, t=5.3 %zu\n", typed.nonZero(), cards.nonZero());
    CHECK(cards.nonZero() > typed.nonZero());
    CHECK(matchesText(cards, "Sound Blaster 16 at 220h IRQ 5 DMA 1", 4, 28));
    CHECK(matchesText(cards, "Ray tracing hardware ..................", 4, 44));
    CHECK(matchesText(cards, "not yet", 4, 52));

    // t = 2.00 and 2.02 are both retro frame 70: the same image, re-rendered after another frame between.
    bool rendered = false;
    const Indices first = fixture.render(boot(2.00), &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == 70);
    fixture.render(boot(1.0));
    const Indices second = fixture.render(boot(2.02), &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == 70);
    CHECK(first.pixels == second.pixels);

    // The flash: two white retro frames, then black until the chapter ends.
    const TimelineFrame any = boot(1.0);
    const std::uint32_t flash = evo::retro::bootFlashFrame(any);
    const Indices before = fixture.render(boot(middleOf(flash - 1)));
    CHECK(before.count(15) == 0 && before.nonZero() > 0);
    for (std::uint32_t white = flash; white < flash + 2; ++white) {
        const Indices image = fixture.render(boot(middleOf(white)));
        CHECK(fixture.stage().retroFrame() == white);
        std::printf("boot: retro frame %u is %.1f %% index 15\n", white,
                    100.0 * image.count(15) / image.pixels.size());
        CHECK(image.count(15) * 10 > image.pixels.size() * 9);
    }
    CHECK(fixture.render(boot(middleOf(flash + 2))).nonZero() == 0);
    CHECK(fixture.render(boot(any.chapterDuration - 1e-3)).nonZero() == 0);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testScript();
        testDetection();
        testFlashFrame();
        testTextIsPure();
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
    std::printf("boot_test: %d checks passed\n", checksPassed);
    return 0;
}
