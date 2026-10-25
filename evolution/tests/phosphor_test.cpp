// Phosphor persistence on the 1993 picture (EffectInfo::phosphor, render::RetroStage): a test effect draws a
// block that moves 12 pixels a retro frame, so each frame's picture holds the current block and the ghosts of
// the last three, each the earlier block's light times the decay to that power (within 2 levels), and nothing
// older. A picture reached by seeking equals the one reached by playing, byte for byte, and the stage draws
// only the frames it does not hold; the same effect without the switch shows no ghost. The registry turns it
// on exactly for BOOT, REBOOT, the DOS prompt, RETURN, CREDITS and THE END. Runs with the validation layer on
// and fails on any message it reports.
//
// `phosphor_test --look <dir> <show seconds>...` instead writes the retro picture of each time (played from 3
// retro frames before it, as the show would) to <dir> at 320x200 and 2x, and exits.

#include "gfx/context.hpp"
#include "image/png.hpp"
#include "retro/effects.hpp"
#include "retro/scene.hpp"
#include "retro_stage_fixture.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::show::Effect;
using evo::show::TimelineFrame;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr std::uint8_t kBlockIndex = 9;
constexpr int kBlockStep = 12, kBlockSize = 10, kBlockY = 50;

// The test effect: a block at x = 12 r in a grey of 200 (index 9), and from frame 6 on a white one (index 15)
// below it, both on black.
void blockPalette(evo::retro::Palette& palette, const TimelineFrame&, std::uint32_t) {
    palette.fill(evo::retro::rgba(0, 0, 0));
    palette[kBlockIndex] = evo::retro::rgba(200, 200, 200);
    palette[15] = evo::retro::rgba(255, 255, 255);
}
void addBlock(evo::retro::RetroScene& scene, int x0, int y0, std::uint8_t index) {
    const int corners[4][2] = {
        {x0, y0}, {x0 + kBlockSize, y0}, {x0 + kBlockSize, y0 + kBlockSize}, {x0, y0 + kBlockSize}};
    for (int t = 0; t < 2; ++t) {
        const int k[3] = {0, 1 + t, 2 + t};
        evo::retro::RetroTriangle triangle{};
        for (int v = 0; v < 3; ++v) {
            triangle.x[v] = corners[k[v]][0] * evo::retro::kSubpixel;
            triangle.y[v] = corners[k[v]][1] * evo::retro::kSubpixel;
            triangle.index[v] = index;
        }
        triangle.mode = evo::retro::kFlat;
        scene.triangles.push_back(triangle);
    }
}
void blockScene(evo::retro::RetroScene& scene, const TimelineFrame&, std::uint32_t retroFrame) {
    addBlock(scene, static_cast<int>(retroFrame) * kBlockStep, kBlockY, kBlockIndex);
    if (retroFrame >= 6)
        addBlock(scene, static_cast<int>(retroFrame) * kBlockStep, kBlockY + 20, 15);
}

constexpr evo::retro::EffectInfo kBlockPhosphor{
    .shader = nullptr, .retroRate = 35, .palette = blockPalette, .scene = blockScene, .phosphor = true};
constexpr evo::retro::EffectInfo kBlockPlain{
    .shader = nullptr, .retroRate = 35, .palette = blockPalette, .scene = blockScene};

// BOOT's and DOTS' chapters show the two test effects; the rest is the registry.
const evo::retro::EffectInfo& testEffects(Effect effect) {
    if (effect == Effect::Boot)
        return kBlockPhosphor;
    if (effect == Effect::Dots)
        return kBlockPlain;
    return evo::retro::effectFor(effect);
}

TimelineFrame frameOf(int chapter, std::uint32_t retroFrame) {
    return evo::show::sampleTimeline(evo::show::chapterStart(chapter) + (retroFrame + 0.5) / 35.0);
}

