#include "retro/portal.hpp"

#include "retro/boot.hpp"
#include "retro/finale.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace evo::retro {

namespace {

std::uint32_t frameOf(double seconds) {
    return static_cast<std::uint32_t>(std::floor(std::max(seconds, 0.0) * kPortalRetroRate + 1e-9));
}

// The position of a cell in kPortalOrder.
int orderOf(int cell) {
    for (std::size_t i = 0; i < kPortalOrder.size(); ++i)
        if (kPortalOrder[i] == cell)
            return static_cast<int>(i);
    return 0;
}

} // namespace

std::uint32_t portalOffFrame(const show::TimelineFrame& frame, int cell) {
    return frameOf(chapterBeat(frame, kPortalFirstOff + orderOf(cell)));
}

std::uint32_t portalPromptFrame(const show::TimelineFrame& frame) {
    return frameOf(chapterBeat(frame, kPortalPromptBeat));
}

Collapse portalCollapse(const PixelRect& cell, std::uint32_t n) {
    int width = cell.width, height = cell.height;
    switch (n) {
    case 0:
        height = (cell.height + 1) / 3;
        break;
    case 1:
        height = 4;
        break;
    case 2:
        height = 2;
        break;
    case 3:
        height = 1;
        width = static_cast<int>(std::lround(0.45 * cell.width));
        break;
    case 4:
        height = 1;
        width = static_cast<int>(std::lround(0.12 * cell.width));
        break;
    case 5:
        height = 1;
        width = 2;
        break;
    default:
        return {{cell.x + cell.width / 2, cell.y + cell.height / 2, 0, 0}, 0};
    }
    const PixelRect rect{cell.x + (cell.width - width) / 2, cell.y + (cell.height - height) / 2, width,
                         height};
    const double gain = static_cast<double>(cell.width) * cell.height / (static_cast<double>(width) * height);
    return {rect, static_cast<float>(std::min(gain, 64.0))};
}

std::string portalPrompt(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::uint32_t start = portalPromptFrame(frame);
    if (retroFrame < start)
        return {};
    const std::string text = kPortalPromptText;
    const std::size_t typed = (retroFrame - start) * kPortalPromptRate / kPortalRetroRate;
    return text.substr(0, std::min(typed, text.size()));
}

std::vector<GridLayer> portalGrid(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::uint32_t elapsed = finaleElapsed(frame, retroFrame);
    const EffectFrame own{frame.effect, frame, retroFrame};
    std::vector<GridLayer> layers;
    const auto add = [&](const EffectFrame& source, const PixelRect& from, int cell) {
        const PixelRect rect = finaleCell(cell);
        const std::uint32_t off = portalOffFrame(frame, cell);
        if (retroFrame < off) {
            layers.push_back({source, from, rect});
            return;
        }
        const Collapse c = portalCollapse(rect, retroFrame - off);
        if (c.rect.width > 0)
            layers.push_back({source, from, c.rect, c.gain});
    };
    for (std::size_t tile = 0; tile < kFinaleTiles.size(); ++tile)
        add(finaleTileSource(kFinaleTiles[tile], elapsed), kFinaleSource, finaleTileCell(tile));
    const std::uint32_t centreGone = portalOffFrame(frame, kFinaleCentre) + kPortalCollapseFrames;
    if (retroFrame < centreGone) {
        add(own, finaleCell(kFinaleCentre), kFinaleCentre);
        const PixelRect card = cardRect(frame, retroFrame, kPortalRetroRate);
        if (card.width > 0)
            layers.push_back({own, card, card});
    } else {
        // Nothing left of the grid: the whole screen is PORTAL's own (black, the prompt, the card).
        layers.push_back({own, {0, 0, 320, 200}, {0, 0, 320, 200}});
    }
    return layers;
}

void portalScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    cardCursor(scene, frame, retroFrame, kPortalRetroRate, kFinaleWhite);
}

std::vector<text::Item> portalText(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    std::vector<text::Item> items;
    const std::uint32_t centreGone = portalOffFrame(frame, kFinaleCentre) + kPortalCollapseFrames;
    if (retroFrame < centreGone)
        items = finaleScroller(finaleElapsed(frame, retroFrame));
    else {
        const std::string typed = portalPrompt(frame, retroFrame);
        constexpr std::uint32_t kWhite = 0xFF000000u | kFinaleWhite;
        if (!typed.empty())
            items.push_back({kPortalPromptX, kPortalPromptY, 1, kWhite, typed});
        if (beatCursorOn(frame, retroFrame, kPortalRetroRate))
            items.push_back({static_cast<float>(kPortalPromptX + 8 * static_cast<int>(typed.size())),
                             kPortalPromptY, 1, kWhite, "_"});
    }
    const std::vector<text::Item> card = cardText(frame, retroFrame, kPortalRetroRate, kFinaleWhite);
    items.insert(items.end(), card.begin(), card.end());
    return items;
}

} // namespace evo::retro
