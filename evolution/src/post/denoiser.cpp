#include "post/denoiser.hpp"

#include "gfx/paths.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace evo::post {

namespace {

constexpr VkFormat kLightingFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
constexpr VkFormat kHalfFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kDepthFormat = VK_FORMAT_R32_SFLOAT;

// À-trous steps double each iteration; beyond 2^8 pixels a tap leaves any sensible image.
constexpr std::uint32_t kMaxAtrousIterations = 9;

// Must match the push constant blocks of shaders/post/*.comp.
struct FireflyPush {
    std::uint32_t width, height;
    float k;
};
static_assert(sizeof(FireflyPush) == 12);

struct AccumulatePush {
    std::uint32_t width, height, useHistory, maxHistory;
    float alphaColor, alphaMoments, sigmaDepth, sigmaNormal;
    std::uint32_t useFirefly, hasMedium, useVolumeHistory;
};
static_assert(sizeof(AccumulatePush) == 44);

struct AtrousPush {
    std::uint32_t width, height, step;
    float sigmaDepth, sigmaNormal, sigmaLuminance;
    std::uint32_t copyAbove, volume;
};
static_assert(sizeof(AtrousPush) == 32);

struct CombinePush {
    std::uint32_t width, height, hasMedium;
};
static_assert(sizeof(CombinePush) == 12);

// AtrousPush::copyAbove: filter every pixel.
constexpr std::uint32_t kFilterAll = 0xFFFFFFFFu;
// The history length up to which a pixel gets resetAtrousIterations: the window's first three frames after a
// reset (at 3 iterations frame 3 kept low-frequency blotches in the Studio).
constexpr std::uint32_t kResetLength = 3;

template <std::size_t N> std::array<gfx::Binding, N> storageImages() {
    std::array<gfx::Binding, N> bindings{};
    for (std::uint32_t i = 0; i < N; ++i)
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    return bindings;
}

const auto kFireflyBindings = storageImages<5>();
const auto kAccumulateBindings = storageImages<15>();
const auto kAtrousBindings = storageImages<5>();
const auto kCombineBindings = storageImages<5>();

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

void checkSettings(const DenoiseSettings& settings) {
    if (settings.maxHistory == 0)
        throw std::invalid_argument("Denoiser: maxHistory must be at least 1");
    for (const auto& [name, iterations] :
         {std::pair{"atrousIterations", settings.atrousIterations},
          std::pair{"resetAtrousIterations", settings.resetAtrousIterations},
          std::pair{"volumeAtrousIterations", settings.volumeAtrousIterations}})
        if (iterations > kMaxAtrousIterations)
            throw std::invalid_argument(std::string("Denoiser: at most ") +
                                        std::to_string(kMaxAtrousIterations) + " a-trous iterations, got " +
                                        name + " " + std::to_string(iterations));
    if (!(std::isfinite(settings.fireflyK) && settings.fireflyK >= 1.0f))
        throw std::invalid_argument("Denoiser: fireflyK must be finite and at least 1, got " +
                                    std::to_string(settings.fireflyK));
    // Written as positive tests so a NaN fails them.
    for (const auto& [name, sigma] :
         {std::pair{"sigmaDepth", settings.sigmaDepth}, std::pair{"sigmaNormal", settings.sigmaNormal},
          std::pair{"sigmaLuminance", settings.sigmaLuminance}})
        if (!(std::isfinite(sigma) && sigma > 0.0f))
            throw std::invalid_argument(std::string("Denoiser: ") + name +
                                        " must be positive and finite, got " + std::to_string(sigma));
    for (const auto& [name, alpha] :
         {std::pair{"alphaColor", settings.alphaColor}, std::pair{"alphaMoments", settings.alphaMoments}})
        if (!(alpha > 0.0f && alpha <= 1.0f))
            throw std::invalid_argument(std::string("Denoiser: ") + name + " must be in (0, 1], got " +
                                        std::to_string(alpha));
}

void checkSize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0)
        throw std::invalid_argument("Denoiser needs a positive size, got " + sizeText({width, height}));
}

} // namespace

