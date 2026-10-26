// PLASMA, chapter 3: the cycling palette on the CPU and the plasma's 320x200 palette index image (and its
// palette-mapped colours) from the retro stage on the GPU. Runs with the validation layer on and fails on any
// message it reports.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "render/retro_stage.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"
#include "text/text_pass.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::render::RetroStage;
using evo::retro::Palette;
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
constexpr int kPlasmaChapter = 3;

// A PLASMA frame at `local` seconds into the chapter.
TimelineFrame plasma(double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(kPlasmaChapter) + local);
    if (frame.chapter != kPlasmaChapter || frame.effect != Effect::Plasma)
        throw std::runtime_error("expected a PLASMA frame at local time " + std::to_string(local));
    return frame;
}

// A time `fraction` of the way through retro frame `retroFrame` of PLASMA.
double timeIn(std::uint32_t retroFrame, double fraction = .5) {
    return (retroFrame + fraction) / evo::retro::kPlasmaRetroRate;
}

Palette paletteAt(const TimelineFrame& frame, std::uint32_t retroFrame) {
    Palette palette{};
    evo::retro::plasmaPalette(palette, frame, retroFrame);
    return palette;
}

std::size_t differingEntries(const Palette& a, const Palette& b) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        count += a[i] != b[i];
    return count;
}

// The registry entry: the plasma shader, 35 Hz, the cycling palette and no text.
void testRegistry() {
    const evo::retro::EffectInfo& info = evo::retro::effectFor(Effect::Plasma);
    CHECK(info.shader != nullptr && std::string(info.shader) == "retro_plasma.comp.spv");
    CHECK(info.retroRate == 35);
    CHECK(info.palette == &evo::retro::plasmaPalette);
    CHECK(info.text == nullptr);
}

// The palette: black at 0, a fixed shadow ramp at 1..31, and at 32..255 three gradients that rotate by one
// entry per retro frame.
void testPalette() {
    using evo::retro::kPlasmaColours;
    using evo::retro::kPlasmaFirstIndex;
    CHECK(kPlasmaFirstIndex == 32 && kPlasmaColours == 224);

    const TimelineFrame frame = plasma(1.0);
    const Palette at10 = paletteAt(frame, 10);
    const Palette at11 = paletteAt(frame, 11);
    std::printf("plasma: palettes at retro frames 10 and 11 differ in %zu entries\n",
                differingEntries(at10, at11));
    CHECK(differingEntries(at10, at11) >= 200);

    for (const Palette& palette : {at10, at11}) {
        CHECK(palette[0] == evo::retro::rgba(0, 0, 0));
        for (std::uint32_t entry : palette)
            CHECK((entry >> 24) == 0xff); // opaque
    }
    // The shadow range does not cycle.
    for (std::size_t i = 0; i < kPlasmaFirstIndex; ++i)
        CHECK(at10[i] == at11[i]);
    // Cycling is a rotation of the 224 entries: what is at j + 1 now is at j one retro frame later.
    for (std::size_t j = 0; j < kPlasmaColours; ++j)
        CHECK(at11[kPlasmaFirstIndex + j] == at10[kPlasmaFirstIndex + (j + 1) % kPlasmaColours]);
    // Every neighbour differs, the wrap from 255 back to 32 included: a rotation by one changes them all.
    for (std::size_t j = 0; j < kPlasmaColours; ++j)
        CHECK(at10[kPlasmaFirstIndex + j] != at10[kPlasmaFirstIndex + (j + 1) % kPlasmaColours]);
    CHECK(differingEntries(at10, at11) == kPlasmaColours);
    // A full turn every 224 retro frames.
    CHECK(paletteAt(frame, 10 + kPlasmaColours) == at10);
    CHECK(paletteAt(frame, 0) == paletteAt(frame, kPlasmaColours));

    // Smooth: neighbouring entries are close in every channel (no hard edge anywhere on the ring).
    int largestStep = 0;
    for (std::size_t j = 0; j < kPlasmaColours; ++j) {
        const std::uint32_t a = at10[kPlasmaFirstIndex + j];
        const std::uint32_t b = at10[kPlasmaFirstIndex + (j + 1) % kPlasmaColours];
        for (int shift = 0; shift < 24; shift += 8)
            largestStep = std::max(largestStep, std::abs(int(a >> shift & 0xff) - int(b >> shift & 0xff)));
    }
    std::printf("plasma: largest channel step between neighbouring palette entries: %d\n", largestStep);
    CHECK(largestStep <= 8);

    // The builder reads nothing of the frame that changes within a retro frame.
    TimelineFrame noisy = plasma(timeIn(10, .99));
    noisy.kick = 1;
    noisy.energy = 1;
    noisy.beatPhase = .7f;
    noisy.showSeconds += 100;
    CHECK(paletteAt(noisy, 10) == at10);
}

