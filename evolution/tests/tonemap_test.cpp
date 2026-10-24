// The ACES tonemap (Review Focus 6: one tonemap, display encoding exactly once): HDR RGBA16F ramps and colour
// patches uploaded from the CPU, tone mapped on the GPU into RGBA8 and compared with tonemapReference, the
// CPU mirror (within 1 per channel); black stays 0 and white 255 at every pixel whatever the dither; the ramp
// is monotonic once the dither is taken out; +1 EV equals the input doubled, byte for byte; the dither is
// static. Then the scene registry (every effect is the placeholder until the Studio arrives) and the modern
// stage's placeholder through the HDR path: its picture must match the old direct RGBA8 fill within 1 per
// channel. Runs with the validation layer on and fails on any message it reports.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/fill_pass.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "modern/modern_scene.hpp"
#include "post/tonemap.hpp"
#include "post/tonemap_reference.hpp"
#include "render/modern_stage.hpp"
#include "show/cues.hpp"
#include "show/timeline.hpp"

#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

using evo::gfx::Context;
using evo::gfx::FrameRing;
using evo::gfx::Image;

// --- Half floats -------------------------------------------------------------------------------------------

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    float magnitude;
    if (exponent == 0)
        magnitude = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31)
        magnitude =
            mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else
        magnitude = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -magnitude : magnitude;
}

// Round to nearest even; finite values inside the half range only.
std::uint16_t floatToHalf(float f) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
    const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    const float magnitude = std::fabs(f);
    if (magnitude < std::ldexp(1.0f, -14)) { // subnormal half: multiples of 2^-24
        const auto m = static_cast<std::uint32_t>(std::nearbyint(magnitude * 16777216.0f));
        return static_cast<std::uint16_t>(sign | m);
    }
    int exponent;
    std::frexp(magnitude, &exponent);                          // magnitude = m * 2^exponent, m in [0.5, 1)
    const float scaled = std::ldexp(magnitude, 11 - exponent); // in [1024, 2048)
    auto mantissa = static_cast<std::uint32_t>(std::nearbyint(scaled));
    int halfExponent = exponent - 1 + 15;
    if (mantissa == 2048) {
        mantissa = 1024;
        ++halfExponent;
    }
    return static_cast<std::uint16_t>(sign | (halfExponent << 10) | (mantissa & 0x3FFu));
}

// --- An HDR picture on the CPU, held as the half values the GPU sees
// -----------------------------------------

struct HdrPicture {
    std::uint32_t width = 0, height = 0;
    std::vector<float> rgb; // already rounded to half precision

    HdrPicture(std::uint32_t w, std::uint32_t h) : width(w), height(h), rgb(std::size_t{w} * h * 3, 0.f) {}
    void set(std::uint32_t x, std::uint32_t y, float r, float g, float b) {
        float* p = &rgb[(std::size_t{y} * width + x) * 3];
        p[0] = halfToFloat(floatToHalf(r));
        p[1] = halfToFloat(floatToHalf(g));
        p[2] = halfToFloat(floatToHalf(b));
    }
    const float* at(std::uint32_t x, std::uint32_t y) const { return &rgb[(std::size_t{y} * width + x) * 3]; }
    HdrPicture scaled(float factor) const {
        HdrPicture out(width, height);
        for (std::size_t i = 0; i < rgb.size(); ++i)
            out.rgb[i] = halfToFloat(floatToHalf(rgb[i] * factor));
        return out;
    }
};

constexpr std::uint32_t kWidth = 256, kHeight = 8;
constexpr float kRampTop = 64.f;

// The ramp value of column x: 0, then log spaced from 2^-12 to kRampTop.
float rampValue(std::uint32_t x) {
    if (x == 0)
        return 0.f;
    const float t = static_cast<float>(x - 1) / static_cast<float>(kWidth - 2);
    return std::exp2(-12.f + t * (std::log2(kRampTop) + 12.f));
}

