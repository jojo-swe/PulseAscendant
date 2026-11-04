// The text pass on the GPU: glyphs from the atlas into an R8_UINT palette index image (drawIndex) and
// alpha-blended into an RGBA8 image (drawRgba), compared pixel for pixel with a CPU rendering of the same
// items from the font table. Runs with the validation layer on and fails on any message it reports.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/sync.hpp"
#include "test_env.hpp"
#include "text/font8x8.hpp"
#include "text/text_pass.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::text::Item;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

// A test target: an image the text pass draws into, cleared to a colour first and read back after.
class Target {
public:
    Target(evo::gfx::Context& ctx, std::uint32_t width, std::uint32_t height, VkFormat format)
        : width_(width), height_(height), texel_(format == VK_FORMAT_R8_UINT ? 1 : 4),
          image_(ctx, {width, height, format,
                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                       1, "text test target"}),
          buffer_(ctx, {VkDeviceSize{width} * height * texel_, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                        "text test readback"}) {}

    void clear(VkCommandBuffer cmd, VkClearColorValue color) {
        image_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, image_.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
    }

    void readback(VkCommandBuffer cmd) {
        image_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                          VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width_, height_, 1};
        vkCmdCopyImageToBuffer(cmd, image_.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_.handle(),
                               1, &region);
        evo::gfx::bufferBarrier2(cmd, buffer_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                 VK_ACCESS_2_HOST_READ_BIT);
    }

    std::vector<std::uint8_t> bytes() const {
        buffer_.invalidate();
        const auto* data = static_cast<const std::uint8_t*>(buffer_.mapped());
        return {data, data + buffer_.size()};
    }

    evo::gfx::Image& image() { return image_; }

private:
    std::uint32_t width_, height_, texel_;
    evo::gfx::Image image_;
    evo::gfx::Buffer buffer_;
};

// The CPU rendering the GPU must match: each glyph of an item 8 * scale pixels right of the last (a newline
// goes back to x and 8 * scale down), each set font pixel a scale x scale block, clipped to the image.
// `plot` gets every covered pixel inside the image.
template <typename Plot>
void reference(const Item& item, std::uint32_t width, std::uint32_t height, Plot plot) {
    const int scale = std::max(1, static_cast<int>(std::lround(item.scale)));
    const int x0 = static_cast<int>(std::floor(item.x));
    int x = x0, y = static_cast<int>(std::floor(item.y));
    for (std::uint8_t code : evo::text::glyphCodes(item.text)) {
        if (code == '\n') {
            x = x0;
            y += 8 * scale;
            continue;
        }
        const evo::text::GlyphRows& rows = evo::text::glyph(static_cast<char>(code));
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col) {
                if (!(rows[row] & (0x80u >> col)))
                    continue;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx) {
                        const int px = x + col * scale + sx, py = y + row * scale + sy;
                        if (px >= 0 && py >= 0 && px < static_cast<int>(width) &&
                            py < static_cast<int>(height))
                            plot(static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(py));
                    }
            }
        x += 8 * scale;
    }
}

std::vector<std::uint8_t> referenceIndex(std::uint32_t width, std::uint32_t height,
                                         const std::vector<Item>& items) {
    std::vector<std::uint8_t> pixels(std::size_t{width} * height, 0);
    for (const Item& item : items)
        reference(item, width, height, [&](std::uint32_t x, std::uint32_t y) {
            pixels[std::size_t{y} * width + x] = static_cast<std::uint8_t>(item.rgba & 0xffu);
        });
    return pixels;
}

class Fixture {
public:
    explicit Fixture(evo::gfx::Context& ctx) : ring_(ctx), text_(ctx, ring_) {}

    // Clears the target, draws the items with the index or the rgba variant and reads the target back.
    std::vector<std::uint8_t> draw(Target& target, VkClearColorValue clear, const std::vector<Item>& items,
                                   bool index) {
        VkCommandBuffer cmd = ring_.begin();
        target.clear(cmd, clear);
        if (index)
            text_.drawIndex(cmd, target.image(), items);
        else
            text_.drawRgba(cmd, target.image(), items);
        target.readback(cmd);
        ring_.wait(ring_.submit());
        return target.bytes();
    }

