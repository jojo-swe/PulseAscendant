#include "post/denoiser.hpp"

#include "gfx/paths.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace evo::post {

namespace {

constexpr VkFormat kLightingFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
constexpr VkFormat kHalfFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kDepthFormat = VK_FORMAT_R32_SFLOAT;

// À-trous steps double each iteration; beyond 2^8 pixels a tap leaves any sensible image.
constexpr std::uint32_t kMaxAtrousIterations = 9;

// Must match the push constant blocks of shaders/post/*.comp.
struct AccumulatePush {
    std::uint32_t width, height, useHistory, maxHistory;
    float alphaColor, alphaMoments, sigmaDepth, sigmaNormal;
};
static_assert(sizeof(AccumulatePush) == 32);

struct AtrousPush {
    std::uint32_t width, height, step;
    float sigmaDepth, sigmaNormal, sigmaLuminance;
};
static_assert(sizeof(AtrousPush) == 24);

struct CombinePush {
    std::uint32_t width, height;
};
static_assert(sizeof(CombinePush) == 8);

template <std::size_t N> std::array<gfx::Binding, N> storageImages() {
    std::array<gfx::Binding, N> bindings{};
    for (std::uint32_t i = 0; i < N; ++i)
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    return bindings;
}

const auto kAccumulateBindings = storageImages<11>();
const auto kAtrousBindings = storageImages<4>();
const auto kCombineBindings = storageImages<4>();

VkDescriptorImageInfo storage(const gfx::Image& image) {
    return {VK_NULL_HANDLE, image.view(), VK_IMAGE_LAYOUT_GENERAL};
}

void prepareRead(VkCommandBuffer cmd, gfx::Image& image) {
    image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

void prepareWrite(VkCommandBuffer cmd, gfx::Image& image) {
    image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

std::string sizeText(VkExtent2D extent) {
    return std::to_string(extent.width) + "x" + std::to_string(extent.height);
}

void checkImage(const gfx::Image& image, const char* what, VkFormat format, std::uint32_t width,
                std::uint32_t height) {
    if (image.format() != format)
        throw std::invalid_argument(std::string("Denoiser: ") + what + " has format " +
                                    std::to_string(static_cast<int>(image.format())) + ", expected " +
                                    std::to_string(static_cast<int>(format)));
    if (image.extent().width != width || image.extent().height != height)
        throw std::invalid_argument(std::string("Denoiser: ") + what + " is " + sizeText(image.extent()) +
                                    ", the denoiser " + sizeText({width, height}));
}

void checkSize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0)
        throw std::invalid_argument("Denoiser needs a positive size, got " + sizeText({width, height}));
}

} // namespace

Denoiser::Denoiser(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height)
    : ctx_(ctx), ring_(ring), width_(width), height_(height),
      accumulate_(ctx, gfx::shaderDir() / "post_accumulate.comp.spv", kAccumulateBindings,
                  sizeof(AccumulatePush)),
      atrous_(ctx, gfx::shaderDir() / "post_atrous.comp.spv", kAtrousBindings, sizeof(AtrousPush)),
      combine_(ctx, gfx::shaderDir() / "post_combine.comp.spv", kCombineBindings, sizeof(CombinePush)) {
    checkSize(width, height);
    createImages();
    ringAlive_ = ring.onBegin([](std::uint32_t) {});
}

Denoiser::~Denoiser() {
    // A frame in flight may still use the images and the pipelines' descriptor sets. A ring that is already
    // gone waited for its submits itself.
    if (ringAlive_.active()) {
        try {
            ring_.waitAll(); // shutdown
        } catch (...) {
        }
    }
}

void Denoiser::createImages() {
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const auto make = [&](const char* name) {
        return gfx::Image(ctx_, {width_, height_, kHalfFormat, usage, 1, name});
    };
    color_ = {make("denoiser colour history 0"), make("denoiser colour history 1")};
    moments_ = {make("denoiser moments history 0"), make("denoiser moments history 1")};
    guides_ = {make("denoiser guide history 0"), make("denoiser guide history 1")};
    filtered_ = {make("denoiser a-trous 0"), make("denoiser a-trous 1")};
    current_ = 0;
    historyValid_ = false;
}

void Denoiser::resize(std::uint32_t width, std::uint32_t height) {
    checkSize(width, height);
    if (ringAlive_.active())
        ring_.waitAll(); // resize: a frame in flight may still use the old images
    width_ = width;
    height_ = height;
    createImages();
}