// Rows 0-3: the ramp in grey, red, green and blue. Rows 4-7: the 8 primaries and secondaries (black, red,
// green, blue, cyan, magenta, yellow, white) at the intensities 0.05, 0.5, 2 and 16, 8 pixels per patch.
HdrPicture testPicture() {
    HdrPicture picture(kWidth, kHeight);
    for (std::uint32_t x = 0; x < kWidth; ++x) {
        const float v = rampValue(x);
        picture.set(x, 0, v, v, v);
        picture.set(x, 1, v, 0, 0);
        picture.set(x, 2, 0, v, 0);
        picture.set(x, 3, 0, 0, v);
    }
    constexpr std::array<std::array<float, 3>, 8> kColours = {
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1, 1}, {1, 0, 1}, {1, 1, 0}, {1, 1, 1}}};
    constexpr std::array<float, 4> kIntensities = {.05f, .5f, 2.f, 16.f};
    for (std::uint32_t y = 4; y < kHeight; ++y)
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const std::uint32_t patch = (x / 8) % 32;
            const auto& colour = kColours[patch % 8];
            const float intensity = kIntensities[patch / 8];
            picture.set(x, y, colour[0] * intensity, colour[1] * intensity, colour[2] * intensity);
        }
    return picture;
}

HdrPicture uniformPicture(float value) {
    HdrPicture picture(kWidth, kHeight);
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (std::uint32_t x = 0; x < kWidth; ++x)
            picture.set(x, y, value, value, value);
    return picture;
}

// --- The GPU side ------------------------------------------------------------------------------------------

class Gpu {
public:
    explicit Gpu(Context& ctx)
        : ctx_(ctx), hdr_(ctx, {kWidth, kHeight, VK_FORMAT_R16G16B16A16_SFLOAT,
                                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "test hdr"}),
          ldr_(ctx, {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "test ldr"}),
          staging_(ctx, {std::size_t{kWidth} * kHeight * 8, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true,
                         "test hdr upload"}),
          readback_(ctx, kWidth, kHeight), ring_(ctx), tonemap_(ctx, ring_) {}

    // Tone maps the picture at exposureEV; the RGB bytes.
    std::vector<std::uint8_t> run(const HdrPicture& picture, float exposureEV) {
        auto* halves = static_cast<std::uint16_t*>(staging_.mapped());
        for (std::size_t i = 0; i < std::size_t{kWidth} * kHeight; ++i) {
            for (int c = 0; c < 3; ++c)
                halves[i * 4 + c] = floatToHalf(picture.rgb[i * 3 + c]);
            halves[i * 4 + 3] = floatToHalf(1.f);
        }
        staging_.flush();

        VkCommandBuffer cmd = ring_.begin();
        hdr_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kWidth, kHeight, 1};
        vkCmdCopyBufferToImage(cmd, staging_.handle(), hdr_.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        tonemap_.record(cmd, hdr_, ldr_, exposureEV);
        readback_.record(cmd, ldr_);
        ring_.wait(ring_.submit());
        return readback_.rgb();
    }

    evo::post::Tonemap& tonemap() { return tonemap_; }
    FrameRing& ring() { return ring_; }

private:
    Context& ctx_;
    Image hdr_, ldr_;
    evo::gfx::Buffer staging_;
    evo::gfx::Readback readback_;
    FrameRing ring_; // after the images and buffers: its destructor waits for the submits that use them
    evo::post::Tonemap tonemap_;
};

std::uint8_t byteAt(const std::vector<std::uint8_t>& rgb, std::uint32_t x, std::uint32_t y, int c) {
    return rgb[(std::size_t{y} * kWidth + x) * 3 + c];
}

