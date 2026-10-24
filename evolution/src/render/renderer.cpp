#include "render/renderer.hpp"

#include "text/font8x8.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace evo::render {

namespace {

// Checked before any member is built from the size.
const RenderSettings& checked(const RenderSettings& settings) {
    if (settings.width == 0 || settings.height == 0)
        throw std::invalid_argument("the render size must be positive, got " +
                                    std::to_string(settings.width) + "x" + std::to_string(settings.height));
    if (settings.spp == 0)
        throw std::invalid_argument("the samples per pixel must be positive");
    return settings;
}

constexpr std::uint32_t kTitleColor = 0xFFFFFFFFu;
constexpr std::uint32_t kCardColor = 0xFFE0E0E0u;
constexpr std::uint32_t kShadowColor = 0xFF000000u;
// The gap, in units, between the 1993 card raised above the 2026 card and the 2026 card, shadow included.
constexpr int kCardLineGapUnits = 3;

} // namespace

int hudUnit(std::uint32_t height) {
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(height) / 360.0)));
}

std::vector<text::Item> hudItems(const show::TimelineFrame& frame, std::uint32_t width, std::uint32_t height,
                                 bool title) {
    std::vector<text::Item> items;
    const auto empty = [](const char* text) { return text == nullptr || *text == '\0'; };
    const int unit = hudUnit(height);
    const int margin = kHudMarginUnits * unit;
    const auto px = [](int pixels) { return static_cast<float>(pixels); };

    // The item after its drop shadow, one unit right and down.
    const auto addWithShadow = [&](int x, int y, int scale, std::uint32_t rgba, const char* text) {
        items.push_back({px(x + unit), px(y + unit), px(scale), kShadowColor, text});
        items.push_back({px(x), px(y), px(scale), rgba, text});
    };
    // An epoch card's scale: 2 units, or 1 when at 2 it would cross the right margin (the shadow included).
    const auto cardScale = [&](const char* card) {
        const int available = static_cast<int>(width) - 2 * margin - unit;
        return text::textWidth(card, 2 * unit) <= available ? 2 * unit : unit;
    };
    // A card's top row at a scale: its bottom row, shadow included, is the last one inside the margin.
    const auto cardY = [&](int scale) { return static_cast<int>(height) - margin - 8 * scale - unit; };

    if (title && !empty(frame.title))
        addWithShadow(margin, margin, unit, kTitleColor, frame.title);

    // Both cards only while the wipe is under way; before it the 1993 card, after it the 2026 card.
    const bool wiping = frame.transition == show::Transition::Wipe && frame.wipe > 0.f && frame.wipe < 1.f;
    if (wiping) {
        const int nowScale = empty(frame.nowCard) ? 0 : cardScale(frame.nowCard);
        const int nowWidth = nowScale == 0 ? 0 : text::textWidth(frame.nowCard, nowScale);
        if (nowWidth > 0)
            addWithShadow(margin, cardY(nowScale), nowScale, kCardColor, frame.nowCard);
        if (!empty(frame.thenCard)) {
            const int thenScale = cardScale(frame.thenCard);
            const int thenWidth = text::textWidth(frame.thenCard, thenScale);
            const int thenX = static_cast<int>(width) - margin - unit - thenWidth;
            // Too narrow for both on one line: the 1993 card goes a line up, above the 2026 card.
            const bool meets = nowWidth > 0 && thenX < margin + nowWidth + 8 * nowScale;
            const int thenY = meets ? cardY(nowScale) - (8 * thenScale + unit + kCardLineGapUnits * unit)
                                    : cardY(thenScale);
            addWithShadow(thenX, thenY, thenScale, kCardColor, frame.thenCard);
        }
    } else {
        const bool now = frame.transition == show::Transition::Wipe ? frame.wipe >= 1.f : frame.era >= .5f;
        const char* card = now ? frame.nowCard : frame.thenCard;
        if (!empty(card)) {
            const int scale = cardScale(card);
            addWithShadow(margin, cardY(scale), scale, kCardColor, card);
        }
    }
    return items;
}

Renderer::Renderer(gfx::Context& ctx, gfx::FrameRing& ring, const RenderSettings& settings)
    : ring_(ring), crt_(checked(settings).crt), hud_(settings.hud), hudTitle_(settings.hudTitle),
      mode_(settings.mode), spp_(settings.spp),
      maxBounces_(resolveMaxBounces(settings.mode, settings.maxBounces)), text_(ctx, ring),
      retro_(ctx, ring, text_), modern_(ctx, ring, settings.width, settings.height),
      compositor_(ctx, ring, text_, settings.width, settings.height) {}

Renderer::~Renderer() {
    // The frames in flight still use the images and pipelines. Best effort, as in ~FrameRing: a wait that
    // fails (a lost device) has nothing left to protect.
    try {
        ring_.waitAll(); // shutdown
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
    history_.markResize(); // reported even if the size is back to the last frame's when the stage runs
}

void Renderer::render(VkCommandBuffer cmd, const show::TimelineFrame& frame) {
    const StagePlan plan = planStages(frame, mode_, modern_.samplesRetro(frame));
    const VkExtent2D size = output().extent();

    stats_.retroRendered = plan.retro && retro_.render(cmd, frame);
    stats_.retroFrame = plan.retro ? retro_.retroFrame() : 0u;

    stats_.modernRendered = plan.modern;
    stats_.historyReset = HistoryReset::None;
    if (plan.modern) {
        const HistoryKey key{
            frame.chapter, modern_.shotFor(frame), frame.showSeconds, size.width, size.height, mode_, spp_};
        stats_.historyReset = history_.advance(key, !modernRanLastFrame_);
        modern_.render(cmd, frame, {stats_.historyReset, history_.framesSinceReset()});
    }
    stats_.framesSinceReset = plan.modern ? history_.framesSinceReset() : 0u;
    modernRanLastFrame_ = plan.modern;

    const std::uint32_t visible =
        (plan.retroVisible ? kRetroVisible : 0u) | (plan.modernVisible ? kModernVisible : 0u);
    const std::vector<text::Item> hud =
        hud_ ? hudItems(frame, size.width, size.height, hudTitle_) : std::vector<text::Item>{};
    compositor_.render(cmd, retro_.image(), modern_.image(), frame, crt_, visible, hud);
}

} // namespace evo::render
