// The compositor on the GPU, with the validation layer on (fails on any message it reports):
//   - the visible mask: the retro and modern images are cleared to two distinct solid colours, and every
//     subset of visible bits (none, retro, modern, both) is composited for a Cut in 1993, a Cut in 2026, a
//     Portal half way, a Wipe half way and FINALE in either era. A cleared bit must give black where that
//     image would show, a set bit its colour, pixel by pixel against a CPU model of composite.comp; without
//     the retro bit the CRT pass must not run at all;
//   - the wipe's refractive edge, with the CRT look on a 1993 picture that varies along the line: the columns
//     beyond 3 pixels of the line equal the plain 2026 and 1993 frames byte for byte, the four edge columns
//     differ from both and are not white;
//   - the flash policy (show::eraFlash): kEraFlash on a BeatSwap flip, none in FINALE or a Portal chapter.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "render/compositor.hpp"
#include "show/director.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace {

using evo::render::kModernVisible;
using evo::render::kRetroVisible;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::show::Transition;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr std::uint32_t kWidth = 160, kHeight = 90;
// The 4:3 rectangle the 1993 screen fills at 160x90: 120x90 from x = 20.
constexpr std::uint32_t kRectLeft = 20, kRectRight = 140;

using Rgb = std::array<float, 3>;
constexpr Rgb kRetroColour = {200.f, 40.f, 10.f};
constexpr Rgb kModernColour = {20.f, 120.f, 230.f};

struct Case {
    const char* name;
    Transition transition;
    float era, wipe;
    Effect effect = Effect::Galaxy;
};

constexpr std::array<Case, 6> kCases = {{
    {"cut 1993", Transition::Cut, 0.f, 0.f},
    {"cut 2026", Transition::Cut, 1.f, 1.f},
    {"portal 0.5", Transition::Portal, .5f, 0.f},
    {"wipe 0.5", Transition::Wipe, .5f, .5f},
    {"finale room", Transition::BeatSwap, 0.f, 0.f, Effect::Finale},
    {"finale cut", Transition::BeatSwap, 1.f, 0.f, Effect::Finale},
}};

// composite.comp on the CPU, with the CRT off (the 1993 screen is the retro colour inside the 4:3 rectangle,
// black outside) and no eraCut: what pixel x shows, in 0..255 per channel.
Rgb expected(const Case& c, std::uint32_t visible, std::uint32_t x) {
    const bool inRect = x >= kRectLeft && x < kRectRight;
    const Rgb then = (visible & kRetroVisible) != 0 && inRect ? kRetroColour : Rgb{0.f, 0.f, 0.f};
    const Rgb now = (visible & kModernVisible) != 0 ? kModernColour : Rgb{0.f, 0.f, 0.f};
    if (c.effect == Effect::Finale)
        return now; // the director holds the 2026 picture
    Rgb out{};
    const float centre = static_cast<float>(x) + .5f;
    const float edge = c.wipe * static_cast<float>(kWidth);
    for (int i = 0; i < 3; ++i) {
        switch (c.transition) {
        case Transition::Wipe:
            out[i] = centre < edge ? now[i] : then[i];
            // The glass edge: the 1993 side (one colour here, so the refraction does not show) + 3 %.
            if (c.wipe > 0.f && c.wipe < 1.f && centre - edge >= -2.f && centre - edge < 2.f)
                out[i] = std::min(then[i] * 1.03f, 255.f);
            break;
        case Transition::Portal:
            out[i] = then[i] + (now[i] - then[i]) * std::clamp(c.era, 0.f, 1.f);
            break;
        case Transition::Cut:
        case Transition::BeatSwap:
            out[i] = c.era >= .5f ? now[i] : then[i];
            break;
        }
    }
    return out;
}

void clear(VkCommandBuffer cmd, evo::gfx::Image& image, const Rgb& colour) {
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkClearColorValue value{};
    for (int i = 0; i < 3; ++i)
        value.float32[i] = colour[i] / 255.f;
    value.float32[3] = 1.f;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
}

