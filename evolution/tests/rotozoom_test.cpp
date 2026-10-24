// LENS, chapter 6: the registry entry, the palette, the texture on the CPU (a checker of bevelled tiles with
// EVOLUTION stamped from the font, outlined and shadowed), the view (rotozoomView against the header's
// formulas), and the 320x200 palette index image from the retro stage on the GPU. Test builds of the shader
// (ROTOZOOM_TEST_VIEW: no rotation, 1 or 8 texels per pixel, the lens held at the screen's middle) show the
// tile itself outside the lens, and with coordinate textures map the lens's refraction pixel by pixel and
// measure 2x at its middle. The show's shader matches a CPU reference inside and outside the lens, shows the
// rim and the highlight, passes the logo under the lens, and is deterministic per retro frame. The stage
// refuses a texture without a shader. Runs with the validation layer on and fails on any message it reports.

#include "gfx/context.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "retro/rotozoom.hpp"
#include "retro_stage_fixture.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"
#include "text/font8x8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::EffectInfo;
using evo::retro::EffectTexture;
using evo::retro::Palette;
using evo::retro::RotozoomView;
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
constexpr int kSize = static_cast<int>(evo::retro::kRotozoomSize);
constexpr int kLensChapter = 6;
constexpr std::uint32_t kRate = evo::retro::kRotozoomRetroRate;
constexpr double kRadius = evo::retro::kRotozoomLensRadius;
constexpr double kRimInner = 1 - 4.0 / kRadius; // the rim: the outer 4 pixels of the radius

// A LENS frame at `local` seconds into the chapter.
TimelineFrame lens(double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(kLensChapter) + local);
    if (frame.chapter != kLensChapter || frame.effect != Effect::Lens)
        throw std::runtime_error("expected a LENS frame at local time " + std::to_string(local));
    return frame;
}

double duration() {
    return lens(0).chapterDuration;
}

std::uint32_t frameCount() {
    return static_cast<std::uint32_t>(std::ceil(duration() * kRate));
}

double timeIn(std::uint32_t retroFrame, double within = .5) {
    return std::min((retroFrame + within) / kRate, duration() - 1e-6);
}

TimelineFrame frameOf(std::uint32_t retroFrame, double within = .5) {
    return lens(timeIn(retroFrame, within));
}

int brightness(std::uint32_t entry) {
    return static_cast<int>((entry & 0xff) + (entry >> 8 & 0xff) + (entry >> 16 & 0xff));
}
int red(std::uint32_t entry) {
    return static_cast<int>(entry & 0xff);
}
int blue(std::uint32_t entry) {
    return static_cast<int>(entry >> 16 & 0xff);
}

bool isTexel(int index) {
    return index >= evo::retro::kRotozoomTexture && index < evo::retro::kRotozoomTexture + 64;
}
bool isGlass(int index) {
    return index >= evo::retro::kRotozoomTexture + 64 && index < evo::retro::kRotozoomTexture + 128;
}
bool isRim(int index) {
    return index >= evo::retro::kRotozoomRim && index < evo::retro::kRotozoomRim + 16;
}
bool isInk(int index) {
    return index >= evo::retro::kRotozoomInk && index < evo::retro::kRotozoomInk + 14;
}

// Whether texel (u, v) of the tile is set in the logo's font mask.
bool logoMask(int u, int v) {
    const int x = u - evo::retro::kRotozoomLogoX, y = v - evo::retro::kRotozoomLogoY;
    const std::string logo = evo::retro::kRotozoomLogo;
    if (x < 0 || y < 0 || x >= 8 * static_cast<int>(logo.size()) || y >= 16)
        return false;
    const evo::text::GlyphRows& rows = evo::text::glyph(logo[static_cast<std::size_t>(x / 8)]);
    return (rows[static_cast<std::size_t>(y / 2)] & (0x80u >> (x % 8))) != 0;
}

// --- The lens and the view, written out from the header ---------------------------------------------------

