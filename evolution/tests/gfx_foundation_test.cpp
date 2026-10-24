// The render foundation: tracked images, buffers, the frame ring, compute passes and the readback. Runs with
// the validation layer on and fails on any message it reports, so every barrier and descriptor the
// foundation records is checked by the layer as well as by the pixels.

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/fill_pass.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/paths.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace evo::gfx;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

// Points shaderDir() at the build's compiled shaders.
void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

ImageDesc targetDesc(std::uint32_t width, std::uint32_t height, const char* name) {
    return {
        width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        1,     name};
}

// (a) The image remembers its layout and last access, and records a barrier only when one is needed.
void testImageTracksLayout(Context& ctx) {
    Image image(ctx, targetDesc(16, 8, "layout test"));
    FrameRing ring(ctx); // declared after what its frames use, so it waits for them before they go
    CHECK(image.handle() != VK_NULL_HANDLE && image.view() != VK_NULL_HANDLE);
    CHECK(image.layout() == VK_IMAGE_LAYOUT_UNDEFINED);
    CHECK(image.extent().width == 16 && image.extent().height == 8);
    CHECK(image.format() == VK_FORMAT_R8G8B8A8_UNORM);

    VkCommandBuffer cmd = ring.begin();
    CHECK(image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
    CHECK(image.layout() == VK_IMAGE_LAYOUT_GENERAL);
    CHECK(image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                           VK_ACCESS_2_TRANSFER_READ_BIT));
    CHECK(image.layout() == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    // The same read again: nothing to order.
    CHECK(!image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_READ_BIT));
    // A reader at another stage must still wait for the shader's write once.
    CHECK(image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                           VK_ACCESS_2_TRANSFER_READ_BIT));
    CHECK(!image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_READ_BIT));
    CHECK(image.layout() == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    // Write after read and write after write always need a barrier.
    CHECK(image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
    CHECK(image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT));
    ring.wait(ring.submit());

    // Moving keeps the handle and the tracked state; the moved-from image is empty.
    const VkImage handle = image.handle();
    Image moved = std::move(image);
    CHECK(moved.handle() == handle);
    CHECK(moved.layout() == VK_IMAGE_LAYOUT_GENERAL);
    CHECK(image.handle() == VK_NULL_HANDLE); // NOLINT(bugprone-use-after-move): checking the moved-from state
    Image assigned;
    assigned = std::move(moved);
    CHECK(assigned.handle() == handle);
}

std::vector<std::uint8_t> renderOnce(Context& ctx, FrameRing& ring, FillPass& fill, Image& target,
                                     Readback& readback, double seconds) {
    VkCommandBuffer cmd = ring.begin();
    fill.record(cmd, ring, target, evo::show::sampleTimeline(seconds));
    readback.record(cmd, target);
    ring.wait(ring.submit());
    (void)ctx;
    return readback.rgb();
}

struct HeapUsage {
    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};

    explicit HeapUsage(const Context& ctx) { vmaGetHeapBudgets(ctx.allocator(), budgets.data()); }

    bool operator==(const HeapUsage& other) const {
        for (std::size_t i = 0; i < budgets.size(); ++i) {
            const VmaBudget& a = budgets[i];
            const VmaBudget& b = other.budgets[i];
            if (a.usage != b.usage || a.statistics.blockCount != b.statistics.blockCount ||
                a.statistics.allocationCount != b.statistics.allocationCount ||
                a.statistics.blockBytes != b.statistics.blockBytes ||
                a.statistics.allocationBytes != b.statistics.allocationBytes)
                return false;
        }
        return true;
    }
};

