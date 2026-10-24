#include "render/retro_stage.hpp"

#include "gfx/buffer.hpp"
#include "gfx/paths.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
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
// An effect with a texture: the index image, then the texture.
constexpr std::array<gfx::Binding, 2> kTexturedBindings = {gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                                                           gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}};
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

// An effect's texture as an R8_UINT storage image, uploaded through a staging buffer and left in GENERAL for
// the effect shaders to read.
gfx::Image createTexture(gfx::Context& ctx, const retro::EffectTexture& texture) {
    if (texture.width == 0 || texture.height == 0 ||
        texture.texels.size() != std::size_t{texture.width} * texture.height)
        throw std::invalid_argument("an effect texture must hold width x height texels");
    gfx::Image image(ctx,
                     {texture.width, texture.height, VK_FORMAT_R8_UINT,
                      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "effect texture"});
    gfx::Buffer staging(
        ctx, {texture.texels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "effect texture staging"});
    std::memcpy(staging.mapped(), texture.texels.data(), texture.texels.size());
    staging.flush();
    // immediate() waits for the copy, so the staging buffer may go when this returns.
    ctx.immediate([&](VkCommandBuffer cmd) { // setup: once, when the stage is built
        image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {texture.width, texture.height, 1};
        vkCmdCopyBufferToImage(cmd, staging.handle(), image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    });
    return image;
}

std::uint32_t retroFrameOf(const show::TimelineFrame& frame, std::uint32_t rate) {
    const double frames = std::floor(std::max(frame.localSeconds, 0.0) * rate);
    return static_cast<std::uint32_t>(frames);
}

} // namespace

RetroStage::RetroStage(gfx::Context& ctx, gfx::FrameRing& ring, text::TextPass& text, EffectLookup effects)
    : ring_(ring), text_(text), effects_(effects), index_(createIndexImage(ctx)),
      // Written as UNORM storage, sampled and mipmapped as sRGB; the blits need both transfer usages (and
      // tests read it back).
      color_(ctx, {kWidth, kHeight, VK_FORMAT_R8G8B8A8_SRGB,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                   kRetroMipLevels, "retro color", VK_FORMAT_R8G8B8A8_UNORM}),
      paletteMap_(ctx, gfx::shaderDir() / "retro_palette_map.comp.spv", kPaletteMapBindings, 0),
      raster_(ctx, ring) {
    for (gfx::Buffer& palette : palettes_)
        palette =
            gfx::Buffer(ctx, {kPaletteBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "retro palette"});
    for (std::size_t effect = 0; effect < show::kEffectCount; ++effect) {
        const retro::EffectInfo& info = effects_(static_cast<show::Effect>(effect));
        const bool textured = info.texture != nullptr;
        if (textured && !info.shader)
            throw std::invalid_argument("an effect texture needs an effect shader to read it");
        if (textured && !textures_.contains(info.texture))
            textures_.emplace(info.texture, createTexture(ctx, info.texture()));
        if (!info.shader)
            continue;
        if (const auto known = shaders_.find(info.shader); known != shaders_.end()) {
            if (known->second.textured != textured)
                throw std::logic_error(std::string("effect shader ") + info.shader +
                                       " is named both with and without a texture");
            continue;
        }
        const std::span<const gfx::Binding> bindings = textured
                                                           ? std::span<const gfx::Binding>(kTexturedBindings)
                                                           : std::span<const gfx::Binding>(kEffectBindings);
        shaders_.emplace(info.shader, EffectShader{std::make_unique<gfx::ComputePipeline>(
                                                       ctx, gfx::shaderDir() / info.shader, bindings,
                                                       sizeof(RetroPushConstants)),
                                                   textured});
    }
}

bool RetroStage::render(VkCommandBuffer cmd, const show::TimelineFrame& frame) {
    const retro::EffectInfo& outer = effects_(frame.effect);
    retroFrame_ = retroFrameOf(frame, outer.retroRate);
    const Key key{frame.chapter, static_cast<int>(frame.effect), retroFrame_};
    if (cached_ == key)
        return false;

    // A megamix renders another entry with that entry's own frame and retro frame (its home chapter's), but
    // keeps its key above and its own text below.
    const retro::EffectFrame shown = retro::resolveEffect(outer, frame, retroFrame_);
    const retro::EffectInfo& effect = effects_(shown.effect);
    if (shown.effect != frame.effect && effect.resolve)
        throw std::logic_error("an effect resolved to an effect that resolves again");
    const show::TimelineFrame& at = shown.frame;
    // The retro frame the resolved entry renders, at its own rate: a megamix's sub-effect's home frame, or
    // retroFrame_ itself.
    const std::uint32_t homeRetroFrame = shown.retroFrame;

    // The effect: palette indices, from its shader, or for an effect without one a clear to its background
    // (index 0 by default) that the scene and text below draw on; nothing of the last frame may show through.
    const VkDescriptorImageInfo indexInfo{VK_NULL_HANDLE, index_.view(), VK_IMAGE_LAYOUT_GENERAL};
    if (effect.shader) {
        index_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        gfx::ComputePipeline& pass = *shaders_.at(effect.shader).pipeline;
        if (effect.texture) {
            gfx::Image& texture = textures_.at(effect.texture);
            texture.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               VK_ACCESS_2_SHADER_STORAGE_READ_BIT); // read only: no barrier after the upload
            const std::array<VkDescriptorImageInfo, 2> images = {
                indexInfo, VkDescriptorImageInfo{VK_NULL_HANDLE, texture.view(), VK_IMAGE_LAYOUT_GENERAL}};
            pass.bind(cmd, ring_, images, {});
        } else {
            pass.bind(cmd, ring_, {&indexInfo, 1}, {});
        }
        const RetroPushConstants push{homeRetroFrame, static_cast<std::uint32_t>(at.chapter),
                                      static_cast<float>(homeRetroFrame) /
                                          static_cast<float>(effect.retroRate)};
        pass.push(cmd, &push, sizeof push);
        pass.dispatch(cmd, kWidth, kHeight);
    } else {
        index_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkClearColorValue clear{};
        clear.uint32[0] = effect.background ? effect.background(at, homeRetroFrame) : 0u;
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, index_.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    }

    // The effect's triangles and points over it, built on the CPU for this retro frame and drawn in painter's
    // order; the rasteriser throws for a scene past its limits.
    if (effect.scene) {
        scene_.clear();
        effect.scene(scene_, at, homeRetroFrame);
        raster_.draw(cmd, index_, scene_);
    }

    // The effect's own text over it (a megamix's, not the resolved entry's); drawIndex leaves the image in
    // GENERAL after its last write.
    if (outer.text) {
        const std::vector<text::Item> items = outer.text(frame, retroFrame_);
        if (!items.empty())
            text_.drawIndex(cmd, index_, items);
    }

    // The palette, written into this slot's buffer: the ring's begin() waited for the submit that last used
    // it. The host write is visible to the GPU through the queue submit.
    retro::Palette palette{};
    effect.palette(palette, at, homeRetroFrame);
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

    // The mip chain from the new picture, ready for the compositor's CRT pass (storage reads of level 0)
    // and for samplers.
    color_.generateMips(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    cached_ = key;
    return true;
}

} // namespace evo::render
