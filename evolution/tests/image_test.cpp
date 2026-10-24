// Images with mip levels, depth formats and sampled views, and the retro colour image as a mipmapped
// texture. Runs with the validation layer on (and its synchronization validation where the layer has it, so
// the per-level barriers of generateMips are checked too) and fails on any message it reports.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/sync.hpp"
#include "render/retro_stage.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include "test_env.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <stdexcept>
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

constexpr std::uint32_t kWidth = 320, kHeight = 200;
// Every staging and readback buffer the test makes, kept until the end: the synchronization validation of
// layer 1.3.275 does not credit host waits, so a buffer whose memory VMA reuses after a freed one reads as
// a hazard (see the step 4 Task 2 report); keeping them alive keeps the layer's reports to real ones.
std::deque<Buffer> retained;

constexpr VkImageUsageFlags kMipUsage =
    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

std::uint32_t levelSize(std::uint32_t size, std::uint32_t level) {
    return std::max(1u, size >> level);
}

// One mip level's texels, read back after the frame that copied them completed.
struct Level {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> bytes; // 4 bytes a texel

    const std::uint8_t* at(std::uint32_t x, std::uint32_t y) const {
        return &bytes[(std::size_t{y} * width + x) * 4];
    }
};

// Copies levels of an image into host buffers in one frame: records the copies, submits and waits.
class LevelReader {
public:
    LevelReader(Context& ctx, FrameRing& ring) : ctx_(ctx), ring_(ring) {}

    std::vector<Level> read(Image& image, const std::vector<std::uint32_t>& levels,
                            VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
        VkCommandBuffer cmd = ring_.begin();
        const std::vector<const Buffer*> buffers = record(cmd, image, levels, aspect);
        ring_.wait(ring_.submit());
        return collect(image, levels, buffers);
    }

    std::vector<const Buffer*> record(VkCommandBuffer cmd, Image& image,
                                      const std::vector<std::uint32_t>& levels,
                                      VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
        image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_READ_BIT);
        std::vector<const Buffer*> buffers;
        for (const std::uint32_t level : levels) {
            const std::uint32_t w = levelSize(image.extent().width, level);
            const std::uint32_t h = levelSize(image.extent().height, level);
            Buffer& buffer = retained.emplace_back(
                ctx_, BufferDesc{VkDeviceSize{w} * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "level"});
            VkBufferImageCopy region{};
            region.imageSubresource = {aspect, level, 0, 1};
            region.imageExtent = {w, h, 1};
            vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle(),
                                   1, &region);
            bufferBarrier2(cmd, buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
            buffers.push_back(&buffer);
        }
        return buffers;
    }

    static std::vector<Level> collect(const Image& image, const std::vector<std::uint32_t>& levels,
                                      const std::vector<const Buffer*>& buffers) {
        std::vector<Level> out;
        for (std::size_t i = 0; i < levels.size(); ++i) {
            buffers[i]->invalidate();
            const auto* data = static_cast<const std::uint8_t*>(buffers[i]->mapped());
            out.push_back({levelSize(image.extent().width, levels[i]),
                           levelSize(image.extent().height, levels[i]),
                           {data, data + buffers[i]->size()}});
        }
        return out;
    }

private:
    Context& ctx_;
    FrameRing& ring_;
};

// Fills level 0 with RGBA texels from a function of (x, y), through a staging copy.
template <typename Texel> void upload(Context& ctx, Image& image, Texel texel) {
    const std::uint32_t w = image.extent().width, h = image.extent().height;
    Buffer& staging = retained.emplace_back(
        ctx, BufferDesc{VkDeviceSize{w} * h * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "staging"});
    auto* bytes = static_cast<std::uint8_t*>(staging.mapped());
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::uint8_t value = texel(x, y);
            std::uint8_t* p = bytes + (std::size_t{y} * w + x) * 4;
            p[0] = p[1] = p[2] = value;
            p[3] = 255;
        }
    staging.flush();
    ctx.immediate([&](VkCommandBuffer cmd) { // setup
        image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(cmd, staging.handle(), image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
    });
}