// (b) Fifty frames through the ring, two in flight, each into its slot's own image and readback: the same
// time always gives the same pixels, different times different pixels, and nothing grows after frame 2.
void testFrameRingRendersFrames(Context& ctx) {
    constexpr std::uint32_t kWidth = 48, kHeight = 32;
    constexpr int kFrames = 50;
    const std::array<double, 3> times = {0.25, 1.5, evo::show::chapterStart(3) + 0.5};

    std::vector<Image> targets;
    std::vector<Readback> readbacks;
    for (std::uint32_t slot = 0; slot < FrameRing::kFramesInFlight; ++slot) {
        targets.emplace_back(ctx, targetDesc(kWidth, kHeight, "ring target"));
        readbacks.emplace_back(ctx, kWidth, kHeight);
    }
    Image single(ctx, targetDesc(kWidth, kHeight, "single target"));
    Readback singleReadback(ctx, kWidth, kHeight);
    FrameRing ring(ctx);
    FillPass fill(ctx);

    struct Pending {
        std::uint64_t value;
        std::size_t time;
    };
    std::array<std::optional<Pending>, FrameRing::kFramesInFlight> pending;
    std::array<std::optional<std::vector<std::uint8_t>>, 3> reference;
    int checked = 0;
    auto consume = [&](std::uint32_t slot) {
        const Pending done = *pending[slot];
        ring.wait(done.value); // rgb() only after the submit that recorded the copy completed
        std::vector<std::uint8_t> rgb = readbacks[slot].rgb();
        CHECK(rgb.size() == std::size_t{kWidth} * kHeight * 3);
        if (!reference[done.time])
            reference[done.time] = std::move(rgb);
        else
            CHECK(rgb == *reference[done.time]);
        pending[slot].reset();
        ++checked;
    };

    std::optional<HeapUsage> afterSecondFrame;
    for (int i = 0; i < kFrames; ++i) {
        VkCommandBuffer cmd = ring.begin();
        const std::uint32_t slot = ring.slot();
        if (pending[slot])
            consume(slot);
        const std::size_t time = static_cast<std::size_t>(i) % times.size();
        fill.record(cmd, ring, targets[slot], evo::show::sampleTimeline(times[time]));
        readbacks[slot].record(cmd, targets[slot]);
        pending[slot] = Pending{ring.submit(), time};
        if (i == 1)
            afterSecondFrame.emplace(ctx);
    }
    CHECK(HeapUsage(ctx) == *afterSecondFrame);
    ring.waitAll();
    for (std::uint32_t slot = 0; slot < FrameRing::kFramesInFlight; ++slot) {
        if (pending[slot])
            consume(slot);
    }
    CHECK(checked == kFrames);
    CHECK(*reference[0] != *reference[1]);
    CHECK(*reference[1] != *reference[2]);
    CHECK(*reference[0] != *reference[2]);
    CHECK(targets[0].layout() == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // A lone render through the same pass matches the pipelined one.
    CHECK(renderOnce(ctx, ring, fill, single, singleReadback, times[1]) == *reference[1]);
    std::printf("frame ring: %d frames read back, %zu bytes each\n", checked, reference[0]->size());
}

// The descriptor pools grow when a frame binds more sets than one pool holds, are reset when the ring
// comes back to their slot, and are reused rather than added to on later frames.
void testDescriptorPoolsAreRecycled(Context& ctx) {
    const std::array<Binding, 1> bindings = {Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}};
    Image image(ctx, targetDesc(8, 8, "pool test"));
    ComputePipeline pipeline(ctx, shaderDir() / "fill.comp.spv", bindings, 32);
    FrameRing ring(ctx);
    const std::array<std::uint32_t, 8> push = {8, 8, 0, 0, 0, 0, 0, 0};

    auto frame = [&](int binds) {
        VkCommandBuffer cmd = ring.begin();
        image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const VkDescriptorImageInfo info{VK_NULL_HANDLE, image.view(), VK_IMAGE_LAYOUT_GENERAL};
        for (int i = 0; i < binds; ++i) {
            pipeline.bind(cmd, ring, {&info, 1}, {});
            pipeline.push(cmd, push.data(), sizeof push);
            pipeline.dispatch(cmd, 8, 8);
            image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                             VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        }
        return ring.submit();
    };

    frame(200); // more sets than one 64-set pool holds
    std::printf("descriptor pools after 200 binds in one frame: %zu\n", pipeline.descriptorPoolCount());
    CHECK(pipeline.descriptorPoolCount() >= 4);
    frame(3); // the other slot's first pool
    const std::size_t grown = pipeline.descriptorPoolCount();
    for (int i = 0; i < 20; ++i)
        frame(i % 2 == 0 ? 200 : 3); // the big slot reuses its pools, the small one never needs a second
    CHECK(pipeline.descriptorPoolCount() == grown);
    ring.waitAll();

    // Wrong resource counts are refused before anything is recorded.
    VkCommandBuffer cmd = ring.begin();
    bool threw = false;
    try {
        pipeline.bind(cmd, ring, {}, {});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        pipeline.push(cmd, push.data(), 64);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
    ring.wait(ring.submit());

    // A pipeline belongs to the ring it was first bound with.
    FrameRing other(ctx);
    cmd = other.begin();
    const VkDescriptorImageInfo info{VK_NULL_HANDLE, image.view(), VK_IMAGE_LAYOUT_GENERAL};
    threw = false;
    try {
        pipeline.bind(cmd, other, {&info, 1}, {});
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
    other.wait(other.submit());
}

// (c) A host-visible buffer written on the CPU, copied through a device-local buffer and back.
void testBufferRoundTrip(Context& ctx) {
    constexpr VkDeviceSize kSize = 256;
    Buffer upload(ctx, {kSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "upload"});
    Buffer device(ctx, {kSize,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        false, "device"});
    Buffer download(ctx, {kSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "download"});
    FrameRing ring(ctx);
    CHECK(upload.mapped() != nullptr && download.mapped() != nullptr);
    CHECK(device.mapped() == nullptr);
    CHECK(device.size() == kSize);
    CHECK(device.address() != 0);

    auto* bytes = static_cast<std::uint8_t*>(upload.mapped());
    for (VkDeviceSize i = 0; i < kSize; ++i)
        bytes[i] = static_cast<std::uint8_t>(i * 7 + 3);
    upload.flush();

    VkCommandBuffer cmd = ring.begin();
    const VkBufferCopy region{0, 0, kSize};
    vkCmdCopyBuffer(cmd, upload.handle(), device.handle(), 1, &region);
    bufferBarrier2(cmd, device.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    vkCmdCopyBuffer(cmd, device.handle(), download.handle(), 1, &region);
    bufferBarrier2(cmd, download.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    ring.wait(ring.submit());
    download.invalidate();

    const auto* back = static_cast<const std::uint8_t*>(download.mapped());
    bool same = true;
    for (VkDeviceSize i = 0; i < kSize; ++i)
        same &= back[i] == static_cast<std::uint8_t>(i * 7 + 3);
    CHECK(same);

    // Moving hands over the buffer.
    const VkBuffer handle = download.handle();
    Buffer moved = std::move(download);
    CHECK(moved.handle() == handle && moved.mapped() == back);
    CHECK(download.handle() == VK_NULL_HANDLE); // NOLINT(bugprone-use-after-move)
}

void testMisuseIsRefused(Context& ctx) {
    Image image(ctx, targetDesc(16, 8, "mismatch"));
    Readback readback(ctx, 8, 8);
    FrameRing ring(ctx);
    bool threw = false;
    try {
        ring.wait(1); // never submitted: waiting would hang
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        ring.submit(); // nothing recording
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);

    // A readback copies the whole image, so the sizes must match.
    VkCommandBuffer cmd = ring.begin();
    threw = false;
    try {
        readback.record(cmd, image);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(image.layout() == VK_IMAGE_LAYOUT_UNDEFINED); // refused before recording anything
    ring.wait(ring.submit());
}

void testSamplers(Context& ctx) {
    CHECK(ctx.nearestSampler() != VK_NULL_HANDLE);
    CHECK(ctx.linearSampler() != VK_NULL_HANDLE);
    CHECK(ctx.nearestSampler() != ctx.linearSampler());
}

} // namespace

int main() {
    try {
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        {
            auto ctx = Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testImageTracksLayout(ctx);
            testFrameRingRendersFrames(ctx);
            testDescriptorPoolsAreRecycled(ctx);
            testBufferRoundTrip(ctx);
            testMisuseIsRefused(ctx);
            testSamplers(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        // Also counts what the layer reported while the objects and the Context were destroyed.
        CHECK(validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("gfx_foundation_test: %d checks passed\n", checksPassed);
    return 0;
}