struct LensPoint {
    double r;       // 0 at the centre, 1 at the edge
    double nx, ny;  // the offset in lens units (x across, y down, both / 48 after squaring up)
    int sourceX256; // the point it shows (inside the glass), in 1/256 pixel
    int sourceY256;
};

LensPoint lensPoint(int cx, int cy, int x, int y) {
    const double dx = x - cx, dy = y - cy;
    LensPoint p{};
    p.nx = dx / kRadius;
    p.ny = 1.2 * dy / kRadius;
    p.r = std::hypot(p.nx, p.ny);
    const double q = p.r < 1 ? 0.5 + 0.5 * (1 - std::sqrt(1 - p.r * p.r)) : 1;
    p.sourceX256 = static_cast<int>(std::floor((cx + dx * q) * 256 + 0.5));
    p.sourceY256 = static_cast<int>(std::floor((cy + dy * q) * 256 + 0.5));
    return p;
}

// Whether a lens point lies within `margin` of the highlight.
bool nearHighlight(const LensPoint& p, double margin) {
    return std::hypot(p.nx + 0.4, p.ny + 0.4) < 0.12 + margin;
}

// The rotozoomed texel at screen point (x256, y256) / 256 by the 16.16 view, as the header gives it.
std::uint8_t rotozoomed(const RotozoomView& view, const EffectTexture& texture, int x256, int y256) {
    const int x = x256 >> 8, y = y256 >> 8, fx = x256 & 255, fy = y256 & 255;
    const auto axis = [&](std::int32_t origin, std::int32_t stepX, std::int32_t stepY) {
        return static_cast<std::uint32_t>(origin) +
               static_cast<std::uint32_t>(x) * static_cast<std::uint32_t>(stepX) +
               static_cast<std::uint32_t>(y) * static_cast<std::uint32_t>(stepY) +
               static_cast<std::uint32_t>((fx * stepX + fy * stepY) >> 8);
    };
    const std::uint32_t u = axis(view.originU, view.stepXU, view.stepYU);
    const std::uint32_t v = axis(view.originV, view.stepXV, view.stepYV);
    return texture.texels[((v >> 16) & 127) * 128 + ((u >> 16) & 127)];
}
std::uint8_t rotozoomedPixel(const RotozoomView& view, const EffectTexture& texture, int x, int y) {
    return rotozoomed(view, texture, x * 256, y * 256);
}

// --- CPU tests --------------------------------------------------------------------------------------------

void testRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::Lens);
    CHECK(std::strcmp(info.shader, evo::retro::kRotozoomShader) == 0);
    CHECK(info.retroRate == 35 && kRate == 35);
    CHECK(info.palette == &evo::retro::rotozoomPalette);
    CHECK(info.texture == &evo::retro::rotozoomTexture);
    CHECK(info.text == nullptr && info.background == nullptr && info.scene == nullptr);
}

// Black at 0, white at 255; the tile ramps and the chrome rising from dark to light; the glass copy of each
// texture colour lighter and bluer; the rim ramp rising to white; the rest black; the same at any frame.
void testPalette() {
    Palette palette{};
    palette.fill(0x12345678u);
    evo::retro::rotozoomPalette(palette, lens(1), 10);
    CHECK(palette[0] == evo::retro::rgba(0, 0, 0));
    CHECK(palette[255] == evo::retro::rgba(255, 255, 255));
    for (std::uint32_t entry : palette)
        CHECK((entry >> 24) == 0xff);
    const auto rising = [&](int first, int count) {
        for (int i = first; i + 1 < first + count; ++i)
            CHECK(brightness(palette[i + 1]) > brightness(palette[i]));
    };
    rising(evo::retro::kRotozoomTileA, 24);
    rising(evo::retro::kRotozoomTileB, 24);
    rising(evo::retro::kRotozoomRim, 16);
    CHECK(palette[evo::retro::kRotozoomRim + 15] == evo::retro::rgba(255, 255, 255));
    // Tile A blue, B violet (redder); the chrome brighter than every tile; outline and shadow dark.
    for (int i = 0; i < 24; ++i)
        CHECK(blue(palette[16 + i]) > red(palette[16 + i]) && red(palette[40 + i]) > red(palette[16 + i]));
    for (int i = 0; i < 14; ++i)
        CHECK(brightness(palette[evo::retro::kRotozoomInk + i]) > 3 * 150);
    CHECK(brightness(palette[evo::retro::kRotozoomOutline]) < 3 * 40);
    CHECK(brightness(palette[evo::retro::kRotozoomShadow]) < 3 * 40);
    // Through the glass: lighter and bluer.
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t plain = palette[16 + i], glass = palette[16 + 64 + i];
        CHECK(brightness(glass) > brightness(plain) || brightness(plain) > 3 * 240);
        CHECK(blue(glass) >= blue(plain));
    }
    for (int i = 1; i < 255; ++i)
        if (i < 16 || i >= 160)
            CHECK(palette[static_cast<std::size_t>(i)] == evo::retro::rgba(0, 0, 0));
    Palette later{};
    evo::retro::rotozoomPalette(later, lens(9), 300);
    CHECK(later == palette);
}

