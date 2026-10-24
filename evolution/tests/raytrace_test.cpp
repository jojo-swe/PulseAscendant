// RAYTRACE, chapter 7: the registry entry, the fixed palette (the 6x6x6 cube and 24 greys), the quantiser on
// the CPU and on the GPU (a test shader running the shader's own quantiser on known colours), and the
// ray-traced 320x200 palette index image from the retro stage: the checkerboard in the lower half, its
// reflection in the middle sphere, motion between retro frames and determinism within one. Runs with the
// validation layer on and fails on any message it reports.

#include "gfx/context.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "retro/raytrace.hpp"
#include "retro/scene.hpp"
#include "retro_stage_fixture.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::EffectInfo;
using evo::retro::Palette;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::test::RetroStageFixture;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr int kWidth = static_cast<int>(RetroStageFixture::kWidth);
constexpr int kHeight = static_cast<int>(RetroStageFixture::kHeight);
constexpr int kRaytraceChapter = 7;
constexpr std::uint32_t kRate = evo::retro::kRaytraceRetroRate;

TimelineFrame raytrace(double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(kRaytraceChapter) + local);
    if (frame.chapter != kRaytraceChapter || frame.effect != Effect::Raytrace)
        throw std::runtime_error("expected a RAYTRACE frame at local time " + std::to_string(local));
    return frame;
}

double timeIn(std::uint32_t retroFrame, double within = .5) {
    return (retroFrame + within) / kRate;
}

std::array<int, 3> rgbOf(std::uint32_t entry) {
    return {static_cast<int>(entry & 0xff), static_cast<int>(entry >> 8 & 0xff),
            static_cast<int>(entry >> 16 & 0xff)};
}

Palette palette() {
    Palette p{};
    evo::retro::raytracePalette(p, raytrace(1), 10);
    return p;
}

void testRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::Raytrace);
    CHECK(info.shader != nullptr && std::string(info.shader) == evo::retro::kRaytraceShader);
    CHECK(info.retroRate == 10 && kRate == 10);
    CHECK(info.palette == &evo::retro::raytracePalette);
    CHECK(info.scene == nullptr && info.text == nullptr);
}

// Entries 16..231 are exactly the 6x6x6 cube (levels 0, 51, ..., 255), 232..255 the greys 8, 18, ..., 238,
// 0..15 black; fixed over the chapter.
void testPalette() {
    using namespace evo::retro;
    Palette p{};
    p.fill(0x12345678u);
    raytracePalette(p, raytrace(1), 10);
    for (int i = 0; i < 16; ++i)
        CHECK(p[i] == rgba(0, 0, 0));
    for (int r = 0; r < 6; ++r)
        for (int g = 0; g < 6; ++g)
            for (int b = 0; b < 6; ++b)
                CHECK(p[16 + 36 * r + 6 * g + b] == rgba(static_cast<std::uint8_t>(51 * r),
                                                         static_cast<std::uint8_t>(51 * g),
                                                         static_cast<std::uint8_t>(51 * b)));
    for (int k = 0; k < 24; ++k) {
        const auto v = static_cast<std::uint8_t>(8 + 10 * k);
        CHECK(p[232 + k] == rgba(v, v, v));
    }
    CHECK(kRaytraceCube + 216 == kRaytraceGreys && kRaytraceGreys + kRaytraceGreyCount == 256);
    Palette later{};
    raytracePalette(later, raytrace(30), 300);
    CHECK(later == p);
}