double toLinear(double c) {
    c /= 255;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double toByte(double linear) {
    const double c = linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
    return 255 * std::clamp(c, 0.0, 1.0);
}

int red(const evo::test::RetroPicture& picture, int x, int y) {
    return picture.rgb[(static_cast<std::size_t>(y) * 320 + static_cast<std::size_t>(x)) * 3];
}

void testRegistry() {
    for (std::size_t e = 0; e < evo::show::kEffectCount; ++e) {
        const auto effect = static_cast<Effect>(e);
        const bool on = effect == Effect::Boot || effect == Effect::Reboot || effect == Effect::StudioNight ||
                        effect == Effect::Return || effect == Effect::Credits || effect == Effect::TheEnd;
        CHECK(evo::retro::effectFor(effect).phosphor == on);
        CHECK(!(evo::retro::effectFor(effect).phosphor && evo::retro::effectFor(effect).grid));
    }
    // 80 ms to 1/e: at 35 frames a second each frame keeps exp(-1 / 2.8) of the last one's light.
    CHECK(std::fabs(evo::retro::phosphorDecay(35) - std::exp(-1.0 / 2.8)) < 1e-12);
    CHECK(std::fabs(evo::retro::phosphorDecay(35) - 0.6997) < 1e-3);
}

void testGhosts(evo::gfx::Context& ctx) {
    evo::test::RetroStageFixture fixture(ctx, true, testEffects);
    const double d = evo::retro::phosphorDecay(35);
    evo::test::RetroPicture last;
    const int y = kBlockY + kBlockSize / 2;
    for (std::uint32_t r = 0; r <= 12; ++r) {
        bool rendered = false;
        last = fixture.renderPicture(frameOf(0, r), &rendered);
        CHECK(rendered);
        // Playing on, only the new frame is drawn: the older ones are held.
        CHECK(fixture.stage().picturesDrawn() == 1);
        for (std::uint32_t k = 0; k <= 4 && k <= r; ++k) {
            const int x = static_cast<int>(r - k) * kBlockStep + kBlockSize / 2;
            const int got = red(last, x, y);
            if (k == 0) {
                CHECK(got == 200);
            } else if (k <= 3) {
                const double want = toByte(std::pow(d, k) * toLinear(200));
                if (std::fabs(got - want) > 2)
                    std::printf("frame %u ghost %u: %d, want %.1f\n", r, k, got, want);
                CHECK(std::fabs(got - want) <= 2);
            } else {
                CHECK(got == 0); // older than three frames: gone
            }
        }
        // The white block's ghost from frame 7 on; at frame 6 it has none (frame 5 had no white block).
        if (r >= 7) {
            const int x = static_cast<int>(r - 1) * kBlockStep + kBlockSize / 2;
            CHECK(std::fabs(red(last, x, kBlockY + 25) - toByte(d)) <= 2);
        }
        if (r == 6)
            CHECK(red(last, 5 * kBlockStep + kBlockSize / 2, kBlockY + 25) == 0);
        // Between two blocks: black.
        CHECK(red(last, static_cast<int>(r) * kBlockStep + kBlockSize + 1, y) == 0);
    }
    // The current frame's indices stay in the index image.
    CHECK(last.at(12 * kBlockStep + 3, kBlockY + 3) == kBlockIndex);
    CHECK(last.at(11 * kBlockStep + 3, kBlockY + 3) == 0);

    // A seek: a fresh stage drawing frame 12 at once gives the same bytes, drawing all four frames.
    {
        evo::test::RetroStageFixture fresh(ctx, true, testEffects);
        const evo::test::RetroPicture seeked = fresh.renderPicture(frameOf(0, 12));
        CHECK(fresh.stage().picturesDrawn() == 4);
        CHECK(seeked.rgb == last.rgb && seeked.indices == last.indices);
        // Back to 3, which it holds none of, and on to 12 again, which it must draw anew.
        fresh.renderPicture(frameOf(0, 3));
        CHECK(fresh.stage().picturesDrawn() == 4);
        const evo::test::RetroPicture again = fresh.renderPicture(frameOf(0, 12));
        CHECK(again.rgb == last.rgb);
        // Frame 13 after 12: one new picture.
        fresh.renderPicture(frameOf(0, 13));
        CHECK(fresh.stage().picturesDrawn() == 1);
    }

    // The same effect without the switch: no ghosts at all.
    evo::test::RetroStageFixture plain(ctx, true, testEffects);
    for (std::uint32_t r = 0; r <= 8; ++r) {
        const evo::test::RetroPicture picture = plain.renderPicture(frameOf(2, r));
        CHECK(plain.stage().picturesDrawn() == 1);
        CHECK(red(picture, static_cast<int>(r) * kBlockStep + 5, y) == 200);
        if (r > 0)
            CHECK(red(picture, static_cast<int>(r - 1) * kBlockStep + 5, y) == 0);
    }
}

// The real registry: a BOOT frame played to equals the frame seeked to, and so does THE END's power-off.
void testShowFrames(evo::gfx::Context& ctx) {
    evo::test::RetroStageFixture played(ctx, true);
    const auto frame = static_cast<std::uint32_t>(4.0 * 35);
    evo::test::RetroPicture picture;
    for (std::uint32_t r = frame - 8; r <= frame; ++r)
        picture = played.renderPicture(frameOf(0, r));
    evo::test::RetroStageFixture seeked(ctx, true);
    CHECK(seeked.renderPicture(frameOf(0, frame)).rgb == picture.rgb);
    const TimelineFrame end = frameOf(30, 0);
    const auto off = static_cast<std::uint32_t>(std::floor(end.accentSeconds * 35));
    for (std::uint32_t r = off - 2; r <= off + 3; ++r)
        picture = played.renderPicture(frameOf(30, r));
    CHECK(seeked.renderPicture(frameOf(30, off + 3)).rgb == picture.rgb);
}

int look(int argc, char** argv) {
    const std::filesystem::path dir = argv[2];
    auto ctx = evo::gfx::Context::create({.validation = false});
    for (int i = 3; i < argc; ++i) {
        const double seconds = std::atof(argv[i]);
        const TimelineFrame frame = evo::show::sampleTimeline(seconds);
        evo::test::RetroStageFixture fixture(ctx, true);
        const std::uint32_t rate = evo::retro::effectFor(frame.effect).retroRate;
        const auto retro = static_cast<std::uint32_t>(std::floor(std::max(frame.localSeconds, 0.0) * rate));
        evo::test::RetroPicture picture;
        for (std::uint32_t back = std::min(retro, 3u) + 1; back-- > 0;)
            picture =
                fixture.renderPicture(evo::show::sampleTimeline(seconds - static_cast<double>(back) / rate));
        char name[64];
        std::snprintf(name, sizeof name, "retro_%07.2f.png", seconds);
        evo::image::writePng(dir / name, 320, 200, picture.rgb.data());
        std::vector<std::uint8_t> big(640 * 400 * 3);
        for (std::size_t y = 0; y < 400; ++y)
            for (std::size_t x = 0; x < 640; ++x)
                for (std::size_t c = 0; c < 3; ++c)
                    big[(y * 640 + x) * 3 + c] = picture.rgb[((y / 2) * 320 + x / 2) * 3 + c];
        std::snprintf(name, sizeof name, "retro2x_%07.2f.png", seconds);
        evo::image::writePng(dir / name, 640, 400, big.data());
        std::printf("%s\n", (dir / name).string().c_str());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        if (argc >= 3 && std::string(argv[1]) == "--look")
            return look(argc, argv);
        testRegistry();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testGhosts(ctx);
            testShowFrames(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("phosphor_test: %d checks passed\n", checksPassed);
    return 0;
}