// The tile: 128x128 texels, all of the texture's 64 colours; the logo's ink exactly where the font sets it
// (row by row in the chrome gradient); an outline round the ink and a drop shadow under it; elsewhere a
// checker of 16x16 tiles, A and B alternating, bevelled light at the top and left; the same every build.
void testTexture() {
    const EffectTexture texture = evo::retro::rotozoomTexture();
    // shaders/retro/rotozoom.comp hard-codes the size (kSize = 128, wrapping with & 127).
    static_assert(evo::retro::kRotozoomSize == 128);
    CHECK(texture.width == 128 && texture.height == 128 && texture.texels.size() == 128u * 128u);
    const auto at = [&](int u, int v) {
        return static_cast<int>(
            texture.texels[static_cast<std::size_t>(((v + kSize) % kSize) * kSize + (u + kSize) % kSize)]);
    };
    std::set<int> colours;
    std::size_t ink = 0, outline = 0, shadow = 0;
    for (int v = 0; v < kSize; ++v)
        for (int u = 0; u < kSize; ++u) {
            const int texel = at(u, v);
            CHECK(isTexel(texel));
            colours.insert(texel);
            const bool set = logoMask(u, v);
            CHECK(set == isInk(texel));
            if (set) {
                CHECK(texel == evo::retro::kRotozoomInk + (v - evo::retro::kRotozoomLogoY));
                ++ink;
                continue;
            }
            bool touches = false;
            for (int dv = -1; dv <= 1; ++dv)
                for (int du = -1; du <= 1; ++du)
                    touches = touches || logoMask(u + du, v + dv);
            if (touches) {
                CHECK(texel == evo::retro::kRotozoomOutline);
                ++outline;
            } else if (logoMask(u - 2, v - 2)) {
                CHECK(texel == evo::retro::kRotozoomShadow);
                ++shadow;
            } else {
                // The checker: tile (u / 16, v / 16), A where even.
                const int base =
                    (u / 16 + v / 16) % 2 == 0 ? evo::retro::kRotozoomTileA : evo::retro::kRotozoomTileB;
                CHECK(texel >= base && texel < base + 24);
                // Bevelled: the top and left edges lightest, the bottom and right edges (which win at the
                // corners) darkest.
                const int i = u % 16, j = v % 16;
                if (std::min(i, j) == 0 && std::max(i, j) <= 12)
                    CHECK(texel == base + 23);
                if (std::max(i, j) == 15)
                    CHECK(texel == base);
            }
        }
    std::printf("rotozoom: texture: %zu colours, logo %zu ink, %zu outline, %zu shadow texels\n",
                colours.size(), ink, outline, shadow);
    CHECK(colours.size() == 64);
    CHECK(ink >= 300 && outline >= ink / 2 && shadow >= 50);
    CHECK(evo::retro::rotozoomTexture().texels == texture.texels);
}

