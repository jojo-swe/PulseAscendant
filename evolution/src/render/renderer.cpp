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
// The gap between the 1993 card raised above the 2026 card and the 2026 card, shadow included.
constexpr int kCardLineGap = 3;

// The item after its drop shadow.
void addWithShadow(std::vector<text::Item>& items, float x, float y, float scale, std::uint32_t rgba,
                   const char* text) {
    items.push_back({x + 1, y + 1, scale, kShadowColor, text});
    items.push_back({x, y, scale, rgba, text});
}

// An epoch card's scale: 2x, or 1x when at 2x it would not fit between the margins at this width.
int cardScale(const char* card, std::uint32_t width) {
    const int available = static_cast<int>(width) - 2 * kHudMargin - 1; // the shadow's column included
    return text::textWidth(card, kCardScale) <= available ? kCardScale : 1;
}

} // namespace

std::vector<text::Item> hudItems(const show::TimelineFrame& frame, std::uint32_t width,
                                 std::uint32_t height) {
    std::vector<text::Item> items;
    const auto empty = [](const char* text) { return text == nullptr || *text == '\0'; };
    constexpr float margin = kHudMargin;

    if (!empty(frame.title))
        addWithShadow(items, margin, margin, 1, kTitleColor, frame.title);

    // A card's top row at a scale: its bottom row, shadow included, is the last one inside the margin.
    const auto cardY = [height](int scale) {
        return static_cast<float>(height) - margin - static_cast<float>(8 * scale) - 1;
    };
    // Both cards only while the wipe is under way; before it the 1993 card, after it the 2026 card.
    const bool wiping = frame.transition == show::Transition::Wipe && frame.wipe > 0.f && frame.wipe < 1.f;
    if (wiping) {
        const int nowScale = empty(frame.nowCard) ? 0 : cardScale(frame.nowCard, width);
        const int nowWidth = nowScale == 0 ? 0 : text::textWidth(frame.nowCard, nowScale);
        if (nowWidth > 0)
            addWithShadow(items, margin, cardY(nowScale), static_cast<float>(nowScale), kCardColor,
                          frame.nowCard);
        if (!empty(frame.thenCard)) {
            const int thenScale = cardScale(frame.thenCard, width);
            const int thenWidth = text::textWidth(frame.thenCard, thenScale);
            const float thenX = static_cast<float>(width) - margin - 1 - static_cast<float>(thenWidth);
            // Too narrow for both on one line: the 1993 card goes a line up, above the 2026 card.
            const bool meets = nowWidth > 0 && thenX < margin + static_cast<float>(nowWidth + 8 * nowScale);
            const float thenY = meets ? cardY(nowScale) - static_cast<float>(8 * thenScale + 1 + kCardLineGap)
                                      : cardY(thenScale);
            addWithShadow(items, thenX, thenY, static_cast<float>(thenScale), kCardColor, frame.thenCard);
        }
    } else {
        const bool now = frame.transition == show::Transition::Wipe ? frame.wipe >= 1.f : frame.era >= .5f;
        const char* card = now ? frame.nowCard : frame.thenCard;
        if (!empty(card)) {
            const int scale = cardScale(card, width);
            addWithShadow(items, margin, cardY(scale), static_cast<float>(scale), kCardColor, card);
        }
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
