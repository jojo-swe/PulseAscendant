// BOOT, chapter 0: the DOS prompt script on the CPU, and its 320x200 palette index image from the retro stage
// (a text-only effect: cleared, then drawn with the text pass) on the GPU. Runs with the validation layer on
// and fails on any message it reports.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/sync.hpp"
#include "render/retro_stage.hpp"
#include "retro/boot.hpp"
#include "retro/effects.hpp"
#include "show/timeline.hpp"
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

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

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

// The registry entry: no shader, the text provider, 35 Hz and the DOS colours.
void testRegistry() {
    const evo::retro::EffectInfo& info = evo::retro::effectFor(Effect::Boot);
    CHECK(info.shader == nullptr);
    CHECK(info.text == &evo::retro::bootText);
    CHECK(info.background == &evo::retro::bootBackground);
    CHECK(info.retroRate == 35);

    evo::retro::Palette palette{};
    info.palette(palette, boot(1.0), 35);
    CHECK(palette[0] == evo::retro::rgba(0, 0, 0));
    CHECK(palette[7] == evo::retro::rgba(0xAA, 0xAA, 0xAA));
    CHECK(palette[8] == evo::retro::rgba(0x55, 0x55, 0x55));
    CHECK(palette[15] == evo::retro::rgba(0xFF, 0xFF, 0xFF));
}

// The script as text at the moments the brief names.
void testScript() {
    using evo::retro::bootScreen;
    const auto screen = [](double seconds) {
        const TimelineFrame frame = boot(seconds);
        return bootScreen(frame, static_cast<std::uint32_t>(std::floor(seconds * 35)));
    };
    using Lines = std::vector<std::string>;
    CHECK(screen(0.0) == Lines{"C:\\>"});
    CHECK(screen(0.5) == Lines{"C:\\>"});
    CHECK(screen(0.9) == Lines{"C:\\>cd"}); // 0.1 s of typing at 25 characters a second
    CHECK(screen(1.5) == Lines{"C:\\>cd demo"});
    CHECK(screen(1.61) == (Lines{"C:\\>cd demo", "C:\\DEMO>"}));
    // Mid-typing: 2.4 s is retro frame 84, 7 frames after typing starts at frame 77 (2.2 s); 7 * 25 / 35 = 5
    // characters.
    CHECK(screen(2.4) == (Lines{"C:\\>cd demo", "C:\\DEMO>evolu"}));
    CHECK(screen(3.0) == (Lines{"C:\\>cd demo", "C:\\DEMO>evolution.exe"}));
    CHECK(screen(3.41) == (Lines{"C:\\>cd demo", "C:\\DEMO>evolution.exe", "", "EVOLUTION  1993 -> 2026"}));
    // Each output line starts on its time, not before: 3.95, 4.75 and 5.35 s still lack the line due next.
    CHECK(screen(3.95) == (Lines{"C:\\>cd demo", "C:\\DEMO>evolution.exe", "", "EVOLUTION  1993 -> 2026"}));
    CHECK(screen(4.01).back() == "Sound Blaster 16: 220h IRQ5 DMA1 ... OK");
    CHECK(screen(4.75).back() == "Sound Blaster 16: 220h IRQ5 DMA1 ... OK");
    CHECK(screen(4.81).back() == "Gravis Ultrasound ... not found");
    CHECK(screen(5.35).back() == "Gravis Ultrasound ... not found");
    CHECK(screen(5.0) ==
          (Lines{"C:\\>cd demo", "C:\\DEMO>evolution.exe", "", "EVOLUTION  1993 -> 2026",
                 "Sound Blaster 16: 220h IRQ5 DMA1 ... OK", "Gravis Ultrasound ... not found"}));
    CHECK(screen(5.41).back() == "VGA 320x200x256 ... OK");
    CHECK(screen(6.01).back() == "Loading");
    CHECK(screen(6.24).back() == "Loading");
    CHECK(screen(6.26).back() == "Loading.");
    CHECK(screen(6.8).back() == "Loading...");
    CHECK(screen(7.01).back() == "Loading....");
}

// Every line of the script fits the 39 columns as written: at every retro frame of the chapter the screen
// has exactly one row per line written, none wrapped, and the full screen just before the flash is the
// whole script.
void testLinesFit() {
    using Lines = std::vector<std::string>;
    const TimelineFrame frame = boot(1.0);
    const std::uint32_t flash = evo::retro::bootFlashFrame(frame);
    for (std::uint32_t retroFrame = 0; retroFrame < flash; ++retroFrame)
        for (const std::string& line : evo::retro::bootScreen(frame, retroFrame))
            CHECK(line.size() <= evo::retro::kBootColumns);
    CHECK(evo::retro::bootScreen(frame, flash - 1) ==
          (Lines{"C:\\>cd demo", "C:\\DEMO>evolution.exe", "", "EVOLUTION  1993 -> 2026",
                 "Sound Blaster 16: 220h IRQ5 DMA1 ... OK", "Gravis Ultrasound ... not found",
                 "VGA 320x200x256 ... OK", "Loading...."}));
}

