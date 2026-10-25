#include "post/upscale.hpp"

#include "gfx/paths.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace evo::post {

namespace {

constexpr VkFormat kHalfFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kDepthFormat = VK_FORMAT_R32_SFLOAT;
// The jitter sequence's length: Halton (2, 3) points 0..15.
constexpr std::uint32_t kJitterFrames = 16;

// Must match the push constant block of shaders/post/upscale.comp.
struct UpscalePush {
    std::uint32_t inWidth, inHeight, outWidth, outHeight;
    float jitterX, jitterY, feedback, clipGamma;
    std::uint32_t flags;
};
static_assert(sizeof(UpscalePush) == 36);

constexpr std::uint32_t kFlagIdentity = 1, kFlagHistory = 2, kFlagEmission = 4;

constexpr std::array<gfx::Binding, 7> kBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, gfx::Binding{3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, gfx::Binding{5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
};

float halton(std::uint32_t index, std::uint32_t base) {
    float result = 0.f, fraction = 1.f;
    for (; index > 0; index /= base) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
    }
    return result;
}

void checkSizes(std::uint32_t inWidth, std::uint32_t inHeight, std::uint32_t outWidth,
                std::uint32_t outHeight) {
    if (inWidth == 0 || inHeight == 0 || outWidth == 0 || outHeight == 0)
        throw std::invalid_argument("Upscaler: the sizes must be positive");
    if (outWidth < inWidth || outHeight < inHeight)
        throw std::invalid_argument("Upscaler: the output " + std::to_string(outWidth) + "x" +
                                    std::to_string(outHeight) + " is smaller than the input " +
                                    std::to_string(inWidth) + "x" + std::to_string(inHeight));
}

// The shader path, after the sizes are checked (before the pipeline is built).
std::filesystem::path checkedShader(std::uint32_t inWidth, std::uint32_t inHeight, std::uint32_t outWidth,
                                    std::uint32_t outHeight) {
    checkSizes(inWidth, inHeight, outWidth, outHeight);
    return gfx::shaderDir() / "post_upscale.comp.spv";
}

void checkImage(const gfx::Image& image, const char* what, VkFormat format, std::uint32_t width,
                std::uint32_t height) {
    if (image.format() != format || image.viewFormat() != format)
        throw std::invalid_argument(std::string("Upscaler: the ") + what + " image has format " +
                                    std::to_string(image.viewFormat()) + ", expected " +
                                    std::to_string(format));
    if (image.extent().width != width || image.extent().height != height)
        throw std::invalid_argument(std::string("Upscaler: the ") + what + " image is " +
                                    std::to_string(image.extent().width) + "x" +
                                    std::to_string(image.extent().height) + ", expected " +
                                    std::to_string(width) + "x" + std::to_string(height));
}

void checkSettings(const UpscaleSettings& s) {
    if (!(s.feedback >= 0.f && s.feedback < 1.f))
        throw std::invalid_argument("Upscaler: feedback must be in [0, 1)");
    if (!(s.clipGamma > 0.f && std::isfinite(s.clipGamma)))
        throw std::invalid_argument("Upscaler: clipGamma must be positive and finite");
    for (const float j : s.jitter)
        if (!(std::fabs(j) <= 0.5f))
            throw std::invalid_argument("Upscaler: a jitter must be within half a pixel");
}

VkDescriptorImageInfo storage(const gfx::Image& image) {
    return {VK_NULL_HANDLE, image.view(), VK_IMAGE_LAYOUT_GENERAL};
}

void prepare(VkCommandBuffer cmd, gfx::Image& image, VkAccessFlags2 access) {
    image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, access);
}

} // namespace

SubpixelWindow subpixelWindow(std::uint32_t inWidth, std::uint32_t inHeight, std::uint32_t outWidth,
                              std::uint32_t outHeight, std::uint32_t framesSinceReset, bool temporal) {
    SubpixelWindow window;
    if (!temporal || framesSinceReset == 0 || (inWidth == outWidth && inHeight == outHeight) ||
        outWidth == 0 || outHeight == 0)
        return window;
    // Halton (2, 3) points 0..15, each axis shifted by 0.5 - its mean over the cycle so that the jitter
    // averages to 0 (every point stays inside [0, 1]).
    const std::uint32_t k = framesSinceReset % kJitterFrames;
    std::array<float, 2> point{halton(k, 2), halton(k, 3)};
    for (int a = 0; a < 2; ++a) {
        float mean = 0.f;
        for (std::uint32_t i = 0; i < kJitterFrames; ++i)
            mean += halton(i, a == 0 ? 2 : 3);
        point[a] += 0.5f - mean / static_cast<float>(kJitterFrames);
    }
    const std::array<float, 2> ratio{static_cast<float>(inWidth) / static_cast<float>(outWidth),
                                     static_cast<float>(inHeight) / static_cast<float>(outHeight)};
    for (int a = 0; a < 2; ++a) {
        window.footprint[a] = std::min(1.f, ratio[a]);
        window.jitter[a] = (point[a] - 0.5f) * (1.f - window.footprint[a]);
    }
    return window;
}

