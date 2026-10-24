#include "text/text_pass.hpp"

#include "gfx/paths.hpp"
#include "text/font8x8.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace evo::text {

namespace {

// Must match the push_constant block of shaders/text/glyphs.glsl (4 bytes).
struct TextPushConstants {
    std::uint32_t first; // the first instance of the dispatch in the bound buffer
};
static_assert(sizeof(TextPushConstants) == 4);
static_assert(sizeof(TextPass::GlyphInstance) == 16, "the std430 layout of GlyphInstance");

constexpr std::array<gfx::Binding, 3> kBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER}, // the font atlas
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},          // the target
    gfx::Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},         // the glyph instances
};

constexpr std::uint32_t kInitialCapacity = 1024; // glyph instances per slot before any growth
constexpr std::uint32_t kMaxWorkgroups = 65535;  // the minimum maxComputeWorkGroupCount[0]
constexpr int kMaxScale = 64;

// A rectangle of pixels, right and bottom exclusive.
struct Rect {
    int left, top, right, bottom;
    bool overlaps(const Rect& other) const {
        return left < other.right && other.left < right && top < other.bottom && other.top < bottom;
    }
};

// The glyphs of an item that touch a width x height target, and the rectangle they cover.
struct Laid {
    std::vector<TextPass::GlyphInstance> glyphs;
    Rect bounds{0, 0, 0, 0};
};

Laid layout(const Item& item, VkExtent2D target) {
    Laid laid;
    const int scale = std::clamp(static_cast<int>(std::lround(item.scale)), 1, kMaxScale);
    const int cell = 8 * scale;
    // Far-off positions (a scroller long gone) are clamped well outside any image, never overflowing.
    const auto whole = [](float v) { return static_cast<int>(std::clamp(std::floor(v), -1.0e8f, 1.0e8f)); };
    const int left = whole(item.x);
    int x = left, y = whole(item.y);
    const Rect visible{0, 0, static_cast<int>(target.width), static_cast<int>(target.height)};
    for (std::uint8_t code : glyphCodes(item.text)) {
        if (code == '\n') {
            x = left;
            y += cell;
            continue;
        }
        const Rect glyphCell{x, y, x + cell, y + cell};
        if (code != ' ' && glyphCell.overlaps(visible)) {
            laid.glyphs.push_back(
                {x, y, static_cast<std::uint32_t>(code) | static_cast<std::uint32_t>(scale) << 8, item.rgba});
            Rect& b = laid.bounds;
            b = laid.glyphs.size() == 1
                    ? glyphCell
                    : Rect{std::min(b.left, x), std::min(b.top, y), std::max(b.right, glyphCell.right),
                           std::max(b.bottom, glyphCell.bottom)};
        }
        x += cell;
    }
    return laid;
}

void requireFormat(const gfx::Image& image, VkFormat format, const char* what) {
    if (image.format() != format)
        throw std::invalid_argument(std::string("TextPass: ") + what);
}

} // namespace

TextPass::TextPass(gfx::Context& ctx, gfx::FrameRing& ring)
    : ctx_(ctx), ring_(ring), atlas_(ctx),
      index_(ctx, gfx::shaderDir() / "text_text_index.comp.spv", kBindings, sizeof(TextPushConstants)),
      rgba_(ctx, gfx::shaderDir() / "text_text_rgba.comp.spv", kBindings, sizeof(TextPushConstants)),
      subscription_(ring.onBegin([this](std::uint32_t slot) { resetSlot(slot); })) {}

TextPass::~TextPass() {
    // Frames in flight may still read the instance buffers and the atlas. Best effort, as in ~Renderer.
    try {
        ring_.waitAll();
    } catch (const std::exception&) {
    }
}

void TextPass::resetSlot(std::uint32_t slot) {
    // begin() runs this once the slot's previous frame is done: its buffers are free to rewrite. A frame that
    // needed several buffers leaves one buffer as large as all of them.
    SlotBuffers& buffers = slots_[slot];
    if (buffers.buffers.size() > 1) {
        VkDeviceSize total = 0;
        for (const gfx::Buffer& buffer : buffers.buffers)
            total += buffer.size();
        buffers.buffers.clear();
        buffers.buffers.emplace_back(
            ctx_, gfx::BufferDesc{total, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "text glyph instances"});
    }
    buffers.used = 0;
}

