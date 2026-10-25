// The tonemap, Khronos PBR Neutral (one tonemap, display encoding exactly once): HDR RGBA16F
// ramps and colour patches uploaded from the CPU, tone mapped on the GPU into RGBA8 and compared with
// tonemapReference, the CPU mirror (within 1 per channel); black stays 0 at every pixel whatever the dither
// and white 255 once it is bright enough for the curve's asymptote (64 rounds to 255, 16384 is 255 at every
// pixel); the ramp is monotonic once the dither is taken out; +1 EV equals the input doubled, +2 EV
// quadrupled and -1 EV halved, byte for byte; the dither is static; golden anchors computed independently of
// this code (mid grey, white, black, saturated primaries, both offset branches, the compression knee, the CRT
// red, the sRGB OETF) hold on the CPU and the GPU; the hue is kept (exactly below the knee, in the plane of
// the input and white above it); negative light counts as 0 and non-finite light is black. Then the scene
// registry and the modern stage's placeholder through the HDR path: its picture must match the old direct
// RGBA8 fill within 1 per channel wherever the curve can reach the fill's colour. Runs with the validation
// layer on and fails on any message it reports.

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

// Round to nearest even; finite values inside the half range, infinities and NaN (a quiet NaN).
std::uint16_t floatToHalf(float f) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
    const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    if (std::isnan(f))
        return static_cast<std::uint16_t>(sign | 0x7E00u);
    if (std::isinf(f))
        return static_cast<std::uint16_t>(sign | 0x7C00u);
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
// ----------------------------------------

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
        : hdr_(ctx, {kWidth, kHeight, VK_FORMAT_R16G16B16A16_SFLOAT,
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
    Image hdr_, ldr_;
    evo::gfx::Buffer staging_;
    evo::gfx::Readback readback_;
    FrameRing ring_; // after the images and buffers: its destructor waits for the submits that use them
    evo::post::Tonemap tonemap_;
};

std::uint8_t byteAt(const std::vector<std::uint8_t>& rgb, std::uint32_t x, std::uint32_t y, int c) {
    return rgb[(std::size_t{y} * kWidth + x) * 3 + c];
}

// (a) GPU bytes equal tonemapReference within 1 at every pixel and exposure, and off by one in at most 0.3 %
// of the channels.
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
        CHECK(off * 1000 <= 3 * static_cast<int>(kWidth * kHeight * 3));
    }
}