    evo::gfx::FrameRing& ring() { return ring_; }
    evo::text::TextPass& text() { return text_; }

private:
    evo::gfx::FrameRing ring_;
    evo::text::TextPass text_; // built on the ring; its destructor waits for the ring's submits
};

std::size_t countNonZero(const std::vector<std::uint8_t>& pixels) {
    return static_cast<std::size_t>(
        std::count_if(pixels.begin(), pixels.end(), [](auto p) { return p != 0; }));
}

// "A" at (8, 8), scale 1, index 15 into a 64x32 index image: the set pixels of glyph('A') at
// (8 + col, 8 + row) and nowhere else.
void testSingleGlyph(evo::gfx::Context& ctx, Fixture& fixture) {
    Target target(ctx, 64, 32, VK_FORMAT_R8_UINT);
    const std::vector<std::uint8_t> pixels = fixture.draw(target, {}, {{8, 8, 1, 15, "A"}}, true);
    const evo::text::GlyphRows& a = evo::text::glyph('A');
    std::size_t set = 0;
    for (std::uint32_t y = 0; y < 32; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) {
            const bool inGlyph = x >= 8 && x < 16 && y >= 8 && y < 16 && (a[y - 8] & (0x80u >> (x - 8)));
            if (pixels[y * 64 + x] != (inGlyph ? 15 : 0))
                throw std::runtime_error("pixel " + std::to_string(x) + "," + std::to_string(y) + " is " +
                                         std::to_string(pixels[y * 64 + x]));
            set += inGlyph;
        }
    CHECK(set >= 10);
    CHECK(countNonZero(pixels) == set);
}

// text running off the 320x200 frame on the right (x = 300), the left (x = -5) and the bottom
// (y = 195) is clipped per pixel: the image equals the clipped CPU rendering, nothing else changed.
void testClipping(evo::gfx::Context& ctx, Fixture& fixture) {
    Target target(ctx, 320, 200, VK_FORMAT_R8_UINT);
    const std::vector<Item> items = {
        {300, 20, 1, 15, "RIGHT EDGE"}, {-5, 60, 1, 7, "LEFT EDGE"}, {100, 195, 1, 9, "BOTTOM gjpqy"},
        {-5, 195, 1, 12, "CORNER"},     {316, -4, 1, 3, "TOP"},      {2000, 2000, 1, 5, "FAR AWAY"},
        {-500, -500, 3, 5, "FAR AWAY"},
    };
    const std::vector<std::uint8_t> pixels = fixture.draw(target, {}, items, true);
    CHECK(pixels == referenceIndex(320, 200, items));
    CHECK(countNonZero(pixels) > 100);
    // Every partly visible item left some pixels, with its own index.
    for (std::size_t i = 0; i < 5; ++i)
        CHECK(countNonZero(referenceIndex(320, 200, {items[i]})) > 0);
    CHECK(std::count(pixels.begin(), pixels.end(), std::uint8_t{7}) > 0);  // x = -5
    CHECK(std::count(pixels.begin(), pixels.end(), std::uint8_t{15}) > 0); // x = 300
}

// Scale 2 doubles each font pixel into a 2x2 block; a fractional position floors and a scale rounds.
void testScale(evo::gfx::Context& ctx, Fixture& fixture) {
    Target target(ctx, 64, 32, VK_FORMAT_R8_UINT);
    const std::vector<Item> items = {{4, 4, 2, 15, "A"}, {24.7f, 3.2f, 2.4f, 9, "Bg"}};
    const std::vector<std::uint8_t> pixels = fixture.draw(target, {}, items, true);
    CHECK(pixels == referenceIndex(64, 32, items));
    const evo::text::GlyphRows& a = evo::text::glyph('A');
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col) {
            const std::uint8_t expected = (a[row] & (0x80u >> col)) ? 15 : 0;
            for (int s = 0; s < 4; ++s)
                CHECK(pixels[std::size_t(4 + 2 * row + s / 2) * 64 + 4 + 2 * col + s % 2] == expected);
        }
}