void mip(FrameRing& ring, Image& image) {
    VkCommandBuffer cmd = ring.begin();
    image.generateMips(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    ring.wait(ring.submit());
}

// Every texel's RGB in [lo, hi].
bool allWithin(const Level& level, int lo, int hi) {
    for (std::uint32_t y = 0; y < level.height; ++y)
        for (std::uint32_t x = 0; x < level.width; ++x)
            for (int c = 0; c < 3; ++c)
                if (level.at(x, y)[c] < lo || level.at(x, y)[c] > hi) {
                    std::fprintf(stderr, "texel (%u, %u) of a %ux%u level is %d, not in [%d, %d]\n", x, y,
                                 level.width, level.height, level.at(x, y)[c], lo, hi);
                    return false;
                }
    return true;
}

double srgbToLinear(double encoded) {
    return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double linear) {
    return linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

void testMipLevelsFor() {
    CHECK(mipLevelsFor(320, 200) == 9);
    CHECK(mipLevelsFor(1, 1) == 1);
    CHECK(mipLevelsFor(3840, 2160) == 12);
    CHECK(mipLevelsFor(2, 1) == 2);
    CHECK(mipLevelsFor(1, 1024) == 11);
}

void testRejectsTooManyLevels(Context& ctx) {
    bool threw = false;
    try {
        Image image(ctx, {4, 4, VK_FORMAT_R8G8B8A8_UNORM, kMipUsage, 4, "too many levels"});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// A one-pixel black/white checker in a linear (UNORM) image: every 2x2 block averages to grey, so level 1
// and every level after it are 127-128 grey. The tracked state afterwards: GENERAL, the requested read
// already ordered, a later write still waits.
void testCheckerMips(Context& ctx, FrameRing& ring) {
    Image image(ctx, {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM, kMipUsage | VK_IMAGE_USAGE_STORAGE_BIT,
                      mipLevelsFor(kWidth, kHeight), "checker"});
    CHECK(image.mipLevels() == 9);
    CHECK(image.view() != VK_NULL_HANDLE && image.sampledView() != VK_NULL_HANDLE);
    CHECK(image.view() != image.sampledView());
    CHECK(image.aspect() == VK_IMAGE_ASPECT_COLOR_BIT);
    upload(ctx, image, [](std::uint32_t x, std::uint32_t y) { return (x + y) % 2 == 0 ? 0 : 255; });
    const std::uint64_t before = image.mipGenerations();
    mip(ring, image);
    CHECK(image.mipGenerations() == before + 1);
    CHECK(image.layout() == VK_IMAGE_LAYOUT_GENERAL);

    VkCommandBuffer cmd = ring.begin();
    CHECK(!image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT));
    CHECK(image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_STORAGE_READ_BIT));
    ring.wait(ring.submit());

    LevelReader reader(ctx, ring);
    const std::vector<Level> levels = reader.read(image, {0, 1, 4, 8});
    CHECK(levels[0].at(0, 0)[0] == 0 && levels[0].at(1, 0)[0] == 255); // level 0 untouched
    CHECK(levels[1].width == 160 && levels[1].height == 100 && allWithin(levels[1], 127, 128));
    CHECK(levels[2].width == 20 && levels[2].height == 12 && allWithin(levels[2], 127, 128));
    CHECK(levels[3].width == 1 && levels[3].height == 1 && allWithin(levels[3], 127, 128));
}

// Left half white, right half black: the 1x1 level is the mean of level 0 within 2.
void testHalvesMips(Context& ctx, FrameRing& ring) {
    Image image(ctx, {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM, kMipUsage, 9, "halves"});
    upload(ctx, image, [](std::uint32_t x, std::uint32_t) { return x < kWidth / 2 ? 255 : 0; });
    mip(ring, image);
    LevelReader reader(ctx, ring);
    const std::vector<Level> levels = reader.read(image, {8});
    std::printf("halves: 1x1 level %d (mean of level 0: 127.5)\n", levels[0].at(0, 0)[0]);
    CHECK(allWithin(levels[0], 126, 129));
}

// An sRGB image with a UNORM storage view: the storage view sees the encoded bytes as they are, while the
// mips (and sampledView) average in linear light, so a black/white checker mips to the linear mid grey,
// encoded about 188, not to 128.
void testSrgbMips(Context& ctx, FrameRing& ring) {
    ImageDesc desc{kWidth, kHeight,        VK_FORMAT_R8G8B8A8_SRGB, kMipUsage | VK_IMAGE_USAGE_STORAGE_BIT,
                   9,      "srgb checker", VK_FORMAT_R8G8B8A8_UNORM};
    Image image(ctx, desc);
    CHECK(image.format() == VK_FORMAT_R8G8B8A8_SRGB && image.viewFormat() == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(image.view() != image.sampledView());
    upload(ctx, image, [](std::uint32_t x, std::uint32_t y) { return (x + y) % 2 == 0 ? 0 : 255; });
    mip(ring, image);
    LevelReader reader(ctx, ring);
    const std::vector<Level> levels = reader.read(image, {0, 1, 8});
    const int mid = static_cast<int>(std::lround(255.0 * linearToSrgb(0.5)));
    std::printf("srgb: level 1 %d, 1x1 level %d (linear mid grey encodes to %d)\n", levels[1].at(0, 0)[0],
                levels[2].at(0, 0)[0], mid);
    CHECK(mid == 188);
    CHECK(levels[0].at(0, 0)[0] == 0 && levels[0].at(1, 0)[0] == 255);
    CHECK(allWithin(levels[1], mid - 1, mid + 1));
    CHECK(allWithin(levels[2], mid - 2, mid + 2));

    // A one-level sRGB image still has its own UNORM storage view.
    desc.mipLevels = 1;
    Image single(ctx, desc);
    CHECK(single.view() != single.sampledView());
    VkCommandBuffer cmd = ring.begin(); // nothing to blit: only the transition
    single.generateMips(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    ring.wait(ring.submit());
    CHECK(single.layout() == VK_IMAGE_LAYOUT_GENERAL && single.mipGenerations() == 1);
}

// generateMips needs a format that can be blitted with a linear filter: an integer format is refused before
// anything is recorded.
void testRefusesUnfilterableFormat(Context& ctx, FrameRing& ring) {
    Image image(ctx, {64, 64, VK_FORMAT_R8_UINT, kMipUsage | VK_IMAGE_USAGE_STORAGE_BIT, 7, "uint"});
    VkCommandBuffer cmd = ring.begin();
    bool threw = false;
    try {
        image.generateMips(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(image.layout() == VK_IMAGE_LAYOUT_UNDEFINED && image.mipGenerations() == 0);
    ring.wait(ring.submit());

    // Without the transfer usages there is nothing to blit with either.
    Image noTransfer(ctx, {64, 64, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 7, "no transfer"});
    cmd = ring.begin();
    threw = false;
    try {
        noTransfer.generateMips(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
    ring.wait(ring.submit());
}

// A D32_SFLOAT depth image: the depth aspect, its views, a depth attachment layout, and a clear to 1.0 read
// back. The other depth formats report their aspects.
void testDepth(Context& ctx, FrameRing& ring) {
    Image depth(ctx, {64, 32, VK_FORMAT_D32_SFLOAT,
                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                      1, "depth"});
    CHECK(depth.aspect() == VK_IMAGE_ASPECT_DEPTH_BIT);
    CHECK(depth.view() != VK_NULL_HANDLE && depth.sampledView() == depth.view());

    VkCommandBuffer cmd = ring.begin();
    CHECK(depth.transition(cmd, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                           VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT));
    CHECK(depth.layout() == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    depth.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkClearDepthStencilValue clear{1.f, 0};
    const VkImageSubresourceRange range = subresourceRange(depth.aspect());
    vkCmdClearDepthStencilImage(cmd, depth.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    LevelReader reader(ctx, ring);
    const std::vector<const Buffer*> buffers = reader.record(cmd, depth, {0}, VK_IMAGE_ASPECT_DEPTH_BIT);
    ring.wait(ring.submit());
    const std::vector<Level> levels = LevelReader::collect(depth, {0}, buffers);
    bool allOne = true;
    for (std::size_t i = 0; i < levels[0].bytes.size(); i += 4) {
        float value = 0.f;
        std::memcpy(&value, &levels[0].bytes[i], 4);
        allOne = allOne && value == 1.f;
    }
    CHECK(allOne);

    const VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    CHECK(Image(ctx, {8, 8, VK_FORMAT_D16_UNORM, usage, 1, "d16"}).aspect() == VK_IMAGE_ASPECT_DEPTH_BIT);
    CHECK(Image(ctx, {8, 8, VK_FORMAT_D24_UNORM_S8_UINT, usage, 1, "d24s8"}).aspect() ==
          (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
    CHECK(Image(ctx, {8, 8, VK_FORMAT_D32_SFLOAT_S8_UINT, usage, 1, "d32s8"}).aspect() ==
          (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
}

// PLASMA's 1993 picture at a retro frame (35 a second): chapter 3, era 0.
evo::show::TimelineFrame plasmaFrame(std::uint32_t retroFrame) {
    evo::show::TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(3) + 1.0);
    CHECK(frame.effect == evo::show::Effect::Plasma);
    frame.localSeconds = (retroFrame + .5) / 35.0;
    frame.showSeconds = evo::show::chapterStart(3) + frame.localSeconds;
    return frame;
}

// The retro stage's colour image: 9 levels, sampled in linear light, re-mipped with every new retro frame
// and only then. After a PLASMA frame, level 1 is the linear-light mean of each 2x2 block of level 0 and the
// 1x1 level the linear-light mean of level 0, each re-encoded.
void testRetroStageMips(Context& ctx, FrameRing& ring) {
    evo::text::TextPass text(ctx, ring);
    evo::render::RetroStage stage(ctx, ring, text);
    Image& colour = stage.image();
    CHECK(colour.mipLevels() == evo::render::kRetroMipLevels && colour.mipLevels() == 9);
    CHECK(colour.extent().width == kWidth && colour.extent().height == kHeight);
    CHECK(colour.viewFormat() == VK_FORMAT_R8G8B8A8_UNORM && colour.format() == VK_FORMAT_R8G8B8A8_SRGB);

    LevelReader reader(ctx, ring);
    const auto renderAndRead = [&](const evo::show::TimelineFrame& frame, bool& rendered) {
        VkCommandBuffer cmd = ring.begin();
        rendered = stage.render(cmd, frame);
        const std::vector<const Buffer*> buffers = reader.record(cmd, colour, {0, 1, 8});
        ring.wait(ring.submit());
        return LevelReader::collect(colour, {0, 1, 8}, buffers);
    };

    bool rendered = false;
    const std::vector<Level> levels = renderAndRead(plasmaFrame(70), rendered);
    CHECK(rendered && colour.mipGenerations() == 1);

    int worstBlock = 0, worstMean = 0;
    double sum[3] = {0, 0, 0};
    for (std::uint32_t y = 0; y < kHeight; ++y)
        for (std::uint32_t x = 0; x < kWidth; ++x)
            for (int c = 0; c < 3; ++c)
                sum[c] += srgbToLinear(levels[0].at(x, y)[c] / 255.0);
    for (int c = 0; c < 3; ++c) {
        const double mean = 255.0 * linearToSrgb(sum[c] / (kWidth * kHeight));
        worstMean =
            std::max(worstMean, static_cast<int>(std::lround(std::abs(levels[2].at(0, 0)[c] - mean))));
    }
    for (std::uint32_t y = 0; y < kHeight / 2; ++y)
        for (std::uint32_t x = 0; x < kWidth / 2; ++x)
            for (int c = 0; c < 3; ++c) {
                double block = 0;
                for (std::uint32_t dy = 0; dy < 2; ++dy)
                    for (std::uint32_t dx = 0; dx < 2; ++dx)
                        block += srgbToLinear(levels[0].at(2 * x + dx, 2 * y + dy)[c] / 255.0);
                const double want = 255.0 * linearToSrgb(block / 4);
                worstBlock = std::max(
                    worstBlock, static_cast<int>(std::ceil(std::abs(levels[1].at(x, y)[c] - want) - 1e-9)));
            }
    std::printf("retro mips: level 1 within %d of the linear 2x2 means, 1x1 within %d of the linear mean\n",
                worstBlock, worstMean);
    CHECK(worstBlock <= 1);
    CHECK(worstMean <= 3);

    // The same retro frame again: cached, no blit. The next one: mipped once more.
    renderAndRead(plasmaFrame(70), rendered);
    CHECK(!rendered && colour.mipGenerations() == 1);
    renderAndRead(plasmaFrame(71), rendered);
    CHECK(rendered && colour.mipGenerations() == 2);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        testMipLevelsFor();
        {
            auto ctx = Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("warning: synchronization validation unavailable\n");
            FrameRing ring(ctx);
            testRejectsTooManyLevels(ctx);
            testCheckerMips(ctx, ring);
            testHalvesMips(ctx, ring);
            testSrgbMips(ctx, ring);
            testRefusesUnfilterableFormat(ctx, ring);
            testDepth(ctx, ring);
            testRetroStageMips(ctx, ring);
            ring.waitAll(); // shutdown
            retained.clear();
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("image_test: %d checks passed\n", checksPassed);
    return 0;
}
