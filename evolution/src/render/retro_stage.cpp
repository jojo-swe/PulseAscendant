#include "render/retro_stage.hpp"

#include "gfx/buffer.hpp"
#include "gfx/paths.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "show/director.hpp"

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

constexpr std::array<gfx::Binding, 5> kPhosphorBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, gfx::Binding{3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}};
constexpr std::array<gfx::Binding, 2> kTileBindings = {gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                                                       gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}};

// Must match phosphor.comp's and tile.comp's push constants.
struct PhosphorPushConstants {
    float weights[4];
};
struct TilePushConstants {
    std::int32_t from[4];
    std::int32_t to[4];
    float gain;
    std::uint32_t masked;
};
static_assert(sizeof(TilePushConstants) == 40);

// The cache key's effect for the thank-you card, which is no chapter's effect.
constexpr int kCardKey = -1;

constexpr VkDeviceSize kPaletteBytes = sizeof(retro::Palette);
// The most pictures one render() draws: a grid's layer sources (the 3x3 grid's eight tiles and its own
// picture) or a phosphor frame and its three predecessors.
constexpr std::uint32_t kMaxPictures = 16;

// A 320x200 RGBA8 storage image for a picture before it reaches the colour image.
gfx::Image createPicture(gfx::Context& ctx, const char* name) {
    return gfx::Image(
        ctx, {RetroStage::kWidth, RetroStage::kHeight, VK_FORMAT_R8G8B8A8_UNORM,
              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              1, name});
}

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
      scratch_(createPicture(ctx, "retro grid source")),
      held_{
          Held{createPicture(ctx, "retro phosphor 0"), {}}, Held{createPicture(ctx, "retro phosphor 1"), {}},
          Held{createPicture(ctx, "retro phosphor 2"), {}}, Held{createPicture(ctx, "retro phosphor 3"), {}}},
      paletteMap_(ctx, gfx::shaderDir() / "retro_palette_map.comp.spv", kPaletteMapBindings, 0),
      phosphor_(ctx, gfx::shaderDir() / "retro_phosphor.comp.spv", kPhosphorBindings,
                sizeof(PhosphorPushConstants)),
      tile_(ctx, gfx::shaderDir() / "retro_tile.comp.spv", kTileBindings, sizeof(TilePushConstants)),
      raster_(ctx, ring), subscription_(ring.onBegin([this](std::uint32_t) { palettesUsed_ = 0; })) {
    static_assert(retro::kPhosphorFrames + 1 == 4, "phosphor.comp reads the current frame and three more");
    for (gfx::Buffer& palette : palettes_)
        palette = gfx::Buffer(
            ctx, {kPaletteBytes * kMaxPictures, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "retro palettes"});
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

void RetroStage::drawIndices(VkCommandBuffer cmd, const retro::EffectInfo& effect,
                             const show::TimelineFrame& at, std::uint32_t retroFrame,
                             retro::TextProvider text) {
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
        const RetroPushConstants push{retroFrame, static_cast<std::uint32_t>(at.chapter),
                                      static_cast<float>(retroFrame) / static_cast<float>(effect.retroRate)};
        pass.push(cmd, &push, sizeof push);
        pass.dispatch(cmd, kWidth, kHeight);
    } else {
        index_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkClearColorValue clear{};
        clear.uint32[0] = effect.background ? effect.background(at, retroFrame) : 0u;
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, index_.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    }

    // The effect's triangles and points over it, built on the CPU for this retro frame and drawn in painter's
    // order; the rasteriser throws for a scene past its limits.
    if (effect.scene) {
        scene_.clear();
        effect.scene(scene_, at, retroFrame);
        raster_.draw(cmd, index_, scene_);
    }

    // The text over it; drawIndex leaves the image in GENERAL after its last write.
    if (text) {
        const std::vector<text::Item> items = text(at, retroFrame);
        if (!items.empty())
            text_.drawIndex(cmd, index_, items);
    }
}