int distance2(const std::array<int, 3>& a, const std::array<int, 3>& b) {
    return (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
}

// The quantiser on the CPU: the nearest of all 240 entries to the Bayer-moved colour (a brute-force search,
// ties to the cube), and a flat colour averages back to itself over a 4x4 tile.
void testQuantiser() {
    const Palette p = palette();
    CHECK(evo::retro::raytraceBayer(0, 0) == 0 && evo::retro::raytraceBayer(1, 0) == 8 &&
          evo::retro::raytraceBayer(0, 1) == 12 && evo::retro::raytraceBayer(3, 3) == 5 &&
          evo::retro::raytraceBayer(4, 4) == 0);
    std::size_t greys = 0, cubes = 0;
    for (int r = 0; r < 256; r += 5)
        for (int g = 0; g < 256; g += 7)
            for (int b = 0; b < 256; b += 11)
                for (std::uint32_t bayer = 0; bayer < 16; ++bayer) {
                    // T in units of 1/32: 32 c + 51 (2 bayer - 15).
                    const int offset = 51 * (2 * static_cast<int>(bayer) - 15);
                    const std::array<int, 3> t = {32 * r + offset, 32 * g + offset, 32 * b + offset};
                    int best = -1, bestDistance = 0;
                    for (int i = 16; i < 256; ++i) {
                        auto c = rgbOf(p[i]);
                        for (int& v : c)
                            v *= 32;
                        const int d = distance2(t, c);
                        if (best < 0 || d < bestDistance)
                            best = i, bestDistance = d;
                    }
                    const std::uint8_t index = evo::retro::raytraceQuantise(
                        static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                        static_cast<std::uint8_t>(b), bayer);
                    auto chosen = rgbOf(p[index]);
                    for (int& v : chosen)
                        v *= 32;
                    CHECK(index >= 16);
                    CHECK(distance2(t, chosen) == bestDistance); // equal only for a duplicate colour
                    (index >= evo::retro::kRaytraceGreys ? greys : cubes)++;
                }
    std::printf("raytrace: quantiser matches the nearest entry (%zu cube, %zu grey)\n", cubes, greys);
    CHECK(greys > 0 && cubes > greys);

    // Flat colours dither to about their own average over a 4x4 tile: brightness within 7 levels, each
    // channel within 15 (a low-chroma colour lands on a grey on some pixels, so it loses a little colour).
    int worst = 0, worstBrightness = 0;
    for (int r = 0; r < 256; r += 17)
        for (int g = 0; g < 256; g += 17)
            for (int b = 0; b < 256; b += 17) {
                std::array<int, 3> sum{};
                for (std::uint32_t y = 0; y < 4; ++y)
                    for (std::uint32_t x = 0; x < 4; ++x) {
                        const auto c = rgbOf(p[evo::retro::raytraceQuantise(
                            static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                            static_cast<std::uint8_t>(b), evo::retro::raytraceBayer(x, y))]);
                        for (int k = 0; k < 3; ++k)
                            sum[k] += c[k];
                    }
                const int in[3] = {r, g, b};
                for (int k = 0; k < 3; ++k)
                    worst = std::max(worst, std::abs(sum[k] - 16 * in[k]));
                worstBrightness =
                    std::max(worstBrightness, std::abs(sum[0] + sum[1] + sum[2] - 16 * (r + g + b)));
            }
    std::printf(
        "raytrace: a flat colour's 4x4 dither averages within %.2f levels a channel, %.2f in brightness\n",
        worst / 16.0, worstBrightness / 48.0);
    CHECK(worst <= 15 * 16);
    CHECK(worstBrightness <= 7 * 48);

    // The ordered dither is unbiased: the 16 Bayer moves 51 (2 bayer - 15) / 32 sum to exactly 0. So where
    // the greys never win (the cube alone), a flat colour's 16 thresholds average to it within 25/16 = 1.5625
    // levels a channel: the mean level of a channel is floor(c / 51) plus the share of the 16 thresholds past
    // its fraction, a whole number of 16ths, at most 25 / 51 of a sixteenth of a step (25 of 816) away.
    int offsets = 0;
    for (int bayer = 0; bayer < 16; ++bayer)
        offsets += 51 * (2 * bayer - 15);
    CHECK(offsets == 0);
    std::size_t cubeOnly = 0;
    int worstCube = 0;
    for (int r = 0; r < 256; r += 3)
        for (int g = 0; g < 256; g += 3)
            for (int b = 0; b < 256; b += 3) {
                std::array<int, 3> sum{};
                bool grey = false;
                for (std::uint32_t bayer = 0; bayer < 16; ++bayer) {
                    const std::uint8_t index = evo::retro::raytraceQuantise(
                        static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                        static_cast<std::uint8_t>(b), bayer);
                    grey |= index >= evo::retro::kRaytraceGreys;
                    const auto c = rgbOf(p[index]);
                    for (int k = 0; k < 3; ++k)
                        sum[k] += c[k];
                }
                if (grey)
                    continue;
                ++cubeOnly;
                const int in[3] = {r, g, b};
                for (int k = 0; k < 3; ++k)
                    worstCube = std::max(worstCube, std::abs(sum[k] - 16 * in[k]));
            }
    std::printf(
        "raytrace: %zu cube-only colours average within %.4f levels a channel over the 16 thresholds\n",
        cubeOnly, worstCube / 16.0);
    CHECK(cubeOnly >= 300000); // most of the 86^3 colours sampled
    CHECK(worstCube <= 25);    // 1.5625 levels
    // A cube colour comes out as itself everywhere.
    for (std::uint32_t bayer = 0; bayer < 16; ++bayer) {
        CHECK(evo::retro::raytraceQuantise(255, 0, 0, bayer) == 16 + 36 * 5);
        CHECK(evo::retro::raytraceQuantise(102, 153, 204, bayer) == 16 + 36 * 2 + 6 * 3 + 4);
    }
}

// The spheres of the CPU twin: the phase reduction to [-pi, pi) changes no angle's sine or cosine, so the
// orbits are the header's formulas.
void testSpheres() {
    CHECK(std::abs(evo::retro::raytracePhase(0.25) - 0.25) < 1e-15);
    for (double x : {0.0, 1.0, 3.0, 7.5, 20.0, 51.3, 400.0}) {
        const double reduced = evo::retro::raytracePhase(x);
        CHECK(reduced >= -3.14159265358979 && reduced < 3.14159265359);
        CHECK(std::abs(std::sin(reduced) - std::sin(x)) < 1e-12 &&
              std::abs(std::cos(reduced) - std::cos(x)) < 1e-12);
    }
    for (double t : {0.0, 2.7, 13.1, 29.9}) {
        const auto spheres = evo::retro::raytraceSpheres(t);
        CHECK(spheres[0].centre[0] == 0 && spheres[0].centre[1] == 1.3 && spheres[0].radius == 1.3);
        for (int i = 1; i < evo::retro::kRaytraceSpheres; ++i) {
            const double a = 0.6 * t + (i - 1) * 2 * 3.14159265358979323846 / 5;
            const auto& s = spheres[static_cast<std::size_t>(i)];
            CHECK(std::abs(s.centre[0] - 3 * std::sin(a)) < 1e-12);
            CHECK(std::abs(s.centre[1] - (0.6 + 0.9 * std::abs(std::sin(1.7 * t + 1.3 * i)))) < 1e-12);
            CHECK(std::abs(s.centre[2] + 3 * std::cos(a)) < 1e-12);
            CHECK(s.radius == 0.6);
        }
    }
}

// The test shader's colour of a pixel (must match shaders/test/raytrace_quantise.comp): near-greys in the
// top 64 rows, a scramble of the whole range below.
std::array<int, 3> testColour(int x, int y) {
    if (y < 64) {
        const int v = x * 255 / 319, d = y % 16;
        return {std::min(255, v + d), v, std::max(0, v - d / 2)};
    }
    return {(x * 7 + y * 13) & 255, (x * 11 + y * 3) & 255, (x * 5 + y * 17) & 255};
}

const char* const kQuantiseShader = EVO_TEST_SPIRV_DIR "/test_raytrace_quantise.comp.spv";

// The registry with RAYTRACE's shader swapped for the quantiser test shader (an absolute path, which
// shaderDir() / path keeps as it is).
const EffectInfo& quantiserRegistry(Effect effect) {
    static const EffectInfo info{kQuantiseShader, evo::retro::kRaytraceRetroRate,
                                 evo::retro::raytracePalette};
    return effect == Effect::Raytrace ? info : evo::retro::effectFor(effect);
}

// The GPU quantiser (raytrace_quantise.glsl, run by the test shader) gives the CPU reference's index on
// every pixel.
void testGpuQuantiser(evo::gfx::Context& ctx) {
    RetroStageFixture fixture(ctx, false, quantiserRegistry);
    const std::vector<std::uint8_t> image = fixture.render(raytrace(timeIn(5)));
    std::size_t wrong = 0;
    std::set<std::uint8_t> indices;
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const auto c = testColour(x, y);
            const std::uint8_t expected = evo::retro::raytraceQuantise(
                static_cast<std::uint8_t>(c[0]), static_cast<std::uint8_t>(c[1]),
                static_cast<std::uint8_t>(c[2]),
                evo::retro::raytraceBayer(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
            const std::uint8_t got = image[static_cast<std::size_t>(y) * kWidth + x];
            indices.insert(got);
            if (got != expected && ++wrong <= 5)
                std::printf("raytrace: pixel (%d, %d) colour (%d, %d, %d): GPU %u, CPU %u\n", x, y, c[0],
                            c[1], c[2], got, expected);
        }
    std::printf("raytrace: GPU quantiser: %zu of %d pixels differ, %zu indices\n", wrong, kWidth * kHeight,
                indices.size());
    CHECK(wrong == 0);
    CHECK(indices.size() >= 200);
}

// Classes of palette entries as the eye sees them.
bool reddish(const std::array<int, 3>& c) {
    return c[0] >= 100 && c[1] <= 60 && c[2] <= 60;
}
bool whitish(const std::array<int, 3>& c) {
    return std::min({c[0], c[1], c[2]}) >= 130 &&
           std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}) <= 60;
}