// The rgba variant: a black shadow at +1,+1 drawn first, then white text over it, then a half-transparent
// red item over the blue background.
void testRgba(evo::gfx::Context& ctx, Fixture& fixture) {
    constexpr std::uint32_t kW = 96, kH = 32;
    Target target(ctx, kW, kH, VK_FORMAT_R8G8B8A8_UNORM);
    const std::vector<Item> items = {
        {9, 9, 1, 0xFF000000u, "Hi!"},
        {8, 8, 1, 0xFFFFFFFFu, "Hi!"},
        {50, 10, 1, 0x800000FFu, "XY"}, // red in the low byte, alpha 0x80
    };
    VkClearColorValue blue{};
    blue.float32[2] = 1.0f;
    blue.float32[3] = 1.0f;
    const std::vector<std::uint8_t> pixels = fixture.draw(target, blue, items, false);

    // The expected image, painted in order.
    std::vector<std::uint32_t> expected(kW * kH, 0xFFFF0000u); // blue (red in the low byte)
    reference(items[0], kW, kH, [&](auto x, auto y) { expected[y * kW + x] = 0xFF000000u; });
    reference(items[1], kW, kH, [&](auto x, auto y) { expected[y * kW + x] = 0xFFFFFFFFu; });
    std::size_t blended = 0;
    reference(items[2], kW, kH, [&](auto x, auto y) {
        expected[y * kW + x] = 0xFF7F0080u; // half red over blue: (128, 0, 127)
        ++blended;
    });
    CHECK(blended > 10);
    for (std::uint32_t i = 0; i < kW * kH; ++i)
        for (int c = 0; c < 4; ++c) {
            const int want = static_cast<int>((expected[i] >> (8 * c)) & 0xffu);
            if (std::abs(pixels[i * 4 + c] - want) > 1)
                throw std::runtime_error("rgba pixel " + std::to_string(i % kW) + "," +
                                         std::to_string(i / kW) + " channel " + std::to_string(c) + " is " +
                                         std::to_string(pixels[i * 4 + c]) + ", expected " +
                                         std::to_string(want));
        }
    ++checksPassed;
}

// More glyphs than the instance buffers first hold, in several draws per frame, over more frames than the
// ring has slots: the buffers grow, a slot's buffer is never rewritten while its last frame may read it, and
// every frame still matches the reference.
void testManyGlyphs(evo::gfx::Context& ctx, Fixture& fixture) {
    Target target(ctx, 320, 200, VK_FORMAT_R8_UINT);
    for (int frame = 0; frame < 4; ++frame) {
        std::vector<Item> items;
        for (int line = 0; line < 25; ++line) {
            std::string text;
            for (int i = 0; i < 40; ++i)
                text += static_cast<char>(33 + (line * 40 + i + frame * 7) % 94);
            items.push_back({0, static_cast<float>(line * 8), 1, static_cast<std::uint32_t>(1 + line), text});
        }
        // Twice over, as two draws: 2000 glyphs in the frame.
        VkCommandBuffer cmd = fixture.ring().begin();
        target.clear(cmd, {});
        fixture.text().drawIndex(cmd, target.image(), items);
        fixture.text().drawIndex(cmd, target.image(), items);
        target.readback(cmd);
        fixture.ring().wait(fixture.ring().submit());
        CHECK(target.bytes() == referenceIndex(320, 200, items));
    }
}