// rotozoomView against the header's formulas; the lens bounces round the whole screen within its edges.
void testView() {
    const double tau = 2 * std::numbers::pi;
    for (double t : {0.0, 1.0 / 35, 2.5, 7.77, 16.0}) {
        const RotozoomView view = evo::retro::rotozoomView(t);
        const double angle = 0.95 * std::sin(0.33 * t) + 0.08 * t;
        const double zoom = 0.7 + 0.4 * std::sin(0.41 * t - 1.2);
        const double cu = 64 + 23 * t, cv = 64 + 40 * std::sin(0.29 * t);
        const auto fixed = [](double x) { return static_cast<std::int32_t>(std::lround(x * 65536)); };
        CHECK(view.stepXU == fixed(zoom * std::cos(angle)) && view.stepXV == fixed(zoom * std::sin(angle)));
        CHECK(view.stepYU == fixed(-1.2 * zoom * std::sin(angle)) &&
              view.stepYV == fixed(1.2 * zoom * std::cos(angle)));
        // The texture point at the screen's middle (pixel 160, 100), modulo the tile.
        const auto wrapped = [](std::int64_t a, double b) {
            const double d = std::fmod(static_cast<double>(a) / 65536 - b, 128);
            return std::min(std::abs(d), 128 - std::abs(d));
        };
        CHECK(wrapped(std::int64_t{view.originU} + 160 * std::int64_t{view.stepXU} +
                          100 * std::int64_t{view.stepYU},
                      cu) < 1e-3);
        CHECK(wrapped(std::int64_t{view.originV} + 160 * std::int64_t{view.stepXV} +
                          100 * std::int64_t{view.stepYV},
                      cv) < 1e-3);
        const double phase = t / 6.2 - std::floor(t / 6.2);
        const double tri = phase < 0.5 ? 2 * phase : 2 - 2 * phase;
        CHECK(view.lensX == static_cast<std::int32_t>(std::floor(48 + 224 * tri + 0.5)));
        CHECK(view.lensY ==
              static_cast<std::int32_t>(std::floor(159 - 119 * std::abs(std::sin(tau / 2 * t / 2.3)) + 0.5)));
    }
    int left = 999, right = -1, top = 999, bounces = 0;
    bool down = false;
    for (std::uint32_t retroFrame = 0; retroFrame < frameCount(); ++retroFrame) {
        const RotozoomView view = evo::retro::rotozoomView(static_cast<double>(retroFrame) / kRate);
        CHECK(view.lensX >= 48 && view.lensX <= 272 && view.lensY >= 40 && view.lensY <= 159);
        left = std::min(left, view.lensX), right = std::max(right, view.lensX),
        top = std::min(top, view.lensY);
        if (view.lensY >= 157 && !down)
            ++bounces;
        down = view.lensY >= 157;
    }
    std::printf("rotozoom: the lens runs x %d..%d, up to y %d, %d bounces\n", left, right, top, bounces);
    CHECK(left <= 60 && right >= 260 && top <= 45 && bounces >= 5);
}

// --- GPU --------------------------------------------------------------------------------------------------

// The test builds of the shader (no rotation, 1 or 8 texels per pixel sampled half a texel in, the lens still
// at (160, 100)),
// compiled for this test only; absolute paths, which the stage's shader directory leaves as they are.
const char* const kViewShader = EVO_TEST_SPIRV_DIR "/test_rotozoom_view1.comp.spv";
const char* const kFineShader = EVO_TEST_SPIRV_DIR "/test_rotozoom_view8.comp.spv";
constexpr int kTestLensX = 160, kTestLensY = 100;

// Coordinate textures: texel (u, v) holds u, or v.
EffectTexture coordinateTexture(bool vertical) {
    EffectTexture texture{128, 128, std::vector<std::uint8_t>(128 * 128)};
    for (int v = 0; v < kSize; ++v)
        for (int u = 0; u < kSize; ++u)
            texture.texels[static_cast<std::size_t>(v * kSize + u)] =
                static_cast<std::uint8_t>(vertical ? v : u);
    return texture;
}
EffectTexture uTexture() {
    return coordinateTexture(false);
}
EffectTexture vTexture() {
    return coordinateTexture(true);
}