// The checkerboard in rows 170..199 (in front of every sphere): along a row, cells of 4 pixels (one Bayer
// period) take the class of most of their pixels; the row must alternate red and white at least 3 times,
// with both well represented and the whole runs between the first and the last equally long (a row of the
// floor is a line of constant depth, its squares equally wide).
void checkCheckerboard(const std::vector<std::uint8_t>& image, const Palette& p) {
    for (int y = 170; y < kHeight; y += 3) {
        std::string cells;
        std::size_t red = 0, white = 0;
        for (int cx = 0; cx < kWidth / 4; ++cx) {
            int r = 0, w = 0;
            for (int x = cx * 4; x < cx * 4 + 4; ++x) {
                const auto c = rgbOf(p[image[static_cast<std::size_t>(y) * kWidth + x]]);
                r += reddish(c);
                w += whitish(c);
            }
            cells += r >= 3 ? 'R' : w >= 3 ? 'W' : '.';
            red += r >= 3;
            white += w >= 3;
        }
        std::vector<int> runs;
        char last = 0;
        for (char c : cells) {
            if (c == '.')
                continue;
            if (c != last)
                runs.push_back(0);
            ++runs.back();
            last = c;
        }
        if (y == 170 || y == 197)
            std::printf("raytrace: row %d: %s\n", y, cells.c_str());
        CHECK(runs.size() >= 4);
        CHECK(red >= 12 && white >= 12);
        const auto [low, high] = std::minmax_element(runs.begin() + 1, runs.end() - 1);
        CHECK(*high <= *low + 2);
    }
}

