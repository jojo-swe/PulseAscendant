#include "render/retro_stage.hpp"

#include "gfx/paths.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace evo::render {

namespace {

// Must match the push_constant block of the effect shaders (shaders/retro/test_pattern.comp, 12 bytes).
struct RetroPushConstants {
    std::uint32_t retroFrame;
    std::uint32_t chapter;
    float retroSeconds;
};
static_assert(sizeof(RetroPushConstants) == 12);

constexpr std::array<gfx::Binding, 1> kEffectBindings = {gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}};
constexpr std::array<gfx::Binding, 3> kPaletteMapBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
};

constexpr VkDeviceSize kPaletteBytes = sizeof(retro::Palette);

gfx::Image createIndexImage(gfx::Context& ctx) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice(), VK_FORMAT_R8_UINT, &properties);
    if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        throw std::runtime_error("the GPU cannot use R8_UINT as a storage image, which the 1993 stage needs");
    // Cleared for the text-only effects (transfer destination) and read back by tests (transfer source).
    return gfx::Image(
        ctx, {RetroStage::kWidth, RetroStage::kHeight, VK_FORMAT_R8_UINT,
              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              1, "retro index"});
}

std::uint32_t retroFrameOf(const show::TimelineFrame& frame, std::uint32_t rate) {
    const double frames = std::floor(std::max(frame.localSeconds, 0.0) * rate);
    return static_cast<std::uint32_t>(frames);
}

} // namespace

RetroStage::RetroStage(gfx::Context& ctx, gfx::FrameRing& ring, text::TextPass& text)
    : ring_(ring), text_(text), index_(createIndexImage(ctx)),
      color_(ctx, {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, 1, "retro color"}),
      paletteMap_(ctx, gfx::shaderDir() / "retro_palette_map.comp.spv", kPaletteMapBindings, 0) {
    for (gfx::Buffer& palette : palettes_)
        palette =
            gfx::Buffer(ctx, {kPaletteBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "retro palette"});
    for (int effect = 0; effect <= static_cast<int>(show::Effect::TheEnd); ++effect) {
        const char* shader = retro::effectFor(static_cast<show::Effect>(effect)).shader;
        if (shader && !effects_.contains(shader))
            effects_.emplace(shader, std::make_unique<gfx::ComputePipeline>(ctx, gfx::shaderDir() / shader,
                                                                            kEffectBindings,
                                                                            sizeof(RetroPushConstants)));
    }
}

bool RetroStage::render(VkCommandBuffer cmd, const show::TimelineFrame& frame) {
    const retro::EffectInfo& effect = retro::effectFor(frame.effect);
    retroFrame_ = retroFrameOf(frame, effect.retroRate);
    const Key key{frame.chapter, static_cast<int>(frame.effect), retroFrame_};
    if (cached_ == key)
        return false;

    // The effect: palette indices, from its shader, or for a text-only effect a clear to its background
    // (index 0 by default) that the text below draws on; nothing of the last frame may show through.
    const VkDescriptorImageInfo indexInfo{VK_NULL_HANDLE, index_.view(), VK_IMAGE_LAYOUT_GENERAL};
    if (effect.shader) {
        index_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        gfx::ComputePipeline& pass = *effects_.at(effect.shader);
        pass.bind(cmd, ring_, {&indexInfo, 1}, {});
        const RetroPushConstants push{retroFrame_, static_cast<std::uint32_t>(frame.chapter),
                                      static_cast<float>(retroFrame_) / static_cast<float>(effect.retroRate)};
        pass.push(cmd, &push, sizeof push);
        pass.dispatch(cmd, kWidth, kHeight);
    } else {
        index_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkClearColorValue clear{};
        clear.uint32[0] = effect.background ? effect.background(frame, retroFrame_) : 0u;
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, index_.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    }

    // The effect's text over it; drawIndex leaves the image in GENERAL after its last write.
    if (effect.text) {
        const std::vector<text::Item> items = effect.text(frame, retroFrame_);
        if (!items.empty())
            text_.drawIndex(cmd, index_, items);
    }

    // The palette, written into this slot's buffer: the ring's begin() waited for the submit that last used
    // it. The host write is visible to the GPU through the queue submit.
    retro::Palette palette{};
    effect.palette(palette, frame, retroFrame_);
    palette[0] = retro::rgba(0, 0, 0); // index 0 is always black
    const gfx::Buffer& buffer = palettes_[ring_.slot()];
    std::memcpy(buffer.mapped(), palette.data(), kPaletteBytes);
    buffer.flush();

    // Indices -> colours.
    index_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    color_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const std::array<VkDescriptorImageInfo, 2> images = {
        indexInfo, VkDescriptorImageInfo{VK_NULL_HANDLE, color_.view(), VK_IMAGE_LAYOUT_GENERAL}};
    const VkDescriptorBufferInfo paletteInfo{buffer.handle(), 0, kPaletteBytes};
    paletteMap_.bind(cmd, ring_, images, {&paletteInfo, 1});
    paletteMap_.dispatch(cmd, kWidth, kHeight);

    cached_ = key;
    return true;
}

} // namespace evo::render