EffectInfo viewInfo(evo::retro::TextureBuilder texture, const char* shader = kViewShader) {
    EffectInfo info = evo::retro::effectFor(Effect::Lens);
    info.shader = shader;
    info.texture = texture;
    return info;
}
const EffectInfo& viewTile(Effect) {
    static const EffectInfo info = viewInfo(evo::retro::rotozoomTexture);
    return info;
}
const EffectInfo& viewU(Effect) {
    static const EffectInfo info = viewInfo(uTexture);
    return info;
}
const EffectInfo& viewV(Effect) {
    static const EffectInfo info = viewInfo(vTexture);
    return info;
}
const EffectInfo& fineU(Effect) {
    static const EffectInfo info = viewInfo(uTexture, kFineShader);
    return info;
}
const EffectInfo& fineV(Effect) {
    static const EffectInfo info = viewInfo(vTexture, kFineShader);
    return info;
}

// Registries the stage refuses: a texture without a shader, and one shader named with and without a texture.
const EffectInfo& textureWithoutShader(Effect) {
    static const EffectInfo info = [] {
        EffectInfo e = evo::retro::effectFor(Effect::Lens);
        e.shader = nullptr;
        return e;
    }();
    return info;
}
const EffectInfo& shaderBothWays(Effect effect) {
    static const EffectInfo plain = [] {
        EffectInfo e = evo::retro::effectFor(Effect::Lens);
        e.texture = nullptr;
        return e;
    }();
    // The first effect with the texture, so the stage builds that pipeline (valid) before it meets the other.
    return effect == Effect::Boot ? evo::retro::effectFor(Effect::Lens) : plain;
}