// The pixels of the middle sphere's lower disc (within 0.8 of its projected radius) that no orbiting sphere
// in front of it can cover (their projected discs with a margin of 30 %).
std::vector<std::size_t> mirrorPixels(std::uint32_t retroFrame) {
    const evo::retro::Camera camera = evo::retro::raytraceCamera();
    const auto spheres = evo::retro::raytraceSpheres(retroFrame / static_cast<double>(kRate));
    const auto disc = [&](const evo::retro::RaytraceSphere& s) {
        const evo::retro::Projected c = evo::retro::project(camera, s.centre);
        const double ry = s.radius / c.z * 100.0 / std::tan(camera.fovYDegrees * 3.14159265358979 / 360.0);
        return std::array<double, 5>{c.x, c.y, ry * evo::retro::kPixelAspect, ry, c.z};
    };
    const auto middle = disc(spheres[0]);
    std::vector<std::size_t> pixels;
    for (int y = static_cast<int>(middle[1]); y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const double u = (x + .5 - middle[0]) / middle[2], v = (y + .5 - middle[1]) / middle[3];
            if (u * u + v * v > .8 * .8)
                continue;
            bool hidden = false;
            for (int s = 1; s < evo::retro::kRaytraceSpheres; ++s) {
                const auto o = disc(spheres[static_cast<std::size_t>(s)]);
                const double ou = (x + .5 - o[0]) / o[2], ov = (y + .5 - o[1]) / o[3];
                hidden |= o[4] < middle[4] && ou * ou + ov * ov < 1.3 * 1.3;
            }
            if (!hidden)
                pixels.push_back(static_cast<std::size_t>(y) * kWidth + x);
        }
    return pixels;
}