// (b) 0 -> 0 at every pixel, whatever the dither. White approaches 1 asymptotically under PBR Neutral (64 ->
// 0.99909 linear, 254.90 encoded): white x 64 rounds to 255 without the dither and is 255 or 254 with it (254
// only where the dither is below -0.398, about 2 % of the pixels); white x 16384 (0.9999965 linear) is 255 at
// every pixel. The reference agrees.
void testBlackAndWhite(Gpu& gpu) {
    const std::vector<std::uint8_t> black = gpu.run(uniformPicture(0.f), 0.f);
    CHECK(std::all_of(black.begin(), black.end(), [](std::uint8_t v) { return v == 0; }));
    constexpr float kBrightWhite = 16384.f;
    const std::vector<std::uint8_t> white = gpu.run(uniformPicture(kBrightWhite), 0.f);
    CHECK(std::all_of(white.begin(), white.end(), [](std::uint8_t v) { return v == 255; }));
    const std::vector<std::uint8_t> top = gpu.run(uniformPicture(kRampTop), 0.f);
    CHECK(std::all_of(top.begin(), top.end(), [](std::uint8_t v) { return v >= 254; }));
    const auto full = std::count(top.begin(), top.end(), std::uint8_t{255});
    std::printf("white x %.0f: %td of %zu channels 255\n", kRampTop, full, top.size());
    CHECK(full * 100 >= static_cast<std::ptrdiff_t>(top.size()) * 95);

    const float zero[3] = {0, 0, 0}, white64[3] = {kRampTop, kRampTop, kRampTop},
                bright[3] = {kBrightWhite, kBrightWhite, kBrightWhite};
    CHECK(std::floor(evo::post::tonemapDisplay(white64, 0.f)[0] * 255.f + .5f) == 255.f);
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) {
            CHECK(evo::post::tonemapReference(zero, 0.f, x, y) == (std::array<std::uint8_t, 3>{0, 0, 0}));
            CHECK(evo::post::tonemapReference(bright, 0.f, x, y) ==
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

// (d) +1 EV equals the input doubled, +2 EV the input times 4 and -1 EV the input halved, byte for byte
// (scaling a half by a power of two is exact while it stays normal, as the test picture does); the dither is
// static: the same picture twice gives the same bytes.
void testExposure(Gpu& gpu) {
    const HdrPicture picture = testPicture();
    const std::vector<std::uint8_t> plusOne = gpu.run(picture, 1.f);
    const std::vector<std::uint8_t> doubled = gpu.run(picture.scaled(2.f), 0.f);
    const std::vector<std::uint8_t> plain = gpu.run(picture, 0.f);
    CHECK(plusOne == doubled);
    CHECK(plusOne != plain);
    CHECK(gpu.run(picture, 0.f) == plain);
    CHECK(gpu.run(picture, 2.f) == gpu.run(picture.scaled(4.f), 0.f));
    CHECK(gpu.run(picture, -1.f) == gpu.run(picture.scaled(.5f), 0.f));
}

// (h) Golden anchors, independent of this code: computed in float64 with numpy (golden.py) from the Khronos PBR Neutral specification (KhronosGroup/ToneMapping, PBR_Neutral/README.md: F90 =
// 0.04, Ks = 0.8 - F90, Kd = 0.15, the offset f = x - x^2 / (4 F90) for x <= 2 F90) and the sRGB OETF,
// rounded to bytes without dither. The reference's linear and display values must be within 1e-4, the GPU's
// dithered bytes (of the half-rounded inputs) within 1 of the literal byte, at every pixel.
struct Golden {
    std::array<float, 3> hdr;
    std::array<float, 3> linear;  // PBR Neutral's output
    std::array<float, 3> display; // its sRGB encoding
    std::array<int, 3> bytes;
};
constexpr std::array<Golden, 11> kGolden = {{
    // Mid grey: the offset's constant branch (0.18 - 0.04), below the knee.
    {{.18f, .18f, .18f},
     {.14000000f, .14000000f, .14000000f},
     {.41002090f, .41002090f, .41002090f},
     {105, 105, 105}},
    {{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {0, 0, 0}},
    // White: the asymptote.
    {{64.f, 64.f, 64.f},
     {.99909206f, .99909206f, .99909206f},
     {.99960078f, .99960078f, .99960078f},
     {255, 255, 255}},
    {{16384.f, 16384.f, 16384.f},
     {.99999648f, .99999648f, .99999648f},
     {.99999845f, .99999845f, .99999845f},
     {255, 255, 255}},
    // Saturated primaries: compressed, then desaturated towards the new peak in the plane of the input and
    // white.
    {{2.f, 0.f, 0.f},
     {.96108108f, .12957942f, .12957942f},
     {.98269360f, .39527290f, .39527290f},
     {251, 101, 101}},
    {{0.f, 2.f, 0.f},
     {.12957942f, .96108108f, .12957942f},
     {.39527290f, .98269360f, .39527290f},
     {101, 251, 101}},
    {{0.f, 0.f, 2.f},
     {.12957942f, .12957942f, .96108108f},
     {.39527290f, .39527290f, .98269360f},
     {101, 101, 251}},
    // The offset's quadratic branch (min 0.05 < 0.08), below the knee.
    {{.5f, .2f, .05f},
     {.46562500f, .16562500f, .01562500f},
     {.71224532f, .44375613f, .13149941f},
     {182, 113, 34}},
    // Above the knee with a non-zero minimum.
    {{1.5f, .6f, .3f},
     {.93872340f, .40202365f, .22312373f},
     {.97256620f, .66670097f, .50969402f},
     {248, 170, 130}},
    // The knee itself: 0.8 - 0.04 = Ks, where both branches meet.
    {{.8f, .8f, .8f},
     {.76000000f, .76000000f, .76000000f},
     {.88600397f, .88600397f, .88600397f},
     {226, 226, 226}},
    // The CRT red (0.8, 0.1, 0.1) at the Studio's white level 6.8 (see testCrtRed).
    {{5.44f, .68f, .68f},
     {.98819672f, .46401055f, .46401055f},
     {.99479350f, .71113576f, .71113576f},
     {254, 181, 181}},
}};

void testGoldenAnchors(Gpu& gpu) {
    // The sRGB OETF on its own: the knee and mid grey.
    CHECK(std::fabs(evo::post::srgbOetf(.0031308f) - .04045f) < 1e-4f);
    CHECK(std::fabs(evo::post::srgbOetf(.5f) - .7354f) < 1e-4f);
    CHECK(evo::post::srgbOetf(0.f) == 0.f && evo::post::srgbOetf(1.f) == 1.f);

    HdrPicture picture(kWidth, kHeight);
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const auto& hdr = kGolden[x % kGolden.size()].hdr;
            picture.set(x, y, hdr[0], hdr[1], hdr[2]);
        }
    const std::vector<std::uint8_t> rgb = gpu.run(picture, 0.f);
    for (const Golden& golden : kGolden) {
        const std::array<float, 3> linear = evo::post::pbrNeutral(golden.hdr);
        const std::array<float, 3> display = evo::post::tonemapDisplay(golden.hdr.data(), 0.f);
        for (int c = 0; c < 3; ++c) {
            CHECK(std::fabs(linear[c] - golden.linear[c]) < 1e-4f);
            CHECK(std::fabs(display[c] - golden.display[c]) < 1e-4f);
        }
    }
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (std::uint32_t x = 0; x < kWidth; ++x)
            for (int c = 0; c < 3; ++c)
                CHECK(std::abs(byteAt(rgb, x, y, c) - kGolden[x % kGolden.size()].bytes[c]) <= 1);
}

// The HSV hue of linear RGB in degrees, [0, 360); -1 for a grey (no hue).
double hsvHue(const std::array<double, 3>& c) {
    const double high = std::max({c[0], c[1], c[2]}), low = std::min({c[0], c[1], c[2]});
    const double chroma = high - low;
    if (chroma <= 1e-12 * std::max(high, 1.0))
        return -1.0;
    double h;
    if (high == c[0])
        h = std::fmod((c[1] - c[2]) / chroma + 6.0, 6.0);
    else if (high == c[1])
        h = (c[2] - c[0]) / chroma + 2.0;
    else
        h = (c[0] - c[1]) / chroma + 4.0;
    return 60.0 * h;
}

double hueDistance(double a, double b) {
    const double d = std::fabs(a - b);
    return std::min(d, 360.0 - d);
}

// (j) The hue is kept. Over a sweep of colours (12 hues x 6 saturations x 18 intensities, 2^-8 .. 2^9): where
// the peak after the offset is below the knee (0.76), the output's HSV hue equals the input's within 1
// degree; above it the output stays in the plane spanned by the input and white (the normalised determinant
// [input, output, white] below 1e-4) on the input's side of the grey axis, so its hue is the input's within 1
// degree too while it has any chroma left (where the desaturation has not turned it grey). PBR Neutral on its
// own, from the reference (the GPU equals it within 1 per channel, (a)).
void testHueKept() {
    int below = 0, above = 0;
    for (int hueStep = 0; hueStep < 12; ++hueStep)
        for (const double saturation : {.05, .2, .5, .8, .95, 1.0})
            for (int stop = 0; stop < 18; ++stop) {
                const double value = std::exp2(-8.0 + stop);
                // HSV -> RGB at hue 30 * hueStep.
                const double h = hueStep / 2.0, f = h - std::floor(h);
                const double p = value * (1 - saturation), q = value * (1 - saturation * f),
                             t = value * (1 - saturation * (1 - f));
                std::array<double, 3> in{};
                switch (static_cast<int>(std::floor(h)) % 6) {
                case 0:
                    in = {value, t, p};
                    break;
                case 1:
                    in = {q, value, p};
                    break;
                case 2:
                    in = {p, value, t};
                    break;
                case 3:
                    in = {p, q, value};
                    break;
                case 4:
                    in = {t, p, value};
                    break;
                default:
                    in = {value, p, q};
                    break;
                }
                const std::array<float, 3> inF = {static_cast<float>(in[0]), static_cast<float>(in[1]),
                                                  static_cast<float>(in[2])};
                const std::array<float, 3> outF = evo::post::pbrNeutral(inF);
                const std::array<double, 3> inD = {inF[0], inF[1], inF[2]}, out = {outF[0], outF[1], outF[2]};
                for (const double v : out)
                    CHECK(v >= 0.0 && v <= 1.0);
                const double low = std::min({inD[0], inD[1], inD[2]});
                const double offset = low < .08 ? low - 6.25 * low * low : .04;
                const double peak = std::max({inD[0], inD[1], inD[2]}) - offset;
                const double inHue = hsvHue(inD), outHue = hsvHue(out);
                if (peak < .76) {
                    ++below;
                    CHECK((inHue < 0) == (outHue < 0));
                    if (inHue >= 0)
                        CHECK(hueDistance(inHue, outHue) <= 1.0);
                } else {
                    ++above;
                    // det [in, out, (1, 1, 1)] / (|in| |out| sqrt 3): 0 when out lies in the plane of in and
                    // white.
                    const double det =
                        inD[0] * (out[1] - out[2]) - inD[1] * (out[0] - out[2]) + inD[2] * (out[0] - out[1]);
                    const double norm = std::hypot(inD[0], inD[1], inD[2]) *
                                        std::hypot(out[0], out[1], out[2]) * std::sqrt(3.0);
                    CHECK(std::fabs(det) / norm < 1e-4);
                    const double outChroma =
                        std::max({out[0], out[1], out[2]}) - std::min({out[0], out[1], out[2]});
                    if (inHue >= 0 && outChroma > 1e-3)
                        CHECK(hueDistance(inHue, outHue) <= 1.0);
                }
            }
    std::printf("hue kept: %d colours below the knee, %d above\n", below, above);
    CHECK(below > 100 && above > 100);
}

// (k) The CRT red. (0.8, 0.1, 0.1) is a saturated 1993 red on the Studio's screen, and 6.8 the screen's white
// level before the tonemap (emission 1.2 x 2^2.5 EV). Under ACES (step 4) it came out as (255, 204, 195): the
// green byte 9 above the blue, a hue shift of 8 degrees towards orange, as well as washed out. Under PBR
// Neutral green and blue stay equal at every pixel (the hue stays exactly red) and the red leads green by at
// least 70 (golden: 254, 181, 181). An earlier draft asked for r >= 2 g here: PBR Neutral's desaturation (Kd = 0.15)
// cannot give that at 6.8 (r >= 2 g in bytes holds up to 2.4x, under ACES up to 2.1x), so the test pins the
// hue and the margin over ACES's washed-out green instead.
void testCrtRed(Gpu& gpu) {
    const float crt[3] = {.8f * 6.8f, .1f * 6.8f, .1f * 6.8f};
    HdrPicture picture(kWidth, kHeight);
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (std::uint32_t x = 0; x < kWidth; ++x)
            picture.set(x, y, crt[0], crt[1], crt[2]);
    const std::vector<std::uint8_t> rgb = gpu.run(picture, 0.f);
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const int r = byteAt(rgb, x, y, 0), g = byteAt(rgb, x, y, 1), b = byteAt(rgb, x, y, 2);
            CHECK(g == b);
            CHECK(r - g >= 70);
            CHECK(g < 204); // less washed out than ACES's 204
        }
    const std::array<float, 3> linear = evo::post::pbrNeutral({crt[0], crt[1], crt[2]});
    CHECK(hsvHue({linear[0], linear[1], linear[2]}) < 1e-3);
}

// (i) Light the tonemap cannot show is black, never a bright speckle: negative light (each component clamped
// to 0 before the curve, which is defined on non-negative light) and NaN or infinite components, on the GPU
// and in the reference. A negative component next to positive ones counts as 0.
void testNegativeAndNonFinite(Gpu& gpu) {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<std::array<float, 3>, 6> kBlack = {{
        {-1.f, -1.f, -1.f},
        {-64.f, -.5f, -.01f},
        {nan, nan, nan},
        {inf, inf, inf},
        {-inf, -inf, -inf},
        {nan, inf, -inf},
    }};
    HdrPicture picture(kWidth, kHeight);
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const auto& hdr = kBlack[x % kBlack.size()];
            picture.set(x, y, hdr[0], hdr[1], hdr[2]);
        }
    for (const float exposure : {0.f, 3.f}) {
        const std::vector<std::uint8_t> rgb = gpu.run(picture, exposure);
        CHECK(std::all_of(rgb.begin(), rgb.end(), [](std::uint8_t v) { return v == 0; }));
        for (const auto& hdr : kBlack)
            for (std::uint32_t x = 0; x < 16; ++x)
                CHECK(evo::post::tonemapReference(hdr.data(), exposure, x, 3) ==
                      (std::array<std::uint8_t, 3>{0, 0, 0}));
    }

    // Mixed: (0.5, -3, 0.2) is (0.5, 0, 0.2), and (-1, 2, NaN) is (0, 2, 0), on the GPU and in the reference.
    HdrPicture mixed(kWidth, kHeight), clamped(kWidth, kHeight);
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            if (x % 2 == 0) {
                mixed.set(x, y, .5f, -3.f, .2f);
                clamped.set(x, y, .5f, 0.f, .2f);
            } else {
                mixed.set(x, y, -1.f, 2.f, nan);
                clamped.set(x, y, 0.f, 2.f, 0.f);
            }
        }
    CHECK(gpu.run(mixed, 0.f) == gpu.run(clamped, 0.f));
    for (std::uint32_t x = 0; x < 16; ++x)
        CHECK(evo::post::tonemapReference(mixed.at(x, 0), 0.f, x, 0) ==
              evo::post::tonemapReference(clamped.at(x, 0), 0.f, x, 0));
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