void testRefusals(evo::gfx::Context& ctx) {
    bool refused = false;
    try {
        RetroStageFixture fixture(ctx, false, textureWithoutShader);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    CHECK(refused);
    refused = false;
    try {
        RetroStageFixture fixture(ctx, false, shaderBothWays);
    } catch (const std::logic_error&) {
        refused = true;
    }
    CHECK(refused);
}

// The unturned, unzoomed view: outside the lens the tile itself, (x, y) showing texel (x & 127, y & 127).
// Inside, the rim and the glass; with the coordinate textures, every glass pixel shows the pixel the
// refraction table names (to a pixel where the rounding falls differently in single precision), and the
// magnification at the middle is 2x within 5 %.
void testView(evo::gfx::Context& ctx) {
    const EffectTexture tile = evo::retro::rotozoomTexture();
    std::vector<std::uint8_t> images[5];
    const evo::render::RetroStage::EffectLookup lookups[5] = {viewTile, viewU, viewV, fineU, fineV};
    for (int i = 0; i < 5; ++i) {
        RetroStageFixture fixture(ctx, false, lookups[i]);
        images[i] = fixture.render(frameOf(40));
    }
    const auto at = [&](int image, int x, int y) {
        return static_cast<int>(images[image][static_cast<std::size_t>(y * kWidth + x)]);
    };

    std::size_t outside = 0, glass = 0, exact = 0, rim = 0, white = 0;
    long rimUpperLeft = 0, rimLowerRight = 0, upperLeft = 0, lowerRight = 0;
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const LensPoint p = lensPoint(kTestLensX, kTestLensY, x, y);
            if (p.r > 1.01) {
                CHECK(at(0, x, y) == tile.texels[static_cast<std::size_t>((y & 127) * 128 + (x & 127))]);
                CHECK(at(1, x, y) == (x & 127) && at(2, x, y) == (y & 127));
                ++outside;
            } else if (p.r > kRimInner + 0.01 && p.r < 0.99) {
                CHECK(isRim(at(0, x, y)) && at(0, x, y) == at(1, x, y));
                ++rim;
                const int shade = at(0, x, y) - evo::retro::kRotozoomRim;
                if (p.nx < -0.3 && p.ny < -0.3)
                    rimUpperLeft += shade, ++upperLeft;
                if (p.nx > 0.3 && p.ny > 0.3)
                    rimLowerRight += shade, ++lowerRight;
            } else if (p.r < kRimInner - 0.01 && !nearHighlight(p, 0.02)) {
                CHECK(isGlass(at(0, x, y)));
                // The coordinate textures through the glass: the source pixel, give or take one.
                const int u = at(1, x, y) - 64, v = at(2, x, y) - 64;
                CHECK(u >= 0 && v >= 0);
                const auto off = [](int got, int want) {
                    const int d = ((got - want) % 128 + 128) % 128;
                    return std::min(d, 128 - d);
                };
                // The test view samples half a texel in: the texel under point s is floor(s + 1/2).
                const int du = off(u, ((p.sourceX256 + 128) >> 8) & 127);
                const int dv = off(v, ((p.sourceY256 + 128) >> 8) & 127);
                CHECK(du <= 1 && dv <= 1);
                exact += du == 0 && dv == 0;
                ++glass;
            } else if (std::hypot(p.nx + 0.4, p.ny + 0.4) < 0.05) {
                CHECK(at(0, x, y) == evo::retro::kRotozoomWhite);
                ++white;
            }
        }
    // The magnification at the middle, from the fine view (8 texels per pixel, so the coordinate textures
    // show where each pixel looks to 1/8 pixel): along the centre row (column), the least-squares slope of
    // the point a pixel shows against its offset, within 6 pixels (5 rows) of the centre, where the
    // refraction table magnifies by 2 to 1.98.
    const auto magnification = [&](int image, bool down) {
        const int reach = down ? 5 : 6;
        const int x0 = kTestLensX, y0 = kTestLensY;
        double sdd = 0, sds = 0, sd = 0, ss = 0;
        int n = 0;
        for (int d = -reach; d <= reach; ++d) {
            const int got = at(image, down ? x0 : x0 + d, down ? y0 + d : y0) - 64;
            const int centre = at(image, x0, y0) - 64;
            const double s = ((got - centre) % 128 + 128 + 64) % 128 - 64; // unwrapped, in 1/8 pixel
            sdd += d * d, sds += d * s, sd += d, ss += s, ++n;
        }
        const double slope = (sds - sd * ss / n) / (sdd - sd * sd / n);
        return 8 / slope;
    };
    const double magnifyX = magnification(3, false), magnifyY = magnification(4, true);
    std::printf(
        "rotozoom: test view: %zu pixels of the tile outside the lens; %zu glass pixels (%.2f %% on the "
        "exact source pixel), %zu rim, %zu white; magnification %.3f across, %.3f down; rim shade %.1f "
        "upper left, %.1f lower right\n",
        outside, glass, 100.0 * exact / glass, rim, white, magnifyX, magnifyY,
        double(rimUpperLeft) / upperLeft, double(rimLowerRight) / lowerRight);
    CHECK(outside > 50000 && glass > 4500 && rim > 600 && white >= 9);
    CHECK(exact >= glass * 98 / 100);
    CHECK(std::abs(magnifyX - 2) <= 0.1 && std::abs(magnifyY - 2) <= 0.1); // 2x within 5 %
    CHECK(double(rimUpperLeft) / upperLeft >
          double(rimLowerRight) / lowerRight + 5); // lit from the upper left
}

// The ink pixels of the logo the CPU reference puts within half the lens radius of its centre.
std::size_t logoUnderLens(const EffectTexture& tile, std::uint32_t retroFrame) {
    const RotozoomView view = evo::retro::rotozoomView(static_cast<double>(retroFrame) / kRate);
    std::size_t ink = 0;
    for (int y = view.lensY - 20; y <= view.lensY + 20; ++y)
        for (int x = view.lensX - 24; x <= view.lensX + 24; ++x)
            ink +=
                lensPoint(view.lensX, view.lensY, x, y).r < 0.5 && isInk(rotozoomedPixel(view, tile, x, y));
    return ink;
}