// A 320x200 palette index image and its palette-mapped colours.
struct Picture {
    std::vector<std::uint8_t> indices; // row by row
    std::vector<std::uint8_t> rgb;     // row by row, 3 bytes a pixel
};

// A retro stage with its own ring and text pass; every render() submits, waits and reads both images back.
class Fixture {
public:
    explicit Fixture(evo::gfx::Context& ctx)
        : ring_(ctx), text_(ctx, ring_), stage_(ctx, ring_, text_),
          indexBuffer_(ctx, {VkDeviceSize{kWidth} * kHeight, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                             "plasma test index readback"}),
          colour_(ctx, kWidth, kHeight) {}

    Picture render(const TimelineFrame& frame, bool* rendered = nullptr) {
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
        vkCmdCopyImageToBuffer(cmd, index.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               indexBuffer_.handle(), 1, &region);
        evo::gfx::bufferBarrier2(cmd, indexBuffer_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                 VK_ACCESS_2_HOST_READ_BIT);
        colour_.record(cmd, stage_.image());
        ring_.wait(ring_.submit());
        indexBuffer_.invalidate();
        const auto* data = static_cast<const std::uint8_t*>(indexBuffer_.mapped());
        return {{data, data + indexBuffer_.size()}, colour_.rgb()};
    }

    RetroStage& stage() { return stage_; }

private:
    evo::gfx::FrameRing ring_;
    evo::text::TextPass text_;
    RetroStage stage_;
    evo::gfx::Buffer indexBuffer_;
    evo::gfx::Readback colour_;
};

std::size_t differingPixels(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
                            std::size_t bytesPerPixel) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size(); i += bytesPerPixel)
        count += std::memcmp(&a[i], &b[i], bytesPerPixel) != 0;
    return count;
}

void testImages(evo::gfx::Context& ctx) {
    Fixture fixture(ctx);

    // Retro frame 10: a rich index image, all of it in the cycling range.
    bool rendered = false;
    const Picture at10 = fixture.render(plasma(timeIn(10, .1)), &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == 10);
    const std::set<std::uint8_t> distinct(at10.indices.begin(), at10.indices.end());
    std::printf("plasma: retro frame 10 uses %zu distinct indices (%u..%u)\n", distinct.size(),
                unsigned(*distinct.begin()), unsigned(*distinct.rbegin()));
    CHECK(distinct.size() >= 64);
    CHECK(*distinct.begin() >= evo::retro::kPlasmaFirstIndex);

    // The colours are the palette of the retro frame looked up per pixel.
    const Palette palette10 = paletteAt(plasma(1.0), 10);
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < at10.indices.size(); ++i) {
        const std::uint32_t entry = palette10[at10.indices[i]];
        for (int c = 0; c < 3; ++c)
            mismatches += at10.rgb[i * 3 + c] != (entry >> (8 * c) & 0xff);
    }
    CHECK(mismatches == 0);

    // Retro frame 11 changes most pixels' colours, through the rotated palette and the moving field.
    const Picture at11 = fixture.render(plasma(timeIn(11)), &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == 11);
    const std::size_t changedColours = differingPixels(at10.rgb, at11.rgb, 3);
    std::printf("plasma: retro frames 10 -> 11 change %zu indices and %zu colours of %u pixels\n",
                differingPixels(at10.indices, at11.indices, 1), changedColours, kWidth * kHeight);
    CHECK(changedColours > kWidth * kHeight / 2);

    // The same retro frame at another time inside it, re-rendered after another frame: identical images.
    const Picture again = fixture.render(plasma(timeIn(10, .9)), &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == 10);
    CHECK(again.indices == at10.indices);
    CHECK(again.rgb == at10.rgb);

    // Within a retro frame nothing is re-rendered.
    fixture.render(plasma(timeIn(10, .95)), &rendered);
    CHECK(!rendered);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testPalette();
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
    std::printf("plasma_test: %d checks passed\n", checksPassed);
    return 0;
}
