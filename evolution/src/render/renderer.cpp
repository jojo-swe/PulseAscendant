#include "render/renderer.hpp"

#include "text/font8x8.hpp"

#include <stdexcept>
#include <string>

namespace evo::render {

namespace {

// Checked before any member is built from the size.
const RenderSettings& checked(const RenderSettings& settings) {
    if (settings.width == 0 || settings.height == 0)
        throw std::invalid_argument("the render size must be positive, got " +
                                    std::to_string(settings.width) + "x" + std::to_string(settings.height));
    return settings;
}

constexpr std::uint32_t kTitleColor = 0xFFFFFFFFu;
constexpr std::uint32_t kCardColor = 0xFFE0E0E0u;
constexpr std::uint32_t kShadowColor = 0xFF000000u;
constexpr int kCardScale = 2;

// The item after its drop shadow.
void addWithShadow(std::vector<text::Item>& items, float x, float y, float scale, std::uint32_t rgba,
                   const char* text) {
    items.push_back({x + 1, y + 1, scale, kShadowColor, text});
    items.push_back({x, y, scale, rgba, text});
}

} // namespace

std::vector<text::Item> hudItems(const show::TimelineFrame& frame, std::uint32_t width,
                                 std::uint32_t height) {
    std::vector<text::Item> items;
    const auto empty = [](const char* text) { return text == nullptr || *text == '\0'; };
    constexpr float margin = kHudMargin;

    if (!empty(frame.title))
        addWithShadow(items, margin, margin, 1, kTitleColor, frame.title);

    // The cards' bottom row, shadow included, is the last one inside the margin.
    const float cardY = static_cast<float>(height) - margin - 8 * kCardScale - 1;
    if (frame.transition == show::Transition::Wipe) {
        const int nowWidth = empty(frame.nowCard) ? 0 : text::textWidth(frame.nowCard, kCardScale);
        if (nowWidth > 0)
            addWithShadow(items, margin, cardY, kCardScale, kCardColor, frame.nowCard);
        if (!empty(frame.thenCard)) {
            const int thenWidth = text::textWidth(frame.thenCard, kCardScale);
            const float thenX = static_cast<float>(width) - margin - 1 - static_cast<float>(thenWidth);
            // Too narrow for both on one line: the 1993 card goes a line up.
            const bool meets = nowWidth > 0 && thenX < margin + static_cast<float>(nowWidth + 8 * kCardScale);
            const float thenY = meets ? cardY - 10 * kCardScale : cardY;
            addWithShadow(items, thenX, thenY, kCardScale, kCardColor, frame.thenCard);
        }
    } else {
        const char* card = frame.era < .5f ? frame.thenCard : frame.nowCard;
        if (!empty(card))
            addWithShadow(items, margin, cardY, kCardScale, kCardColor, card);
    }
    return items;
}

Renderer::Renderer(gfx::Context& ctx, gfx::FrameRing& ring, const RenderSettings& settings)
    : ring_(ring), crt_(checked(settings).crt), hud_(settings.hud), text_(ctx, ring),
      retro_(ctx, ring, text_), modern_(ctx, ring, settings.width, settings.height),
      compositor_(ctx, ring, text_, settings.width, settings.height) {}

Renderer::~Renderer() {
    // The frames in flight still use the images and pipelines. Best effort, as in ~FrameRing: a wait that
    // fails (a lost device) has nothing left to protect.
    try {
        ring_.waitAll();
    } catch (const std::exception&) {
    }
}

void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    checked({width, height});
    const VkExtent2D current = output().extent();
    if (width == current.width && height == current.height)
        return;
    ring_.waitAll(); // resize: the frames in flight may still read the old images
    modern_.resize(width, height);
    compositor_.resize(width, height);
}

void Renderer::render(VkCommandBuffer cmd, const show::TimelineFrame& frame) {
    stats_.retroRendered = retro_.render(cmd, frame);
    stats_.retroFrame = retro_.retroFrame();
    modern_.render(cmd, frame);
    const VkExtent2D size = output().extent();
    const std::vector<text::Item> hud =
        hud_ ? hudItems(frame, size.width, size.height) : std::vector<text::Item>{};
    compositor_.render(cmd, retro_.image(), modern_.image(), frame, crt_, hud);
}

} // namespace evo::render