// Where the flash goes: 4 retro frames before the chapter ends (2 white, then black into FLYBY), or at the
// accent if that came later. BOOT's accent (about 3 s, mid-script) comes earlier.
void testFlashFrame() {
    const TimelineFrame frame = boot(1.0);
    const std::uint32_t flash = evo::retro::bootFlashFrame(frame);
    std::printf("boot: duration %.4f s, accent %.3f s, flash at retro frame %u (%.3f s)\n",
                frame.chapterDuration, frame.accentSeconds, flash, flash / 35.0);
    CHECK(frame.accentSeconds < frame.chapterDuration - 4.0 / 35);
    CHECK(flash == static_cast<std::uint32_t>(std::floor((frame.chapterDuration - 4.0 / 35) * 35)));
    CHECK(flash == 247); // for the current show data: 7.057 s, chapter end 7.1731 s
    // Two white frames and at least one black one fit before the chapter ends.
    CHECK((flash + 3.0) / 35 < frame.chapterDuration);

    TimelineFrame late = frame;
    late.accentSeconds = frame.chapterDuration - 1.0 / 35;
    CHECK(evo::retro::bootFlashFrame(late) ==
          static_cast<std::uint32_t>(std::floor(late.accentSeconds * 35)));

    // The dots stop when the flash starts; from it on the screen is empty.
    CHECK(evo::retro::bootScreen(frame, flash - 1).back().starts_with("Loading"));
    CHECK(evo::retro::bootScreen(frame, flash).empty());
    CHECK(evo::retro::bootBackground(frame, flash - 1) == 0);
    CHECK(evo::retro::bootBackground(frame, flash) == 15);
    CHECK(evo::retro::bootBackground(frame, flash + 1) == 15);
    CHECK(evo::retro::bootBackground(frame, flash + 2) == 0);
    CHECK(evo::retro::bootText(frame, flash).empty() && evo::retro::bootText(frame, flash + 2).empty());
}

// The text, the background and the palette are functions of the retro frame: every time inside one gives
// the same result, and they read nothing of the frame but the chapter's fixed duration and accent. Covers
// frames of the script, the two flash frames and the black after them.
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
        CHECK(same(evo::retro::bootText(early, retroFrame), evo::retro::bootText(late, retroFrame)));
        CHECK(evo::retro::bootBackground(early, retroFrame) == evo::retro::bootBackground(late, retroFrame));
        evo::retro::Palette a{}, b{};
        b.fill(0x12345678u);
        evo::retro::bootPalette(a, early, retroFrame);
        evo::retro::bootPalette(b, late, retroFrame);
        CHECK(a == b);
    }
    // Grey 8x8 text at scale 1, from the 4-pixel margin, and the cursor as an underscore while it is on.
    const auto items = evo::retro::bootText(boot(0.1), 3);
    CHECK(items.size() == 2);
    CHECK(items[0].text == "C:\\>" && items[0].x == 4 && items[0].y == 4 && items[0].scale == 1);
    CHECK((items[0].rgba & 0xffu) == 7);
    CHECK(items[1].text == "_" && items[1].x == 4 + 4 * 8 && items[1].y == 4 && (items[1].rgba & 0xffu) == 7);
    // 2 Hz: on for the first quarter second of each half (judged at the retro frame's centre), off for the
    // second: on at 0.0-0.25 and 0.5-0.75 s, off at 0.25-0.5 and 0.75-1.0 s.
    CHECK(evo::retro::bootText(boot(0.3), 10).size() == 1);
    CHECK(evo::retro::bootText(boot(0.5), 17).size() == 2);
    CHECK(evo::retro::bootText(boot(0.7), 24).size() == 2);
    CHECK(evo::retro::bootText(boot(0.8), 28).size() == 1);
    CHECK(evo::retro::bootText(boot(1.0), 35).size() == 2);
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
    pattern.effect = Effect::Dots;
    CHECK(fixture.render(pattern).count(0) < kWidth * kHeight / 2);

    // t = 0.5: the prompt and the cursor only; at 0.3 the cursor is blinked off.
    const Indices prompt = fixture.render(boot(0.5));
    std::printf("boot: t=0.5 has %zu set pixels\n", prompt.nonZero());
    CHECK(prompt.nonZero() > 0 && prompt.nonZero() <= 70);
    CHECK(prompt.count(7) == prompt.nonZero());
    CHECK(matchesText(prompt, "C:\\>", 4, 4));
    for (int x = 0; x < 7; ++x)
        CHECK(prompt.at(4 + 32 + x, 4 + 7) == 7); // the cursor: row 7 of the fifth cell
    const Indices blinked = fixture.render(boot(0.3));
    CHECK(blinked.nonZero() + 7 == prompt.nonZero());
    CHECK(matchesText(blinked, "C:\\>", 4, 4));

    // t = 3.0: "evolution.exe" typed on the second line.
    const Indices typed = fixture.render(boot(3.0));
    CHECK(matchesText(typed, "C:\\>cd demo", 4, 4));
    CHECK(matchesText(typed, "C:\\DEMO>evolution.exe", 4, 12));

    // t = 5.0: the banner and the sound cards add lines.
    const Indices cards = fixture.render(boot(5.0));
    std::printf("boot: t=3.0 has %zu set pixels, t=5.0 %zu\n", typed.nonZero(), cards.nonZero());
    CHECK(cards.nonZero() > typed.nonZero());
    CHECK(matchesText(cards, "EVOLUTION  1993 -> 2026", 4, 28));

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
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testScript();
        testLinesFit();
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