void RetroStage::mapPalette(VkCommandBuffer cmd, const retro::EffectInfo& effect,
                            const show::TimelineFrame& at, std::uint32_t retroFrame, gfx::Image& target) {
    // The palette, written into a block of this slot's buffer: the ring's begin() waited for the submit that
    // last used it. The host write is visible to the GPU through the queue submit.
    if (palettesUsed_ >= kMaxPictures)
        throw std::logic_error("the retro stage drew more pictures in one frame than it has palettes for");
    retro::Palette palette{};
    effect.palette(palette, at, retroFrame);
    palette[0] = retro::rgba(0, 0, 0); // index 0 is always black
    const gfx::Buffer& buffer = palettes_[ring_.slot()];
    const VkDeviceSize offset = kPaletteBytes * palettesUsed_++;
    std::memcpy(static_cast<std::byte*>(buffer.mapped()) + offset, palette.data(), kPaletteBytes);
    buffer.flush();

    // Indices -> colours.
    index_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    target.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const std::array<VkDescriptorImageInfo, 2> images = {
        VkDescriptorImageInfo{VK_NULL_HANDLE, index_.view(), VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, target.view(), VK_IMAGE_LAYOUT_GENERAL}};
    const VkDescriptorBufferInfo paletteInfo{buffer.handle(), offset, kPaletteBytes};
    paletteMap_.bind(cmd, ring_, images, {&paletteInfo, 1});
    paletteMap_.dispatch(cmd, kWidth, kHeight);
    ++picturesDrawn_;
}

void RetroStage::drawPicture(VkCommandBuffer cmd, const retro::EffectInfo& effect,
                             const show::TimelineFrame& frame, std::uint32_t retroFrame, gfx::Image& target) {
    const std::vector<retro::GridLayer> layers =
        effect.grid ? effect.grid(frame, retroFrame) : std::vector<retro::GridLayer>{};
    if (layers.empty()) {
        drawIndices(cmd, effect, frame, retroFrame, effect.text);
        mapPalette(cmd, effect, frame, retroFrame, target);
        return;
    }

    // Composed: black, then each layer's source scaled into its rect. Consecutive layers of the same source
    // share one picture.
    target.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkClearColorValue black{{0.f, 0.f, 0.f, 1.f}};
    const VkImageSubresourceRange level0{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, target.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &level0);
    std::optional<Key> drawn;
    for (const retro::GridLayer& layer : layers) {
        const retro::EffectFrame& source = layer.source;
        const Key key{source.frame.chapter, static_cast<int>(source.effect), source.retroFrame};
        if (drawn != key) {
            const bool own = &effects_(source.effect) == &effect;
            const retro::EffectInfo& info = effects_(source.effect);
            if (!own && info.grid)
                throw std::logic_error("a grid layer's source composes a grid of its own");
            drawIndices(cmd, info, source.frame, source.retroFrame, own ? info.text : nullptr);
            mapPalette(cmd, info, source.frame, source.retroFrame, scratch_);
            drawn = key;
        }
        if (layer.to.width <= 0 || layer.to.height <= 0 || layer.from.width <= 0 || layer.from.height <= 0)
            continue;
        scratch_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        target.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const std::array<VkDescriptorImageInfo, 2> images = {
            VkDescriptorImageInfo{VK_NULL_HANDLE, scratch_.view(), VK_IMAGE_LAYOUT_GENERAL},
            VkDescriptorImageInfo{VK_NULL_HANDLE, target.view(), VK_IMAGE_LAYOUT_GENERAL}};
        tile_.bind(cmd, ring_, images, {});
        const TilePushConstants push{{layer.from.x, layer.from.y, layer.from.width, layer.from.height},
                                     {layer.to.x, layer.to.y, layer.to.width, layer.to.height},
                                     layer.gain,
                                     layer.masked ? 1u : 0u};
        tile_.push(cmd, &push, sizeof push);
        tile_.dispatch(cmd, static_cast<std::uint32_t>(layer.to.width),
                       static_cast<std::uint32_t>(layer.to.height));
    }
}