// (a) GPU bytes equal tonemapReference within 1 at every pixel and exposure, and off by one in at most 1 % of
// the channels.
void testMatchesReference(Gpu& gpu) {
    const HdrPicture picture = testPicture();
    for (const float exposure : {0.f, 1.5f, -2.f}) {
        const std::vector<std::uint8_t> rgb = gpu.run(picture, exposure);
        int worst = 0, off = 0;
        for (std::uint32_t y = 0; y < kHeight; ++y)
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const auto want = evo::post::tonemapReference(picture.at(x, y), exposure, x, y);
                for (int c = 0; c < 3; ++c) {
                    const int diff = std::abs(byteAt(rgb, x, y, c) - static_cast<int>(want[c]));
                    worst = std::max(worst, diff);
                    off += diff != 0 ? 1 : 0;
                }
            }
        std::printf("exposure %+.1f EV: GPU vs reference worst %d, %d of %u channels off by one\n", exposure,
                    worst, off, kWidth * kHeight * 3);
        CHECK(worst <= 1);
        // Rounding may differ in a few places, not systematically (a lost dither would put about a quarter of
        // the channels off by one).
        CHECK(off * 100 <= static_cast<int>(kWidth * kHeight * 3));
    }
}

// (b) 0 -> 0 and white -> 255 at every pixel, whatever the dither; the reference agrees.
void testBlackAndWhite(Gpu& gpu) {
    const std::vector<std::uint8_t> black = gpu.run(uniformPicture(0.f), 0.f);
    CHECK(std::all_of(black.begin(), black.end(), [](std::uint8_t v) { return v == 0; }));
    const std::vector<std::uint8_t> white = gpu.run(uniformPicture(kRampTop), 0.f);
    CHECK(std::all_of(white.begin(), white.end(), [](std::uint8_t v) { return v == 255; }));
    const float zero[3] = {0, 0, 0}, top[3] = {kRampTop, kRampTop, kRampTop};
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) {
            CHECK(evo::post::tonemapReference(zero, 0.f, x, y) == (std::array<std::uint8_t, 3>{0, 0, 0}));
            CHECK(evo::post::tonemapReference(top, 0.f, x, y) ==
                  (std::array<std::uint8_t, 3>{255, 255, 255}));
        }
}

// (c) Along each ramp row, per channel, the display value never falls: the reference's undithered value
// strictly, and the GPU bytes once their pixel's dither is taken out (within the 1-step quantisation).
void testMonotonic(Gpu& gpu) {
    const HdrPicture picture = testPicture();
    const std::vector<std::uint8_t> rgb = gpu.run(picture, 0.f);
    for (std::uint32_t row = 0; row < 4; ++row)
        for (int c = 0; c < 3; ++c) {
            float previousDisplay = -1.f, previousBytes = -2.f;
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const float display = evo::post::tonemapDisplay(picture.at(x, row), 0.f)[c];
                CHECK(display >= previousDisplay);
                previousDisplay = display;
                const float undithered = byteAt(rgb, x, row, c) - evo::post::tonemapDither(x, row);
                CHECK(undithered >= previousBytes - 1.f);
                previousBytes = std::max(previousBytes, undithered);
            }
        }
    // The dither is a triangular offset strictly inside (-0.5, 0.5), centred on 0.
    double sum = 0;
    for (std::uint32_t y = 0; y < 256; ++y)
        for (std::uint32_t x = 0; x < 256; ++x) {
            const float d = evo::post::tonemapDither(x, y);
            CHECK(d > -.5f && d < .5f);
            sum += d;
        }
    CHECK(std::fabs(sum / 65536.0) < .01);
}

// (d) +1 EV equals the input doubled, byte for byte (doubling a half is exact); the dither is static: the
// same picture twice gives the same bytes.
void testExposure(Gpu& gpu) {
    const HdrPicture picture = testPicture();
    const std::vector<std::uint8_t> plusOne = gpu.run(picture, 1.f);
    const std::vector<std::uint8_t> doubled = gpu.run(picture.scaled(2.f), 0.f);
    const std::vector<std::uint8_t> plain = gpu.run(picture, 0.f);
    CHECK(plusOne == doubled);
    CHECK(plusOne != plain);
    CHECK(gpu.run(picture, 0.f) == plain);
}