Upscaler::Upscaler(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t inWidth, std::uint32_t inHeight,
                   std::uint32_t outWidth, std::uint32_t outHeight)
    : ctx_(ctx), ring_(ring), inWidth_(inWidth), inHeight_(inHeight), outWidth_(outWidth),
      outHeight_(outHeight),
      pipeline_(ctx, checkedShader(inWidth, inHeight, outWidth, outHeight), kBindings, sizeof(UpscalePush)) {
    createImages();
    ringAlive_ = ring.onBegin([](std::uint32_t) {});
}

Upscaler::~Upscaler() {
    // A frame in flight may still use the history and the pipeline's descriptor sets. A ring that is already
    // gone waited for its submits itself.
    if (ringAlive_.active()) {
        try {
            ring_.waitAll(); // shutdown
        } catch (...) {
        }
    }
}

void Upscaler::createImages() {
    historyValid_ = false;
    current_ = 0;
    if (identity()) {
        history_ = {};
        return;
    }
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    history_[0] = gfx::Image(ctx_, {outWidth_, outHeight_, kHalfFormat, usage, 1, "upscale history 0"});
    history_[1] = gfx::Image(ctx_, {outWidth_, outHeight_, kHalfFormat, usage, 1, "upscale history 1"});
}

void Upscaler::resize(std::uint32_t inWidth, std::uint32_t inHeight, std::uint32_t outWidth,
                      std::uint32_t outHeight) {
    checkSizes(inWidth, inHeight, outWidth, outHeight);
    if (ringAlive_.active())
        ring_.waitAll(); // resize: a frame in flight may still use the old history
    inWidth_ = inWidth;
    inHeight_ = inHeight;
    outWidth_ = outWidth;
    outHeight_ = outHeight;
    createImages();
}

void Upscaler::resetHistory() {
    historyValid_ = false;
}

VkDeviceSize Upscaler::imageBytes() const {
    return history_[0].bytes() + history_[1].bytes(); // none when identity()
}

void Upscaler::record(VkCommandBuffer cmd, gfx::Image& hdrIn, gfx::Image& depth, gfx::Image& motion,
                      const UpscaleSettings& settings, gfx::Image& hdrOut, gfx::Image* emission) {
    if (!ringAlive_.active() || !ring_.recording())
        throw std::logic_error("Upscaler::record() outside its ring's begin() and submit()");
    checkImage(hdrIn, "hdrIn", kHalfFormat, inWidth_, inHeight_);
    checkImage(depth, "depth", kDepthFormat, inWidth_, inHeight_);
    checkImage(motion, "motion", kHalfFormat, inWidth_, inHeight_);
    if (emission)
        checkImage(*emission, "emission", kHalfFormat, inWidth_, inHeight_);
    checkImage(hdrOut, "hdrOut", kHalfFormat, outWidth_, outHeight_);
    if (hdrOut.handle() == hdrIn.handle())
        throw std::invalid_argument("Upscaler: hdrOut must not be hdrIn");
    checkSettings(settings);

    UpscalePush push{inWidth_,          inHeight_,          outWidth_,
                     outHeight_,        settings.jitter[0], settings.jitter[1],
                     settings.feedback, settings.clipGamma, 0u};
    for (gfx::Image* image : {&hdrIn, &depth, &motion, emission})
        if (image)
            prepare(cmd, *image, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    prepare(cmd, hdrOut, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    if (identity()) {
        push.flags = kFlagIdentity;
        const std::array<VkDescriptorImageInfo, 7> images = {storage(hdrIn),  storage(hdrIn), storage(depth),
                                                             storage(motion), storage(hdrIn), storage(hdrOut),
                                                             storage(hdrOut)};
        pipeline_.bind(cmd, ring_, images, {});
    } else {
        const std::uint32_t previous = current_;
        current_ = 1 - current_;
        gfx::Image& historyIn = history_[previous];
        gfx::Image& historyOut = history_[current_];
        prepare(cmd, historyIn, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        prepare(cmd, historyOut, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        push.flags =
            (historyValid_ && settings.temporal ? kFlagHistory : 0u) | (emission ? kFlagEmission : 0u);
        const std::array<VkDescriptorImageInfo, 7> images = {
            storage(hdrIn),     storage(emission ? *emission : hdrIn),
            storage(depth),     storage(motion),
            storage(historyIn), storage(historyOut),
            storage(hdrOut)};
        pipeline_.bind(cmd, ring_, images, {});
        historyValid_ = true;
    }
    pipeline_.push(cmd, &push, sizeof push);
    pipeline_.dispatch(cmd, outWidth_, outHeight_);
}

} // namespace evo::post