bool RetroStage::render(VkCommandBuffer cmd, const show::TimelineFrame& frame) {
    // The megamix's thank-you card replaces FINALE's grid for its frames; it is cached under its own key, so
    // the frames before and after it, in the same retro frame, still draw the grid.
    const bool card = show::thankYouCard(frame);
    const retro::EffectInfo& effect = card ? retro::finaleCardEffect() : effects_(frame.effect);
    retroFrame_ = retroFrameOf(frame, effect.retroRate);
    const Key key{frame.chapter, card ? kCardKey : static_cast<int>(frame.effect), retroFrame_};
    if (cached_ == key)
        return false;
    picturesDrawn_ = 0;

    if (!effect.phosphor) {
        drawPicture(cmd, effect, frame, retroFrame_, color_);
    } else {
        if (effect.grid)
            throw std::logic_error("an effect with a grid cannot also show phosphor");
        // The frames r - 3 .. r of this chapter: the older ones from the cache when it holds them, the
        // current one always drawn last, so the index image ends up holding its indices.
        constexpr std::size_t kFrames = retro::kPhosphorFrames + 1;
        std::array<std::optional<Key>, kFrames> wanted; // [k]: frame r - k
        for (std::uint32_t k = 0; k < kFrames; ++k)
            if (retroFrame_ >= k)
                wanted[k] = Key{key.chapter, key.effect, retroFrame_ - k};
        std::array<std::size_t, kFrames> slot{};
        std::array<bool, kFrames> found{};
        for (std::size_t k = 1; k < kFrames; ++k)
            for (std::size_t h = 0; h < held_.size() && wanted[k]; ++h)
                if (held_[h].key == wanted[k]) {
                    slot[k] = h;
                    found[k] = true;
                }
        // Free slots: those holding none of the frames wanted (the current one is redrawn anyway).
        const auto isWanted = [&](const std::optional<Key>& held) {
            for (std::size_t k = 1; k < kFrames; ++k)
                if (held && found[k] && wanted[k] == held)
                    return true;
            return false;
        };
        std::size_t next = 0;
        const auto freeSlot = [&]() {
            while (isWanted(held_[next].key))
                ++next;
            return next++;
        };
        for (std::size_t k = kFrames; k-- > 0;) {
            if (!wanted[k] || found[k])
                continue;
            slot[k] = freeSlot();
            held_[slot[k]].key = wanted[k];
            drawPicture(cmd, effect, frame, wanted[k]->retroFrame, held_[slot[k]].image);
        }

        const float decay =
            static_cast<float>(retro::phosphorDecay(effect.retroRate, effect.phosphorSeconds));
        PhosphorPushConstants push{};
        std::array<VkDescriptorImageInfo, 5> images{};
        for (std::size_t k = 0; k < kFrames; ++k) {
            gfx::Image& picture = held_[slot[wanted[k] ? k : 0]].image;
            picture.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                               VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            images[k] = {VK_NULL_HANDLE, picture.view(), VK_IMAGE_LAYOUT_GENERAL};
            if (k > 0)
                push.weights[k - 1] = wanted[k] ? std::pow(decay, static_cast<float>(k)) : 0.f;
        }
        color_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        images[4] = {VK_NULL_HANDLE, color_.view(), VK_IMAGE_LAYOUT_GENERAL};
        phosphor_.bind(cmd, ring_, images, {});
        phosphor_.push(cmd, &push, sizeof push);
        phosphor_.dispatch(cmd, kWidth, kHeight);
    }

    // The mip chain from the new picture, ready for the compositor's CRT pass (storage reads of level 0)
    // and for samplers.
    color_.generateMips(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    cached_ = key;
    return true;
}

} // namespace evo::render