// The show's shader at three moments, one with the logo under the lens: outside the lens the CPU reference
// of the 16.16 view (but for texel boundaries that single precision moves), inside it glass, rim and the
// highlight; many colours; deterministic per retro frame.
void testImages(evo::gfx::Context& ctx) {
    const EffectTexture tile = evo::retro::rotozoomTexture();
    std::uint32_t logoFrame = 0;
    std::size_t mostInk = 0;
    for (std::uint32_t retroFrame = 0; retroFrame < frameCount(); ++retroFrame) {
        const std::size_t ink = logoUnderLens(tile, retroFrame);
        if (ink > mostInk)
            mostInk = ink, logoFrame = retroFrame;
    }
    std::printf(
        "rotozoom: the logo is most under the lens at retro frame %u (%.3f s into the chapter, %.3f s "
        "into the show), %zu ink pixels\n",
        logoFrame, timeIn(logoFrame), evo::show::chapterStart(kLensChapter) + timeIn(logoFrame), mostInk);
    CHECK(mostInk >= 100);

    RetroStageFixture fixture(ctx);
    const std::uint32_t frames[] = {3 * kRate, logoFrame, frameCount() - 2 * kRate};
    for (std::uint32_t retroFrame : frames) {
        bool rendered = false;
        const std::vector<std::uint8_t> image = fixture.render(frameOf(retroFrame, .1), &rendered);
        CHECK(rendered && fixture.stage().retroFrame() == retroFrame);
        const RotozoomView view = evo::retro::rotozoomView(static_cast<double>(retroFrame) / kRate);
        std::set<int> indices;
        std::size_t outside = 0, matching = 0, inside = 0, glass = 0, refracted = 0, ink = 0, glassInk = 0;
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x) {
                const int index = image[static_cast<std::size_t>(y * kWidth + x)];
                CHECK(isTexel(index) || isGlass(index) || isRim(index) || index == 255);
                indices.insert(index);
                ink += isInk(index);
                const LensPoint p = lensPoint(view.lensX, view.lensY, x, y);
                if (p.r > 1.01) {
                    ++outside;
                    matching += index == rotozoomedPixel(view, tile, x, y);
                } else if (p.r < kRimInner - 0.01 && !nearHighlight(p, 0.02)) {
                    ++inside;
                    glass += isGlass(index);
                    glassInk += isInk(index - 64);
                    refracted += index == rotozoomed(view, tile, p.sourceX256, p.sourceY256) + 64;
                }
            }
        std::printf(
            "rotozoom: retro frame %u: %zu indices; %.2f %% of the outside and %.2f %% of the glass as the "
            "reference; %zu ink pixels, %zu of them under the lens\n",
            retroFrame, indices.size(), 100.0 * matching / outside, 100.0 * refracted / inside, ink,
            glassInk);
        CHECK(matching >= outside * 98 / 100);
        CHECK(glass == inside && refracted >= inside * 97 / 100);
        CHECK(indices.size() >= 40);
        if (retroFrame == logoFrame)
            CHECK(glassInk >= mostInk / 2);

        // Deterministic: the same retro frame at another moment within it, with the music signals pushed,
        // after another frame, gives the identical image.
        fixture.render(frameOf(retroFrame > 40 ? retroFrame - 40 : retroFrame + 40), &rendered);
        CHECK(rendered);
        TimelineFrame again = frameOf(retroFrame, .9);
        again.kick = 1;
        again.energy = 1;
        again.beatPhase = .5f;
        CHECK(fixture.render(again, &rendered) == image);
        CHECK(rendered);
    }
    // A second stage, fresh, renders the same retro frame identically.
    RetroStageFixture other(ctx);
    CHECK(other.render(frameOf(logoFrame)) == fixture.render(frameOf(logoFrame, .3)));
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testPalette();
        testTexture();
        testView();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testRefusals(ctx);
            testView(ctx);
            testImages(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("rotozoom_test: %d checks passed\n", checksPassed);
    return 0;
}