void TextPass::drawIndex(VkCommandBuffer cmd, gfx::Image& indexImage, std::span<const Item> items) {
    requireFormat(indexImage, VK_FORMAT_R8_UINT, "drawIndex needs an R8_UINT image");
    draw(cmd, index_, indexImage, items, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

void TextPass::drawRgba(VkCommandBuffer cmd, gfx::Image& rgbaImage, std::span<const Item> items) {
    requireFormat(rgbaImage, VK_FORMAT_R8G8B8A8_UNORM, "drawRgba needs an R8G8B8A8_UNORM image");
    draw(cmd, rgba_, rgbaImage, items,
         VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

void TextPass::draw(VkCommandBuffer cmd, gfx::ComputePipeline& pass, gfx::Image& target,
                    std::span<const Item> items, VkAccessFlags2 targetAccess) {
    if (!ring_.recording())
        throw std::logic_error("TextPass: draw outside FrameRing::begin()/submit()");

    // Items go into one dispatch until one overlaps an item already in it: workgroups of a dispatch run in
    // no particular order, so that item waits for the next dispatch. The glyphs of one item never overlap.
    std::vector<GlyphInstance> batch;
    std::vector<Rect> batchBounds;
    for (const Item& item : items) {
        Laid laid = layout(item, target.extent());
        if (laid.glyphs.empty())
            continue;
        const bool overlaps = std::any_of(batchBounds.begin(), batchBounds.end(),
                                          [&](const Rect& r) { return r.overlaps(laid.bounds); });
        if (overlaps) {
            dispatch(cmd, pass, target, batch, targetAccess);
            batch.clear();
            batchBounds.clear();
        }
        batch.insert(batch.end(), laid.glyphs.begin(), laid.glyphs.end());
        batchBounds.push_back(laid.bounds);
    }
    dispatch(cmd, pass, target, batch, targetAccess);
}

void TextPass::dispatch(VkCommandBuffer cmd, gfx::ComputePipeline& pass, gfx::Image& target,
                        std::span<const GlyphInstance> glyphs, VkAccessFlags2 targetAccess) {
    if (glyphs.empty())
        return;
    const auto count = static_cast<std::uint32_t>(glyphs.size());

    // Room for the glyphs in this slot's last buffer, or a new one twice as large as needed.
    SlotBuffers& slot = slots_[ring_.slot()];
    const auto capacity = [](const gfx::Buffer& buffer) {
        return static_cast<std::uint32_t>(buffer.size() / sizeof(GlyphInstance));
    };
    if (slot.buffers.empty() || slot.used + count > capacity(slot.buffers.back())) {
        const std::uint32_t previous = slot.buffers.empty() ? 0 : capacity(slot.buffers.back());
        const std::uint32_t instances = std::max({kInitialCapacity, 2 * count, 2 * previous});
        slot.buffers.emplace_back(ctx_, gfx::BufferDesc{VkDeviceSize{instances} * sizeof(GlyphInstance),
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                                                        "text glyph instances"});
        slot.used = 0;
    }
    const gfx::Buffer& buffer = slot.buffers.back();
    const std::uint32_t first = slot.used;
    // Host writes before the submit are visible to the GPU through the submit itself.
    std::memcpy(static_cast<GlyphInstance*>(buffer.mapped()) + first, glyphs.data(), glyphs.size_bytes());
    buffer.flush();
    slot.used += count;

    atlas_.image().transition(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    // Waits for the target's last write, the previous dispatch of this draw included.
    target.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, targetAccess);

    const std::array<VkDescriptorImageInfo, 2> images = {
        VkDescriptorImageInfo{ctx_.nearestSampler(), atlas_.image().view(),
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, target.view(), VK_IMAGE_LAYOUT_GENERAL}};
    const VkDescriptorBufferInfo instances{buffer.handle(), 0, VK_WHOLE_SIZE};
    pass.bind(cmd, ring_, images, {&instances, 1});
    for (std::uint32_t start = 0; start < count; start += kMaxWorkgroups) {
        const TextPushConstants push{first + start};
        pass.push(cmd, &push, sizeof push);
        vkCmdDispatch(cmd, std::min(kMaxWorkgroups, count - start), 1, 1);
    }
}

} // namespace evo::text
