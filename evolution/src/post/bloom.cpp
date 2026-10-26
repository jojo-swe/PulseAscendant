#include "post/bloom.hpp"

#include "gfx/paths.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <stdexcept>
#include <string>

namespace evo::post {

namespace {

// Must match the push_constant blocks of shaders/post/bloom_{down,spokes,up}.comp.
struct DownPush {
    std::uint32_t width, height, sourceWidth, sourceHeight;
};
static_assert(sizeof(DownPush) == 16);

struct SpokesPush {
    std::uint32_t width, height;
    float spokeShare, streakShare;
};
static_assert(sizeof(SpokesPush) == 16);

struct UpPush {
    std::uint32_t width, height;
    float fineWeight, coarseScale, halo;
    std::uint32_t addSpokes;
};
static_assert(sizeof(UpPush) == 24);

constexpr std::array<gfx::Binding, 2> kTwoImages = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
};
constexpr std::array<gfx::Binding, 3> kThreeImages = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
};

static_assert(Bloom::kLevelWeights[0] + Bloom::kLevelWeights[1] + Bloom::kLevelWeights[2] +
                      Bloom::kLevelWeights[3] + Bloom::kLevelWeights[4] + Bloom::kLevelWeights[5] >
                  0.9999f &&
              Bloom::kLevelWeights[0] + Bloom::kLevelWeights[1] + Bloom::kLevelWeights[2] +
                      Bloom::kLevelWeights[3] + Bloom::kLevelWeights[4] + Bloom::kLevelWeights[5] <
                  1.0001f,
              "the halo's levels must keep the energy");

std::uint32_t half(std::uint32_t size) {
    return (size + 1) / 2;
}

gfx::ImageDesc levelDesc(std::uint32_t width, std::uint32_t height, const char* name) {
    return {width,
            height,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            1,
            name};
}

VkDescriptorImageInfo info(const gfx::Image& image) {
    return {VK_NULL_HANDLE, image.view(), VK_IMAGE_LAYOUT_GENERAL};
}

} // namespace

Bloom::Bloom(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height)
    : ctx_(ctx), ring_(ring), width_(width), height_(height),
      down_(ctx, gfx::shaderDir() / "post_bloom_down.comp.spv", kTwoImages, sizeof(DownPush)),
      spokes_(ctx, gfx::shaderDir() / "post_bloom_spokes.comp.spv", kTwoImages, sizeof(SpokesPush)),
      up_(ctx, gfx::shaderDir() / "post_bloom_up.comp.spv", kThreeImages, sizeof(UpPush)) {
    makeImages();
}

void Bloom::resize(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
    makeImages();
}

void Bloom::makeImages() {
    if (width_ == 0 || height_ == 0)
        throw std::invalid_argument("Bloom: an empty picture");
    std::uint32_t w = width_, h = height_;
    for (std::uint32_t k = 0; k < kLevels; ++k) {
        w = half(w);
        h = half(h);
        levels_[k] = gfx::Image(ctx_, levelDesc(w, h, k == 0 ? "bloom" : "bloom level"));
    }
    const VkExtent2D quarter = levels_[1].extent();
    spokeImage_ = gfx::Image(ctx_, levelDesc(quarter.width, quarter.height, "bloom spokes"));
}

VkDeviceSize Bloom::imageBytes() const {
    VkDeviceSize bytes = spokeImage_.bytes();
    for (const gfx::Image& level : levels_)
        bytes += level.bytes();
    return bytes;
}

void Bloom::record(VkCommandBuffer cmd, gfx::Image& hdr, float anamorphic, float spokeShare) {
    if (hdr.format() != VK_FORMAT_R16G16B16A16_SFLOAT || hdr.extent().width != width_ ||
        hdr.extent().height != height_)
        throw std::invalid_argument("Bloom: the HDR image must be R16G16B16A16_SFLOAT at " +
                                    std::to_string(width_) + "x" + std::to_string(height_));
    const float streak = kStreakShare * std::clamp(anamorphic, 0.f, 1.f);
    const float spokes = std::clamp(spokeShare, 0.f, 0.08f);

    // Down: level k from level k - 1 (the HDR image for the first).
    for (std::uint32_t k = 0; k < kLevels; ++k) {
        gfx::Image& source = k == 0 ? hdr : levels_[k - 1];
        gfx::Image& target = levels_[k];
        source.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        target.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const std::array<VkDescriptorImageInfo, 2> images = {info(source), info(target)};
        down_.bind(cmd, ring_, images, {});
        const VkExtent2D t = target.extent(), s = source.extent();
        const DownPush push{t.width, t.height, s.width, s.height};
        down_.push(cmd, &push, sizeof push);
        down_.dispatch(cmd, t.width, t.height);
    }

    // The spokes (and the streak) from the quarter-size picture, before the way up overwrites it.
    gfx::Image& quarter = levels_[1];
    quarter.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    spokeImage_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    {
        const std::array<VkDescriptorImageInfo, 2> images = {info(quarter), info(spokeImage_)};
        spokes_.bind(cmd, ring_, images, {});
        const VkExtent2D q = spokeImage_.extent();
        const SpokesPush push{q.width, q.height, spokes, streak};
        spokes_.push(cmd, &push, sizeof push);
        spokes_.dispatch(cmd, q.width, q.height);
    }

    // Up, in place: level k = w_k level k + (the coarser levels' sum, upsampled); the coarsest is read as it
    // is and scaled by its own weight. The last step scales the halo to its share and adds the spokes.
    for (std::uint32_t k = kLevels - 1; k-- > 0;) {
        gfx::Image& fine = levels_[k];
        gfx::Image& coarse = levels_[k + 1];
        const bool last = k == 0;
        coarse.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        spokeImage_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        fine.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const std::array<VkDescriptorImageInfo, 3> images = {info(fine), info(coarse), info(spokeImage_)};
        up_.bind(cmd, ring_, images, {});
        const VkExtent2D f = fine.extent();
        const UpPush push{f.width,
                          f.height,
                          kLevelWeights[k],
                          k + 1 == kLevels - 1 ? kLevelWeights[kLevels - 1] : 1.f,
                          last ? 1.f - spokes - streak : 1.f,
                          last ? 1u : 0u};
        up_.push(cmd, &push, sizeof push);
        up_.dispatch(cmd, f.width, f.height);
    }
}

} // namespace evo::post
