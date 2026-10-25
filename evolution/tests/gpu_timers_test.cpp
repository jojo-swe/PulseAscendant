// GPU timestamp scopes (trace resolution and the temporal upscaler): a query pool per ring slot, read when the ring reuses the slot.
// Nested scopes come back in the order they began, each non-negative and finite, an outer scope at least as
// long as each scope inside it; the frame read last is the newest completed one; collect() after waitAll()
// reads the frames still pending, oldest first; a disabled timer set (as on a queue without timestamps)
// records nothing and reports nothing; misuse throws. With the validation layer on; fails on any message.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/gpu_timers.hpp"
#include "gfx/sync.hpp"

#include "test_env.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

template <typename Exception, typename F> bool throws(F&& f) {
    try {
        f();
    } catch (const Exception& error) {
        std::printf("  refused: %s\n", error.what());
        return true;
    }
    return false;
}

// Some GPU work to time: a few fills of a buffer, ordered by barriers.
void work(VkCommandBuffer cmd, const gfx::Buffer& buffer, int fills) {
    for (int i = 0; i < fills; ++i) {
        gfx::bufferBarrier2(cmd, buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdFillBuffer(cmd, buffer.handle(), 0, VK_WHOLE_SIZE, static_cast<std::uint32_t>(i));
    }
}

void checkScopes(const gfx::GpuTimers::Scopes& scopes) {
    CHECK(scopes.size() == 3);
    CHECK(scopes[0].first == "total" && scopes[1].first == "first" && scopes[2].first == "second");
    for (const auto& [name, ms] : scopes)
        CHECK(std::isfinite(ms) && ms >= 0.0);
    CHECK(scopes[0].second >= scopes[1].second && scopes[0].second >= scopes[2].second);
}

void testScopes(gfx::Context& ctx) {
    gfx::Buffer buffer(ctx, {1 << 20, VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, "timed"});
    gfx::FrameRing ring(ctx);
    gfx::GpuTimers timers(ctx, ring, 4);
    std::printf("timestamps: %u valid bits, %.3f ns per tick, %s\n", timers.validBits(), timers.periodNs(),
                timers.enabled() ? "enabled" : "unsupported");
    // The queue family's own report decides: a queue with timestamps must give an enabled set.
    std::uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice(), &families, nullptr);
    std::vector<VkQueueFamilyProperties> properties(families);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice(), &families, properties.data());
    VkPhysicalDeviceProperties device{};
    vkGetPhysicalDeviceProperties(ctx.physicalDevice(), &device);
    const std::uint32_t bits = properties[ctx.queueFamily()].timestampValidBits;
    CHECK(timers.validBits() == bits);
    CHECK(timers.periodNs() == static_cast<double>(device.limits.timestampPeriod));
    CHECK(timers.enabled() == (bits > 0 && device.limits.timestampPeriod > 0.f));
    if (!timers.enabled()) {
        std::printf("warning: the queue has no timestamps; only the disabled path is checked\n");
        return;
    }
    CHECK(timers.lastCompleted().empty());
    CHECK(throws<std::logic_error>([&] { timers.begin(VK_NULL_HANDLE, "outside a frame"); }));

    for (int frame = 0; frame < 5; ++frame) {
        VkCommandBuffer cmd = ring.begin();
        // Frame k reads the slot of frame k - 2 (two frames in flight).
        CHECK(timers.lastCompleted().empty() == (frame < 2));
        if (frame >= 2)
            checkScopes(timers.lastCompleted());
        timers.begin(cmd, "total");
        timers.begin(cmd, "first");
        work(cmd, buffer, 4);
        timers.end(cmd);
        timers.begin(cmd, "second");
        work(cmd, buffer, 8);
        timers.end(cmd);
        timers.end(cmd);
        CHECK(throws<std::logic_error>([&] { timers.end(cmd); })); // nothing open
        ring.submit();
    }
    std::vector<gfx::GpuTimers::Scopes> completed = timers.takeCompleted();
    CHECK(completed.size() == 3); // frames 0, 1 and 2, read by the begin()s of frames 2, 3 and 4
    CHECK(timers.takeCompleted().empty());
    ring.waitAll();
    timers.collect(); // frames 3 and 4
    completed = timers.takeCompleted();
    CHECK(completed.size() == 2);
    for (const auto& scopes : completed)
        checkScopes(scopes);
    CHECK(timers.lastCompleted() == completed.back());
    timers.collect(); // nothing left
    CHECK(timers.takeCompleted().empty());

    // More scopes than the pool holds: refused, and the frame still submits.
    VkCommandBuffer cmd = ring.begin();
    for (int i = 0; i < 4; ++i) {
        timers.begin(cmd, "scope");
        timers.end(cmd);
    }
    CHECK(throws<std::length_error>([&] { timers.begin(cmd, "fifth"); }));
    ring.submit();
    ring.waitAll();
    timers.collect();
    CHECK(timers.lastCompleted().size() == 4);
}

// Disabled (the queue family reports no timestamp bits, or the caller turned them off): no queries, nothing
// reported, no validation message.
void testDisabled(gfx::Context& ctx) {
    gfx::FrameRing ring(ctx);
    gfx::GpuTimers timers(ctx, ring, 4, false);
    CHECK(!timers.enabled());
    for (int frame = 0; frame < 3; ++frame) {
        VkCommandBuffer cmd = ring.begin();
        timers.begin(cmd, "total");
        timers.end(cmd);
        ring.submit();
    }
    ring.waitAll();
    timers.collect();
    CHECK(timers.lastCompleted().empty() && timers.takeCompleted().empty());
}

} // namespace

int main() {
    try {
        test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        {
            auto ctx = gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the results are checked\n");
            testScopes(ctx);
            testDisabled(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("gpu_timers_test: %d checks passed\n", checksPassed);
    return 0;
}
