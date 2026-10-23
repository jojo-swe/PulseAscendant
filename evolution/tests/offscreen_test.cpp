#include "gfx/context.hpp"
#include "gfx/offscreen.hpp"
#include "gfx/shader.hpp"
#include "image/png.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    odd.render(0.0);
    auto frame = odd.readback();
    CHECK(frame.rgb.size() == 33u * 17u * 3u);

    // Pixel (32,16) lies in checker cell (1,0), an odd cell, so it shows the plain gradient (not the
    // inverse); the circle at t=0 is centred at (16.5,13.6) with radius 3.4 and does not reach it.
    const int x = 32, y = 16;
    const std::uint8_t* pixel = &frame.rgb[(static_cast<std::size_t>(y) * 33 + x) * 3];
    const int red = unorm8(32.0f / 33.0f);
    const int green = unorm8(16.0f / 17.0f);
    std::printf("pixel(32,16) = %d,%d,%d expected %d,%d,0\n", pixel[0], pixel[1], pixel[2], red, green);
    CHECK(within1(pixel[0], red));
    CHECK(within1(pixel[1], green));
    CHECK(within1(pixel[2], 0));
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
        testMissingShaderNamesPath(ctx);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("offscreen_test: %d checks passed\n", checksPassed);
    return 0;
}