// The retro frame from `first` on (within 4 s) where the most of the middle sphere's lower disc shows.
std::uint32_t clearestFrame(std::uint32_t first) {
    std::uint32_t best = first;
    std::size_t most = 0;
    for (std::uint32_t f = first; f < first + 40; ++f)
        if (const std::size_t n = mirrorPixels(f).size(); n > most)
            best = f, most = n;
    return best;
}

// The middle sphere's lower half reflects the checkerboard: red and white inside its disc.
void checkReflection(const std::vector<std::uint8_t>& image, const Palette& p, std::uint32_t retroFrame) {
    const std::vector<std::size_t> pixels = mirrorPixels(retroFrame);
    std::size_t red = 0, white = 0;
    for (std::size_t i : pixels) {
        const auto c = rgbOf(p[image[i]]);
        red += reddish(c);
        white += whitish(c);
    }
    std::printf(
        "raytrace: retro frame %u: the middle sphere's lower disc (%zu px) shows %zu red and %zu white\n",
        retroFrame, pixels.size(), red, white);
    CHECK(pixels.size() >= 500);
    CHECK(red >= pixels.size() / 10 && white >= pixels.size() / 10);
}

void testImages(evo::gfx::Context& ctx) {
    const Palette p = palette();
    RetroStageFixture fixture(ctx);
    const double duration = raytrace(0).chapterDuration;
    const auto count = static_cast<std::uint32_t>(std::floor(duration * kRate));
    std::printf("raytrace: %.2f s, %u retro frames\n", duration, count);
    std::vector<std::vector<std::uint8_t>> images;
    for (std::uint32_t retroFrame :
         {clearestFrame(count / 8), clearestFrame(count / 2), clearestFrame(count * 3 / 4)}) {
        bool rendered = false;
        const std::vector<std::uint8_t> image = fixture.render(raytrace(timeIn(retroFrame, .1)), &rendered);
        CHECK(rendered && fixture.stage().retroFrame() == retroFrame);
        std::set<std::uint8_t> indices(image.begin(), image.end());
        std::printf("raytrace: retro frame %u: %zu indices\n", retroFrame, indices.size());
        CHECK(indices.size() >= 40);
        CHECK(*indices.begin() >= 16); // only the cube and the greys
        // The sky: the top rows bluer than red.
        std::size_t blue = 0;
        for (int x = 0; x < kWidth; ++x) {
            const auto c = rgbOf(p[image[static_cast<std::size_t>(x)]]);
            blue += c[2] > c[0] + 40;
        }
        CHECK(blue > kWidth * 9 / 10);
        checkCheckerboard(image, p);
        checkReflection(image, p, retroFrame);

        // The next retro frame differs (the spheres move); this one is the same at another moment within
        // it, with the music signals pushed, after the other frame.
        const std::vector<std::uint8_t> next = fixture.render(raytrace(timeIn(retroFrame + 1)), &rendered);
        CHECK(rendered && next != image);
        TimelineFrame again = raytrace(timeIn(retroFrame, .9));
        again.kick = 1;
        again.energy = 1;
        again.beatPhase = .5f;
        CHECK(fixture.render(again, &rendered) == image);
        CHECK(rendered);
        images.push_back(image);
    }
    CHECK(images[0] != images[1] && images[1] != images[2]);
    RetroStageFixture other(ctx);
    CHECK(other.render(raytrace(timeIn(clearestFrame(count / 2), .7))) == images[1]);
}

// A sphere's projected disc: its centre's pixel position, its radii across and down in pixels, its depth.
struct Disc {
    double x, y, rx, ry, z;
};
Disc discOf(const evo::retro::RaytraceSphere& s) {
    const evo::retro::Projected c = evo::retro::project(evo::retro::raytraceCamera(), s.centre);
    const double ry = s.radius / c.z * 100.0 /
                      std::tan(evo::retro::raytraceCamera().fovYDegrees * 3.14159265358979 / 360.0);
    return {c.x, c.y, ry * evo::retro::kPixelAspect, ry, c.z};
}