void Denoiser::resetHistory() {
    historyValid_ = false;
}

void Denoiser::denoise(VkCommandBuffer cmd, const GBufferView& gbuffer, const DenoiseSettings& settings,
                       gfx::Image& hdrOut) {
    if (!ringAlive_.active() || !ring_.recording())
        throw std::logic_error("Denoiser::denoise() outside its ring's begin() and submit()");
    checkImage(gbuffer.lighting, "lighting", kLightingFormat, width_, height_);
    checkImage(gbuffer.emission, "emission", kHalfFormat, width_, height_);
    checkImage(gbuffer.albedo, "albedo", kHalfFormat, width_, height_);
    checkImage(gbuffer.normal, "normal", kHalfFormat, width_, height_);
    checkImage(gbuffer.depth, "depth", kDepthFormat, width_, height_);
    checkImage(gbuffer.motion, "motion", kHalfFormat, width_, height_);
    checkImage(hdrOut, "hdrOut", kHalfFormat, width_, height_);
    if (settings.maxHistory == 0)
        throw std::invalid_argument("Denoiser: maxHistory must be at least 1");
    if (settings.atrousIterations > kMaxAtrousIterations)
        throw std::invalid_argument("Denoiser: at most " + std::to_string(kMaxAtrousIterations) +
                                    " a-trous iterations, got " + std::to_string(settings.atrousIterations));

    const std::uint32_t previous = current_;
    current_ = 1 - current_;

    // Temporal: demodulate, reproject, accumulate.
    for (gfx::Image* image : {&gbuffer.lighting, &gbuffer.albedo, &gbuffer.normal, &gbuffer.depth,
                              &gbuffer.motion, &color_[previous], &moments_[previous], &guides_[previous]})
        prepareRead(cmd, *image);
    for (gfx::Image* image : {&color_[current_], &moments_[current_], &guides_[current_]})
        prepareWrite(cmd, *image);
    const std::array<VkDescriptorImageInfo, 11> accumulateImages = {
        storage(gbuffer.lighting),   storage(gbuffer.albedo),    storage(gbuffer.normal),
        storage(gbuffer.depth),      storage(gbuffer.motion),    storage(color_[previous]),
        storage(moments_[previous]), storage(guides_[previous]), storage(color_[current_]),
        storage(moments_[current_]), storage(guides_[current_])};
    accumulate_.bind(cmd, ring_, accumulateImages, {});
    const AccumulatePush accumulatePush{width_,
                                        height_,
                                        settings.temporal && historyValid_ ? 1u : 0u,
                                        settings.maxHistory,
                                        settings.alphaColor,
                                        settings.alphaMoments,
                                        settings.sigmaDepth,
                                        settings.sigmaNormal};
    accumulate_.push(cmd, &accumulatePush, sizeof(accumulatePush));
    accumulate_.dispatch(cmd, width_, height_);
    historyValid_ = true;

    // Spatial: à-trous over the accumulated illumination; the history itself stays unfiltered.
    gfx::Image* source = &color_[current_];
    for (std::uint32_t i = 0; i < settings.atrousIterations; ++i) {
        gfx::Image& target = filtered_[i % 2];
        prepareRead(cmd, *source);
        prepareWrite(cmd, target);
        const std::array<VkDescriptorImageInfo, 4> atrousImages = {storage(*source), storage(gbuffer.depth),
                                                                   storage(gbuffer.normal), storage(target)};
        atrous_.bind(cmd, ring_, atrousImages, {});
        const AtrousPush atrousPush{
            width_, height_, 1u << i, settings.sigmaDepth, settings.sigmaNormal, settings.sigmaLuminance};
        atrous_.push(cmd, &atrousPush, sizeof(atrousPush));
        atrous_.dispatch(cmd, width_, height_);
        source = &target;
    }

    // Remodulate and add the emission.
    prepareRead(cmd, *source);
    prepareRead(cmd, gbuffer.emission);
    prepareWrite(cmd, hdrOut);
    const std::array<VkDescriptorImageInfo, 4> combineImages = {storage(*source), storage(gbuffer.albedo),
                                                                storage(gbuffer.emission), storage(hdrOut)};
    combine_.bind(cmd, ring_, combineImages, {});
    const CombinePush combinePush{width_, height_};
    combine_.push(cmd, &combinePush, sizeof(combinePush));
    combine_.dispatch(cmd, width_, height_);
}

} // namespace evo::post