void testVisibleMask(evo::gfx::Context& ctx) {
    evo::gfx::FrameRing ring(ctx);
    evo::text::TextPass text(ctx, ring);
    evo::gfx::Image retro(ctx,
                          {320, 200, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "test retro"});
    evo::gfx::Image modern(ctx,
                           {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "test modern"});
    evo::gfx::Readback readback(ctx, kWidth, kHeight);
    evo::render::Compositor compositor(ctx, ring, text, kWidth, kHeight);

    // Both first, so the screen image holds the 1993 colour when a later frame without the retro bit could
    // read it by mistake.
    constexpr std::array<std::uint32_t, 4> kSubsets = {kRetroVisible | kModernVisible, 0u, kRetroVisible,
                                                       kModernVisible};
    for (const bool crt : {false, true}) {
        for (const Case& c : kCases) {
            for (const std::uint32_t visible : kSubsets) {
                TimelineFrame frame;
                frame.effect = c.effect;
                frame.transition = c.transition;
                frame.era = c.era;
                frame.wipe = c.wipe;
                frame.eraCut = 0.f;

                const std::uint64_t crtBefore = compositor.crtDispatchCount();
                VkCommandBuffer cmd = ring.begin();
                clear(cmd, retro, kRetroColour);
                clear(cmd, modern, kModernColour);
                compositor.render(cmd, retro, modern, frame, crt, visible);
                readback.record(cmd, compositor.output());
                ring.wait(ring.submit());

                // The CRT pass runs exactly when the 1993 screen may show.
                const std::uint64_t crtRuns = compositor.crtDispatchCount() - crtBefore;
                CHECK(crtRuns == ((visible & kRetroVisible) != 0 ? 1u : 0u));
                if (crt)
                    continue; // the CRT look changes the colours; only its skip is checked

                const std::vector<std::uint8_t> rgb = readback.rgb();
                int worst = 0;
                for (std::uint32_t y = 0; y < kHeight; ++y)
                    for (std::uint32_t x = 0; x < kWidth; ++x) {
                        const Rgb want = expected(c, visible, x);
                        for (int i = 0; i < 3; ++i) {
                            const int got = rgb[(std::size_t{y} * kWidth + x) * 3 + i];
                            worst = std::max(worst, std::abs(got - static_cast<int>(std::lround(want[i]))));
                        }
                    }
                if (worst > 1)
                    std::fprintf(stderr, "%s, visible %u: off by %d\n", c.name, visible, worst);
                CHECK(worst <= 1); // the Portal mix rounds
            }
        }
    }
}

// The images and passes of one compositor at kWidth x kHeight, rendering one frame at a time.
class Bench {
public:
    explicit Bench(evo::gfx::Context& ctx)
        : ring_(ctx), text_(ctx, ring_),
          retro_(ctx, {320, 200, VK_FORMAT_R8G8B8A8_UNORM,
                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "test retro"}),
          modern_(ctx, {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "test modern"}),
          readback_(ctx, kWidth, kHeight), compositor_(ctx, ring_, text_, kWidth, kHeight),
          staging_(ctx,
                   {VkDeviceSize{320} * 200 * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "test staging"}) {
        // A 1993 picture that changes along every row (and down the rows), so a refracted sample shows.
        auto* bytes = static_cast<std::uint8_t*>(staging_.mapped());
        for (std::uint32_t y = 0; y < 200; ++y)
            for (std::uint32_t x = 0; x < 320; ++x) {
                std::uint8_t* p = bytes + (std::size_t{y} * 320 + x) * 4;
                p[0] = static_cast<std::uint8_t>((x * 37 + y * 11) % 256);
                p[1] = static_cast<std::uint8_t>((x * 13 + 90) % 256);
                p[2] = static_cast<std::uint8_t>(x % 2 == 0 ? 30 : 220);
                p[3] = 255;
            }
        staging_.flush();
    }

    std::vector<std::uint8_t> render(const TimelineFrame& frame, bool crt, std::uint32_t visible) {
        VkCommandBuffer cmd = ring_.begin();
        retro_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {320, 200, 1};
        vkCmdCopyBufferToImage(cmd, staging_.handle(), retro_.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);
        clear(cmd, modern_, kModernColour);
        compositor_.render(cmd, retro_, modern_, frame, crt, visible);
        readback_.record(cmd, compositor_.output());
        ring_.wait(ring_.submit());
        return readback_.rgb();
    }

private:
    evo::gfx::FrameRing ring_;
    evo::text::TextPass text_;
    evo::gfx::Image retro_, modern_;
    evo::gfx::Readback readback_;
    evo::render::Compositor compositor_;
    evo::gfx::Buffer staging_;
};

const std::uint8_t* pixel(const std::vector<std::uint8_t>& rgb, std::uint32_t x, std::uint32_t y) {
    return &rgb[(std::size_t{y} * kWidth + x) * 3];
}

bool sameColumn(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, std::uint32_t x) {
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (int c = 0; c < 3; ++c)
            if (pixel(a, x, y)[c] != pixel(b, x, y)[c])
                return false;
    return true;
}

TimelineFrame frameOf(Effect effect, Transition transition, float era, float wipe, float eraCut) {
    TimelineFrame frame;
    frame.effect = effect;
    frame.transition = transition;
    frame.era = era;
    frame.wipe = wipe;
    frame.eraCut = eraCut;
    return frame;
}