Denoiser::Denoiser(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height)
    : ctx_(ctx), ring_(ring), width_(width), height_(height),
      firefly_(ctx, gfx::shaderDir() / "post_firefly.comp.spv", kFireflyBindings, sizeof(FireflyPush)),
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
    volume_ = {make("denoiser volume history 0"), make("denoiser volume history 1")};
    current_ = 0;
    historyValid_ = false;
    volumeHistoryValid_ = false;
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
    checkImage(gbuffer.volume, "volume", kHalfFormat, width_, height_);
    checkImage(hdrOut, "hdrOut", kHalfFormat, width_, height_);
    checkSettings(settings);

    const std::uint32_t previous = current_;
    current_ = 1 - current_;

    // Exports from 16 spp: clamp lone fireflies first, into an a-trous image that is free until the a-trous.
    gfx::Image& fireflyOut = filtered_[1];
    if (settings.fireflyFilter) {
        for (gfx::Image* image : {&gbuffer.lighting, &gbuffer.albedo, &gbuffer.normal, &gbuffer.depth})
            prepareRead(cmd, *image);
        prepareWrite(cmd, fireflyOut);
        const std::array<VkDescriptorImageInfo, 5> fireflyImages = {
            storage(gbuffer.lighting), storage(gbuffer.albedo), storage(fireflyOut), storage(gbuffer.normal),
            storage(gbuffer.depth)};
        firefly_.bind(cmd, ring_, fireflyImages, {});
        const FireflyPush fireflyPush{width_, height_, settings.fireflyK};
        firefly_.push(cmd, &fireflyPush, sizeof(fireflyPush));
        firefly_.dispatch(cmd, width_, height_);
    }

    // Temporal: demodulate, reproject, accumulate; the volume layer beside it.
    for (gfx::Image* image : {&gbuffer.lighting, &gbuffer.albedo, &gbuffer.normal, &gbuffer.depth,
                              &gbuffer.motion, &gbuffer.volume, &color_[previous], &moments_[previous],
                              &guides_[previous], &volume_[previous], &fireflyOut})
        prepareRead(cmd, *image);
    for (gfx::Image* image : {&color_[current_], &moments_[current_], &guides_[current_], &volume_[current_]})
        prepareWrite(cmd, *image);
    const std::array<VkDescriptorImageInfo, 15> accumulateImages = {
        storage(gbuffer.lighting),   storage(gbuffer.albedo),    storage(gbuffer.normal),
        storage(gbuffer.depth),      storage(gbuffer.motion),    storage(color_[previous]),
        storage(moments_[previous]), storage(guides_[previous]), storage(color_[current_]),
        storage(moments_[current_]), storage(guides_[current_]), storage(fireflyOut),
        storage(gbuffer.volume),     storage(volume_[previous]), storage(volume_[current_])};
    accumulate_.bind(cmd, ring_, accumulateImages, {});
    const AccumulatePush accumulatePush{width_,
                                        height_,
                                        settings.temporal && historyValid_ ? 1u : 0u,
                                        settings.maxHistory,
                                        settings.alphaColor,
                                        settings.alphaMoments,
                                        settings.sigmaDepth,
                                        settings.sigmaNormal,
                                        settings.fireflyFilter ? 1u : 0u,
                                        settings.hasMedium ? 1u : 0u,
                                        settings.temporal && historyValid_ && volumeHistoryValid_ ? 1u : 0u};
    accumulate_.push(cmd, &accumulatePush, sizeof(accumulatePush));
    accumulate_.dispatch(cmd, width_, height_);
    historyValid_ = true;
    volumeHistoryValid_ = settings.hasMedium;

    const std::uint32_t iterations = settings.atrousIterations;
    const std::uint32_t allIterations =
        iterations == 0 ? 0u : std::max(iterations, settings.resetAtrousIterations);

    // One a-trous iteration from source into target.
    const auto atrous = [&](gfx::Image& source, gfx::Image& target, std::uint32_t step,
                            std::uint32_t copyAbove, bool volume) {
        prepareRead(cmd, source);
        prepareRead(cmd, moments_[current_]);
        prepareWrite(cmd, target);
        const std::array<VkDescriptorImageInfo, 5> atrousImages = {storage(source), storage(gbuffer.depth),
                                                                   storage(gbuffer.normal), storage(target),
                                                                   storage(moments_[current_])};
        atrous_.bind(cmd, ring_, atrousImages, {});
        const AtrousPush atrousPush{width_,
                                    height_,
                                    step,
                                    settings.sigmaDepth,
                                    settings.sigmaNormal,
                                    settings.sigmaLuminance,
                                    copyAbove,
                                    volume ? 1u : 0u};
        atrous_.push(cmd, &atrousPush, sizeof(atrousPush));
        atrous_.dispatch(cmd, width_, height_);
    };

    // Spatial: à-trous over the accumulated illumination; the history itself stays unfiltered. Pixels with a
    // history of at most kResetLength frames get resetAtrousIterations, the others copy through the extra
    // ones.
    gfx::Image* source = &color_[current_];
    for (std::uint32_t i = 0; i < allIterations; ++i) {
        gfx::Image& target = filtered_[i % 2];
        atrous(*source, target, 1u << i, i < iterations ? kFilterAll : kResetLength, false);
        source = &target;
    }

    // The volume layer's own à-trous, ping-ponging between the a-trous image the surface left free and the
    // previous frame's volume history, which nothing reads any more this frame.
    gfx::Image* volume = &volume_[current_];
    const std::array<gfx::Image*, 2> volumeTargets = {&filtered_[allIterations % 2], &volume_[previous]};
    // Without a medium the layer is not filtered (nor read by the combine).
    const std::uint32_t volumeIterations = settings.hasMedium ? settings.volumeAtrousIterations : 0u;
    for (std::uint32_t i = 0; i < volumeIterations; ++i) {
        gfx::Image& target = *volumeTargets[i % 2];
        atrous(*volume, target, 1u << i, kFilterAll, true);
        volume = &target;
    }

    // Remodulate, add the emission, compose the volume.
    for (gfx::Image* image : {source, &gbuffer.albedo, &gbuffer.emission, volume})
        prepareRead(cmd, *image);
    prepareWrite(cmd, hdrOut);
    const std::array<VkDescriptorImageInfo, 5> combineImages = {storage(*source), storage(gbuffer.albedo),
                                                                storage(gbuffer.emission), storage(*volume),
                                                                storage(hdrOut)};
    combine_.bind(cmd, ring_, combineImages, {});
    const CombinePush combinePush{width_, height_, settings.hasMedium ? 1u : 0u};
    combine_.push(cmd, &combinePush, sizeof(combinePush));
    combine_.dispatch(cmd, width_, height_);
}

} // namespace evo::post