// (f) The scene registry: FINALE, PORTAL, STUDIO NIGHT, RETURN and REBOOT show the Studio (Task 10), every
// other effect the placeholder (studio_test pins the mapping in full).
void testSceneRegistry() {
    using evo::show::Effect;
    for (std::size_t effect = 0; effect < evo::show::kEffectCount; ++effect) {
        const auto e = static_cast<Effect>(effect);
        const bool studio = e == Effect::Finale || e == Effect::Portal || e == Effect::StudioNight ||
                            e == Effect::Return || e == Effect::Reboot;
        CHECK(evo::modern::sceneFor(e) ==
              (studio ? evo::modern::SceneId::Studio : evo::modern::SceneId::Placeholder));
    }
}

// (g) The placeholder through the HDR path looks as it did as a direct RGBA8 fill, for frames across the 2026
// chapters that show it; the stage owns an RGBA16F HDR image. Within 1 per channel (the dither) at every
// pixel whose brightest channel is at most 226 (0.76 linear, PBR Neutral's knee: every colour below it is the
// image of some light, which fill_hdr.comp finds exactly). Above the knee the curve's desaturation cannot
// reach a bright colour with a low minimum (the top of the placeholder's glow); fill_hdr.comp gives it the
// nearest colour the curve can show with the same hue, a little darker: within 4 per channel, and at most 1 %
// of the channels differ by more than 1.
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

    int worst = 0, worstBelowKnee = 0, off = 0, channels = 0, frames = 0;
    for (const int chapter : {12, 13, 14, 17, 18, 21, 27})
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
            for (std::size_t pixel = 0; pixel < now.size() / 3; ++pixel) {
                const std::uint8_t* direct = &before[pixel * 3];
                const bool belowKnee = std::max({direct[0], direct[1], direct[2]}) <= 226;
                for (std::size_t c = pixel * 3; c < pixel * 3 + 3; ++c) {
                    const int diff = std::abs(now[c] - before[c]);
                    worst = std::max(worst, diff);
                    if (belowKnee)
                        worstBelowKnee = std::max(worstBelowKnee, diff);
                    off += diff > 1 ? 1 : 0;
                    ++channels;
                }
            }
            ++frames;
        }
    std::printf(
        "placeholder through HDR + tonemap vs the direct fill: %d frames, worst %d (below the knee %d), "
        "%d of %d channels off by more than 1\n",
        frames, worst, worstBelowKnee, off, channels);
    CHECK(worstBelowKnee <= 1);
    CHECK(worst <= 4);
    CHECK(off * 100 <= channels);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        testSceneRegistry();
        testHueKept();
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
                testGoldenAnchors(gpu);
                testCrtRed(gpu);
                testNegativeAndNonFinite(gpu);
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
