// The compositor's visible mask on the GPU: the retro and modern images are cleared to two distinct solid
// colours, and every subset of visible bits (none, retro, modern, both) is composited for a Cut in 1993, a
// Cut in 2026, a Portal half way and a Wipe half way. A cleared bit must give black where that image would
// show, a set bit its colour, pixel by pixel against a CPU model of composite.comp; without the retro bit the
// CRT pass must not run at all. Runs with the validation layer on and fails on any message it reports.

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "render/compositor.hpp"
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
};

constexpr std::array<Case, 4> kCases = {{
    {"cut 1993", Transition::Cut, 0.f, 0.f},
    {"cut 2026", Transition::Cut, 1.f, 1.f},
    {"portal 0.5", Transition::Portal, .5f, 0.f},
    {"wipe 0.5", Transition::Wipe, .5f, .5f},
}};

// composite.comp on the CPU, with the CRT off (the 1993 screen is the retro colour inside the 4:3 rectangle,
// black outside) and no eraCut: what pixel x shows, in 0..255 per channel.
Rgb expected(const Case& c, std::uint32_t visible, std::uint32_t x) {
    const bool inRect = x >= kRectLeft && x < kRectRight;
    const Rgb then = (visible & kRetroVisible) != 0 && inRect ? kRetroColour : Rgb{0.f, 0.f, 0.f};
    const Rgb now = (visible & kModernVisible) != 0 ? kModernColour : Rgb{0.f, 0.f, 0.f};
    Rgb out{};
    const float centre = static_cast<float>(x) + .5f;
    const float edge = c.wipe * static_cast<float>(kWidth);
    for (int i = 0; i < 3; ++i) {
        switch (c.transition) {
        case Transition::Wipe:
            out[i] = centre < edge ? now[i] : then[i];
            if (c.wipe > 0.f && c.wipe < 1.f && centre - edge >= -1.f && centre - edge < 1.f)
                out[i] = 255.f; // the seam
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

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testVisibleMask(ctx);
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