// (e) The record() contract: RGBA16F in, RGBA8 UNORM out, the same size.
void testRejectsWrongImages(Context& ctx, Gpu& gpu) {
    Image small(ctx, {kWidth / 2, kHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, 1, "small"});
    Image hdr(ctx, {kWidth, kHeight, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, 1, "hdr"});
    Image ldr(ctx, {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, 1, "ldr"});
    VkCommandBuffer cmd = gpu.ring().begin();
    int thrown = 0;
    const auto attempt = [&](Image& in, Image& out) {
        try {
            gpu.tonemap().record(cmd, in, out, 0.f);
        } catch (const std::invalid_argument&) {
            ++thrown;
        }
    };
    attempt(hdr, small); // another size
    attempt(ldr, ldr);   // an RGBA8 HDR image
    attempt(hdr, hdr);   // an RGBA16F LDR image
    gpu.ring().wait(gpu.ring().submit());
    CHECK(thrown == 3);
}

// (f) The scene registry: until the Studio (Task 10), every effect renders the placeholder.
void testSceneRegistry() {
    for (std::size_t effect = 0; effect < evo::show::kEffectCount; ++effect)
        CHECK(evo::modern::sceneFor(static_cast<evo::show::Effect>(effect)) ==
              evo::modern::SceneId::Placeholder);
}

// (g) The placeholder through the HDR path looks as it did as a direct RGBA8 fill: within 1 per channel (the
// dither) at every pixel, for frames across the 2026 chapters; the stage owns an RGBA16F HDR image.
void testPlaceholderUnchanged(Context& ctx) {
    constexpr std::uint32_t kW = 160, kH = 90;
    Image direct(ctx, {kW, kH, VK_FORMAT_R8G8B8A8_UNORM,
                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "direct fill"});
    evo::gfx::Readback stageReadback(ctx, kW, kH), directReadback(ctx, kW, kH);
    FrameRing ring(ctx);
    evo::gfx::FillPass fill(ctx);
    evo::render::ModernStage stage(ctx, ring, kW, kH);
    CHECK(stage.hdr().format() == VK_FORMAT_R16G16B16A16_SFLOAT);
    CHECK(stage.image().format() == VK_FORMAT_R8G8B8A8_UNORM);
    {
        // Each fill build writes one format: the HDR build refuses the RGBA8 image, recording nothing.
        evo::gfx::FillPass hdrFill(ctx, evo::gfx::FillTarget::Hdr);
        bool refused = false;
        VkCommandBuffer cmd = ring.begin();
        try {
            hdrFill.record(cmd, ring, direct, {});
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        ring.wait(ring.submit());
        CHECK(refused);
    }

    int worst = 0, frames = 0;
    for (const int chapter : {11, 12, 14, 17, 19, 21, 27})
        for (const double fraction : {.1, .45, .8}) {
            const double t =
                evo::show::chapterStart(chapter) +
                fraction * (evo::show::chapterStart(chapter + 1) - evo::show::chapterStart(chapter));
            const evo::show::TimelineFrame frame = evo::show::sampleTimeline(t);
            evo::gfx::FillParams params; // what the stage drew directly before the HDR path
            params.localSeconds = static_cast<float>(frame.localSeconds);
            params.era = 1.f;
            params.kick = frame.kick;
            params.beatPhase = frame.beatPhase;
            params.chapter = static_cast<std::uint32_t>(frame.chapter);

            VkCommandBuffer cmd = ring.begin();
            stage.render(cmd, frame, {evo::render::HistoryReset::None, 1});
            stageReadback.record(cmd, stage.image());
            fill.record(cmd, ring, direct, params);
            directReadback.record(cmd, direct);
            ring.wait(ring.submit());

            const std::vector<std::uint8_t> now = stageReadback.rgb(), before = directReadback.rgb();
            for (std::size_t i = 0; i < now.size(); ++i)
                worst = std::max(worst, std::abs(now[i] - before[i]));
            ++frames;
        }
    std::printf("placeholder through HDR + tonemap vs the direct fill: %d frames, worst %d\n", frames, worst);
    CHECK(worst <= 1);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        testSceneRegistry();
        {
            auto ctx = Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            {
                Gpu gpu(ctx);
                testMatchesReference(gpu);
                testBlackAndWhite(gpu);
                testMonotonic(gpu);
                testExposure(gpu);
                testRejectsWrongImages(ctx, gpu);
            }
            testPlaceholderUnchanged(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("tonemap_test: %d checks passed\n", checksPassed);
    return 0;
}