// The batch split on the CPU: which items share a dispatch, in draw order.
void testSplitBatches() {
    using evo::text::splitBatches;
    const VkExtent2D target{320, 200};
    const auto indices = [](const evo::text::GlyphBatch& batch) { return batch.items; };
    using Indices = std::vector<std::size_t>;

    // A shadow at +1,+1 and its text overlap: the shadow's dispatch first, the text's after it.
    const std::vector<Item> shadowed = {{9, 9, 1, 0xFF000000u, "Hi!"}, {8, 8, 1, 0xFFFFFFFFu, "Hi!"}};
    auto batches = splitBatches(shadowed, target);
    CHECK(batches.size() == 2 && indices(batches[0]) == Indices{0} && indices(batches[1]) == Indices{1});
    CHECK(batches[0].glyphs.size() == 3 && batches[0].glyphs[0].x == 9 && batches[0].glyphs[0].y == 9);
    CHECK(batches[1].glyphs.size() == 3 && batches[1].glyphs[0].x == 8 && batches[1].glyphs[2].x == 24);
    CHECK(batches[1].glyphs[0].codeScale == ('H' | 1u << 8) && batches[1].glyphs[0].rgba == 0xFFFFFFFFu);

    // Items that do not overlap share one dispatch, their glyphs in item order; cells that merely touch
    // (an item ending at x = 16, the next starting there) do not overlap.
    const std::vector<Item> apart = {{0, 0, 1, 1, "AB"}, {16, 0, 1, 2, "CD"}, {0, 8, 2, 3, "EF"}};
    batches = splitBatches(apart, target);
    CHECK(batches.size() == 1 && indices(batches[0]) == (Indices{0, 1, 2}) && batches[0].glyphs.size() == 6);
    CHECK(batches[0].glyphs[2].x == 16 && batches[0].glyphs[4].y == 8 && batches[0].glyphs[5].x == 16);
    CHECK(batches[0].glyphs[5].codeScale == ('F' | 2u << 8));

    // Three mutually overlapping items: three dispatches.
    const std::vector<Item> stacked = {{0, 0, 1, 1, "AAA"}, {4, 4, 1, 2, "BBB"}, {8, 2, 1, 3, "CCC"}};
    batches = splitBatches(stacked, target);
    CHECK(batches.size() == 3);
    for (std::size_t i = 0; i < 3; ++i)
        CHECK(indices(batches[i]) == Indices{i} && batches[i].glyphs.size() == 3);

    // Only the current batch counts: C overlaps A (in the batch before) but not B, so it joins B.
    const std::vector<Item> chain = {{0, 0, 1, 1, "AA"}, {8, 4, 1, 2, "B"}, {0, 12, 1, 3, "C"}};
    batches = splitBatches(chain, target);
    CHECK(batches.size() == 2 && indices(batches[0]) == Indices{0} && indices(batches[1]) == (Indices{1, 2}));

    // Nothing to draw: empty, blank, off the target, or a position or scale that is not finite. A valid item
    // among them keeps its own index.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<Item> nothing = {
        {0, 0, 1, 1, ""},     {0, 0, 1, 1, "   "}, {-100, -100, 1, 1, "X"}, {400, 0, 1, 1, "X"},
        {nan, 0, 1, 1, "X"},  {0, nan, 1, 1, "X"}, {0, 0, nan, 1, "X"},     {inf, 0, 1, 1, "X"},
        {0, -inf, 1, 1, "X"}, {0, 0, inf, 1, "X"}, {0, 0, -inf, 1, "X"},
    };
    CHECK(splitBatches(nothing, target).empty());
    std::vector<Item> mixed = nothing;
    mixed.push_back({4, 4, 1, 9, "OK"});
    batches = splitBatches(mixed, target);
    CHECK(batches.size() == 1 && indices(batches[0]) == Indices{nothing.size()} &&
          batches[0].glyphs.size() == 2);
}

// Items with a position or scale that is not finite draw nothing, beside one that draws.
void testNonFinite(evo::gfx::Context& ctx, Fixture& fixture) {
    Target target(ctx, 64, 32, VK_FORMAT_R8_UINT);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<Item> items = {{nan, 8, 1, 7, "NAN"},
                                     {8, inf, 1, 7, "INF"},
                                     {8, 8, nan, 7, "NAN"},
                                     {8, 8, inf, 7, "INF"},
                                     {8, 8, 1, 15, "A"}};
    const std::vector<std::uint8_t> pixels = fixture.draw(target, {}, items, true);
    CHECK(pixels == referenceIndex(64, 32, {items.back()}));
    CHECK(std::count(pixels.begin(), pixels.end(), std::uint8_t{7}) == 0);
}

void testEmpty(evo::gfx::Context& ctx, Fixture& fixture) {
    Target target(ctx, 32, 32, VK_FORMAT_R8_UINT);
    CHECK(countNonZero(fixture.draw(target, {}, {}, true)) == 0);
    CHECK(countNonZero(fixture.draw(target, {}, {{0, 0, 1, 15, ""}, {0, 0, 1, 15, "   "}}, true)) == 0);
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testSplitBatches();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            {
                Fixture fixture(ctx);
                testSingleGlyph(ctx, fixture);
                testClipping(ctx, fixture);
                testScale(ctx, fixture);
                testRgba(ctx, fixture);
                testManyGlyphs(ctx, fixture);
                testEmpty(ctx, fixture);
                testNonFinite(ctx, fixture);
            }
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("text_pass_test: %d checks passed\n", checksPassed);
    return 0;
}
