#include "gfx/context.hpp"
#include "gfx/offscreen.hpp"
#include "gfx/shader.hpp"
#include "image/png.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

// The host-side twin of fill.comp's UNORM store: round to nearest.
int unorm8(float value) {
    return static_cast<int>(value * 255.0f + 0.5f);
}

bool within1(int actual, int expected) {
    return std::abs(actual - expected) <= 1;
}

// The host-side twin of fill.comp's chapterHue(): a cosine palette indexed by chapter / 31.
float chapterHue(int chapter, int channel) {
    const float hue = static_cast<float>(chapter) / 31.0f + static_cast<float>(channel) / 3.0f;
    return 0.5f + 0.5f * std::cos(6.2831853f * hue);
}

// The distinct RGB colours in columns [x0, x1) of a frame.
std::size_t distinctColours(const evo::gfx::Frame& frame, int x0, int x1) {
    std::set<std::uint32_t> colours;
    for (int y = 0; y < frame.height; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::uint8_t* p = &frame.rgb[(static_cast<std::size_t>(y) * frame.width + x) * 3];
            colours.insert(std::uint32_t(p[0]) << 16 | std::uint32_t(p[1]) << 8 | p[2]);
        }
    }
    return colours.size();
}

// Points shaderDir() at the build's compiled shaders before any Offscreen loads fill.comp.spv.
void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void testRendersAndReadsBack(evo::gfx::Context& ctx) {
    evo::gfx::Offscreen off(ctx, 64, 64);
    CHECK(off.width() == 64 && off.height() == 64);

    off.render(0.0);
    CHECK(off.layout() == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    CHECK(off.image() != VK_NULL_HANDLE);
    auto a = off.readback();
    CHECK(a.width == 64 && a.height == 64);
    CHECK(a.rgb.size() == 64u * 64u * 3u);
    CHECK(std::any_of(a.rgb.begin(), a.rgb.end(), [](std::uint8_t byte) { return byte > 0; }));

    off.render(1.0);
    auto b = off.readback();
    CHECK(a.rgb != b.rgb); // the circle moved

    off.render(0.5);
    auto c = off.readback();
    off.render(0.5);
    auto d = off.readback();
    CHECK(c.rgb == d.rgb); // same time, same frame

    const auto path = std::string(EVO_TEST_OUT_DIR) + "/offscreen/t0.png";
    evo::image::writePng(path, a.width, a.height, a.rgb.data());
    std::printf("wrote %s\n", path.c_str());
}

// Odd sizes exercise the partial 8x8 workgroups at the right and bottom edges.
void testOddSizeEdgePixel(evo::gfx::Context& ctx) {
    evo::gfx::Offscreen odd(ctx, 33, 17);
    evo::show::TimelineFrame frame; // chapter 0, Cut, era 0: the 1993 look, the circle at rest
    odd.render(frame);
    auto image = odd.readback();
    CHECK(image.rgb.size() == 33u * 17u * 3u);

    // Pixel (32,16) sits on the diagonal band 3 ((32.5/33 + 16.5/17) / 2 * 4 = 3.9) in checker cell (1,0),
    // an odd cell, which inverts it to band 0: a quarter of chapter 0's hue. The circle at t=0 is centred
    // at (16.5,13.6) with radius 3.4 and does not reach it.
    const int x = 32, y = 16;
    const std::uint8_t* pixel = &image.rgb[(static_cast<std::size_t>(y) * 33 + x) * 3];
    const int red = unorm8(chapterHue(0, 0) * 0.25f);
    const int green = unorm8(chapterHue(0, 1) * 0.25f);
    const int blue = unorm8(chapterHue(0, 2) * 0.25f);
    std::printf("pixel(32,16) = %d,%d,%d expected %d,%d,%d\n", pixel[0], pixel[1], pixel[2], red, green,
                blue);
    CHECK(within1(pixel[0], red));
    CHECK(within1(pixel[1], green));
    CHECK(within1(pixel[2], blue));
}

// The timeline drives the pattern: another chapter at the same local time looks different, and the same
// frame always renders the same pixels.
void testTimelineFrames(evo::gfx::Context& ctx) {
    evo::gfx::Offscreen off(ctx, 64, 64);

    const auto boot = evo::show::sampleTimeline(0.5);
    const auto raytrace = evo::show::sampleTimeline(evo::show::chapterStart(7) + 0.5);
    CHECK(boot.effect == evo::show::Effect::Boot);
    CHECK(raytrace.effect == evo::show::Effect::Raytrace);

    off.render(boot);
    auto a = off.readback();
    off.render(raytrace);
    auto b = off.readback();
    CHECK(a.rgb != b.rgb);

    off.render(raytrace);
    auto c = off.readback();
    CHECK(b.rgb == c.rgb);

    // render(double) is render(sampleTimeline(seconds)).
    off.render(evo::show::chapterStart(7) + 0.5);
    auto d = off.readback();
    CHECK(b.rgb == d.rgb);
}

// A Wipe half way through: the 2026 smooth gradient on the left, the 4-colour 1993 checker on the right.
void testWipeSplitsTheFrame(evo::gfx::Context& ctx) {
    constexpr int kWidth = 128, kHeight = 64;
    evo::gfx::Offscreen off(ctx, kWidth, kHeight);

    evo::show::TimelineFrame frame;
    frame.chapter = 3;
    frame.transition = evo::show::Transition::Wipe;
    frame.wipe = .5f;
    frame.era = .5f;
    off.render(frame);
    auto image = off.readback();

    // Compare every row's left half with its right half, byte for byte.
    constexpr int kHalf = kWidth / 2 * 3;
    bool halvesDiffer = false;
    for (int y = 0; y < kHeight; ++y) {
        const std::uint8_t* row = &image.rgb[static_cast<std::size_t>(y) * kWidth * 3];
        halvesDiffer |= !std::equal(row, row + kHalf, row + kHalf);
    }
    CHECK(halvesDiffer);

    // Left of the wipe line (columns 0-61): a gradient with many shades. Right of it (66-127): the four
    // palette colours plus the white circle.
    const std::size_t left = distinctColours(image, 0, kWidth / 2 - 2);
    const std::size_t right = distinctColours(image, kWidth / 2 + 2, kWidth);
    std::printf("wipe .5: %zu colours left, %zu right\n", left, right);
    CHECK(left > 32);
    CHECK(right <= 5);

    const auto path = std::string(EVO_TEST_OUT_DIR) + "/offscreen/wipe.png";
    evo::image::writePng(path, image.width, image.height, image.rgb.data());
    std::printf("wrote %s\n", path.c_str());
}

void testMissingShaderNamesPath(evo::gfx::Context& ctx) {
    const std::string missing = "/nonexistent/evo/missing.comp.spv";
    bool threw = false;
    try {
        evo::gfx::loadShaderModule(ctx.device(), missing);
    } catch (const std::runtime_error& error) {
        threw = true;
        CHECK(std::string(error.what()).find(missing) != std::string::npos);
    }
    CHECK(threw);
}

} // namespace

int main() {
    try {
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        auto ctx = evo::gfx::Context::create({});
        testRendersAndReadsBack(ctx);
        testOddSizeEdgePixel(ctx);
        testTimelineFrames(ctx);
        testWipeSplitsTheFrame(ctx);
        testMissingShaderNamesPath(ctx);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("offscreen_test: %d checks passed\n", checksPassed);
    return 0;
}