// The pixels within `inner` of sphere `which`'s disc, on the screen, that no nearer sphere's disc (with a
// margin of 15 %) covers.
std::vector<std::size_t> clearPixels(std::uint32_t retroFrame, int which, double inner) {
    const auto spheres = evo::retro::raytraceSpheres(retroFrame / static_cast<double>(kRate));
    const Disc d = discOf(spheres[static_cast<std::size_t>(which)]);
    std::vector<std::size_t> pixels;
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const double u = (x + .5 - d.x) / d.rx, v = (y + .5 - d.y) / d.ry;
            if (u * u + v * v > inner * inner)
                continue;
            bool hidden = false;
            for (int s = 0; s < evo::retro::kRaytraceSpheres; ++s) {
                const Disc o = discOf(spheres[static_cast<std::size_t>(s)]);
                const double ou = (x + .5 - o.x) / o.rx, ov = (y + .5 - o.y) / o.ry;
                hidden |= s != which && o.z < d.z && ou * ou + ov * ov < 1.15 * 1.15;
            }
            if (!hidden)
                pixels.push_back(static_cast<std::size_t>(y) * kWidth + x);
        }
    return pixels;
}

// The glass sphere's green-cyan tint: green the strongest, clearly above red (the sky is bluest in chrome).
bool greenCyan(const std::array<int, 3>& c) {
    return c[1] >= c[0] + 51 && c[1] >= c[2] && c[2] >= c[0];
}

// The scene's constants are written twice, in raytrace.comp and raytrace.cpp: where the CPU twin projects the
// glass sphere (kRaytraceGlassSphere), the GPU image shows green-cyan glass, and where it projects a chrome
// orbiter, it does not. On the frame where the glass sphere shows the largest disc that nothing covers.
void testGlass(evo::gfx::Context& ctx) {
    const Palette p = palette();
    const auto count = static_cast<std::uint32_t>(std::floor(raytrace(0).chapterDuration * kRate));
    const int glass = evo::retro::kRaytraceGlassSphere;
    std::uint32_t best = 0;
    std::size_t most = 0;
    for (std::uint32_t f = 0; f < count; ++f) {
        const auto spheres = evo::retro::raytraceSpheres(f / static_cast<double>(kRate));
        const Disc d = discOf(spheres[static_cast<std::size_t>(glass)]);
        const std::size_t clear = clearPixels(f, glass, .7).size();
        // Wholly clear of the nearer spheres: as many pixels as the disc has on the screen.
        const double area = 3.14159265358979 * .49 * d.rx * d.ry;
        if (clear > most && clear >= area * .9)
            best = f, most = clear;
    }
    CHECK(most >= 150);
    RetroStageFixture fixture(ctx);
    const std::vector<std::uint8_t> image = fixture.render(raytrace(timeIn(best)));
    const auto share = [&](int which) {
        const std::vector<std::size_t> pixels = clearPixels(best, which, .7);
        std::size_t tinted = 0;
        for (std::size_t i : pixels)
            tinted += greenCyan(rgbOf(p[image[i]]));
        return std::make_pair(tinted, pixels.size());
    };
    const auto [tinted, pixels] = share(glass);
    std::printf("raytrace: retro frame %u: the glass sphere's disc (%zu px) is %zu green-cyan\n", best,
                pixels, tinted);
    CHECK(tinted * 2 >= pixels);
    std::size_t chromes = 0;
    for (int s = 1; s < evo::retro::kRaytraceSpheres; ++s) {
        if (s == glass)
            continue;
        const auto [chromeTinted, chromePixels] = share(s);
        if (chromePixels < 100)
            continue;
        ++chromes;
        std::printf("raytrace: retro frame %u: chrome sphere %d's disc (%zu px) is %zu green-cyan\n", best, s,
                    chromePixels, chromeTinted);
        CHECK(chromeTinted * 10 <= chromePixels);
    }
    CHECK(chromes >= 1);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testPalette();
        testQuantiser();
        testSpheres();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testGpuQuantiser(ctx);
            testImages(ctx);
            testGlass(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("raytrace_test: %d checks passed\n", checksPassed);
    return 0;
}