// The wipe's line as a refractive edge, at two line positions (on a pixel boundary and between) and with the
// CRT look off and on: beyond 3 pixels of the line the frame is the plain 2026 frame on the left and the
// plain 1993 frame on the right, byte for byte, and so are the columns just outside the 4-pixel strip; the
// strip's columns differ from both and have no white. With the CRT off the strip is checked pixel by pixel:
// the 1993 frame sampled (linearly) (2 - u)^2 / 8 pixels right of the pixel centre u pixels from the line,
// times 1.03.
void testRefractiveEdge(evo::gfx::Context& ctx) {
    Bench bench(ctx);
    constexpr std::uint32_t kBoth = kRetroVisible | kModernVisible;
    for (const bool crt : {false, true}) {
        const std::vector<std::uint8_t> now =
            bench.render(frameOf(Effect::Plasma, Transition::Cut, 1.f, 1.f, 0.f), crt, kBoth);
        const std::vector<std::uint8_t> then =
            bench.render(frameOf(Effect::Plasma, Transition::Cut, 0.f, 0.f, 0.f), crt, kBoth);
        CHECK(now != then);
        for (const float wipe : {.5f, .4375f + .3f / kWidth}) {
            const std::vector<std::uint8_t> split =
                bench.render(frameOf(Effect::Plasma, Transition::Wipe, wipe, wipe, 0.f), crt, kBoth);
            const float edge = wipe * static_cast<float>(kWidth);
            int edgeColumns = 0;
            int worst = 0;
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const float u = static_cast<float>(x) + .5f - edge;
                if (u < -3.f) {
                    CHECK(sameColumn(split, now, x));
                } else if (u >= 3.f) {
                    CHECK(sameColumn(split, then, x));
                } else if (u < -2.f) {
                    CHECK(sameColumn(split, now, x)); // just outside the strip: untouched
                } else if (u >= 2.f) {
                    CHECK(sameColumn(split, then, x));
                } else {
                    ++edgeColumns;
                    CHECK(!sameColumn(split, now, x) && !sameColumn(split, then, x));
                    for (std::uint32_t y = 0; y < kHeight; ++y) {
                        const std::uint8_t* p = pixel(split, x, y);
                        CHECK(!(p[0] == 255 && p[1] == 255 && p[2] == 255));
                        if (crt)
                            continue;
                        const float d = 2.f - u;
                        const float s = static_cast<float>(x) + .5f + d * d / 8.f - .5f;
                        const auto left = static_cast<std::uint32_t>(std::floor(s));
                        const float f = s - std::floor(s);
                        for (int c = 0; c < 3; ++c) {
                            const float a = pixel(then, left, y)[c],
                                        b = pixel(then, std::min(left + 1, kWidth - 1), y)[c];
                            const float want = std::min((a + (b - a) * f) * 1.03f, 255.f);
                            worst = std::max(worst, std::abs(p[c] - static_cast<int>(std::lround(want))));
                        }
                    }
                }
            }
            CHECK(edgeColumns == 4);
            if (worst > 1)
                std::fprintf(stderr, "refractive edge at %.2f: off by %d\n", edge, worst);
            CHECK(worst <= 1);
        }
    }
}

// The flash policy: a BeatSwap flip (DOTS) whitens the frame by kEraFlash * eraCut; FINALE and a Portal
// chapter do not flash at all, whatever eraCut says.
void testFlash(evo::gfx::Context& ctx) {
    Bench bench(ctx);
    const std::vector<std::uint8_t> plain =
        bench.render(frameOf(Effect::Dots, Transition::BeatSwap, 1.f, 0.f, 0.f), false, kModernVisible);
    CHECK(pixel(plain, 0, 0)[0] == 20 && pixel(plain, 0, 0)[1] == 120 && pixel(plain, 0, 0)[2] == 230);
    const auto flashed = [&](float eraCut) {
        const std::vector<std::uint8_t> rgb = bench.render(
            frameOf(Effect::Dots, Transition::BeatSwap, 1.f, 0.f, eraCut), false, kModernVisible);
        return static_cast<int>(pixel(rgb, kWidth / 2, kHeight / 2)[0]);
    };
    CHECK(std::abs(flashed(1.f) - 84) <= 1); // 20 + 0.25 * 255
    CHECK(std::abs(flashed(.5f) - 52) <= 1); // 20 + 0.125 * 255
    CHECK(evo::show::kEraFlash == .25f);
    CHECK(bench.render(frameOf(Effect::Finale, Transition::BeatSwap, 1.f, 0.f, 1.f), false, kModernVisible) ==
          plain);
    CHECK(bench.render(frameOf(Effect::Finale, Transition::BeatSwap, 0.f, 0.f, 1.f), false, kModernVisible) ==
          plain);
    CHECK(bench.render(frameOf(Effect::Flyby, Transition::Portal, 1.f, 0.f, 1.f), false, kModernVisible) ==
          plain);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testVisibleMask(ctx);
            testRefractiveEdge(ctx);
            testFlash(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("compositor_test: %d checks passed\n", checksPassed);
    return 0;
}
