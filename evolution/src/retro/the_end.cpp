#include "retro/the_end.hpp"

#include "retro/boot.hpp"
#include "show/era.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace evo::retro {

namespace {

std::uint32_t frameOf(double seconds) {
    return static_cast<std::uint32_t>(std::floor(std::max(seconds, 0.0) * kTheEndRetroRate + 1e-9));
}

constexpr int kTextWidth = 8 * 26; // the caption's cells

} // namespace

std::uint32_t theEndOffFrame(const show::TimelineFrame& frame) {
    return frameOf(frame.accentSeconds);
}

std::uint32_t theEndDotFrame(const show::TimelineFrame& frame) {
    return theEndOffFrame(frame) + 5;
}

std::uint32_t theEndBlackFrame(const show::TimelineFrame& frame) {
    const double bar = show::transitionMarks(frame).bar;
    return theEndDotFrame(frame) + static_cast<std::uint32_t>(std::lround(2 * bar * kTheEndRetroRate));
}

std::string theEndTyped(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::uint32_t start = frameOf(chapterDownbeat(frame, 0));
    if (retroFrame < start)
        return {};
    const std::string caption = kTheEndCaption;
    const std::size_t typed = (retroFrame - start) * kTheEndTypeRate / kTheEndRetroRate;
    return caption.substr(0, std::min(typed, caption.size()));
}

std::uint8_t theEndLevel(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::uint32_t dot = theEndDotFrame(frame), black = theEndBlackFrame(frame);
    if (retroFrame >= black)
        return 0;
    if (retroFrame <= dot)
        return 255;
    // The afterglow: exp(-5 u) rescaled to run from 1 at the dot to 0 at black, in the DAC's 64 steps (the
    // last one held until black).
    const double u = static_cast<double>(retroFrame - dot) / static_cast<double>(black - dot);
    const double level = (std::exp(-5 * u) - std::exp(-5.0)) / (1 - std::exp(-5.0));
    const int dac = std::max(1, static_cast<int>(std::lround(63 * level))); // lit until the black frame
    return static_cast<std::uint8_t>((dac * 255 + 31) / 63);
}

TheEndRect theEndLight(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::uint32_t off = theEndOffFrame(frame);
    if (retroFrame < off || theEndLevel(frame, retroFrame) == 0)
        return {160, 100, 0, 0};
    int width = kTextWidth, height = 2;
    switch (retroFrame - off) {
    case 0:
        height = 6;
        break;
    case 1:
    case 2:
        height = 2;
        break;
    case 3:
        width = static_cast<int>(std::lround(0.45 * kTextWidth));
        height = 1;
        break;
    case 4:
        width = static_cast<int>(std::lround(0.12 * kTextWidth));
        height = 1;
        break;
    default: // the dot
        width = 2;
        height = 2;
        break;
    }
    return {160 - width / 2, kTheEndY + 4 - height / 2, width, height};
}

void theEndPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    palette.fill(rgba(0, 0, 0));
    palette[kTheEndText] = rgba(0xFF, 0xFF, 0xFF);
    const std::uint8_t level = theEndLevel(frame, retroFrame);
    palette[kTheEndLight] = rgba(level, level, level);
}

void theEndScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const TheEndRect light = theEndLight(frame, retroFrame);
    addRect(scene, light.x, light.y, light.x + light.width, light.y + light.height, kTheEndLight);
}

std::vector<text::Item> theEndText(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    if (retroFrame >= theEndOffFrame(frame))
        return {};
    const std::string typed = theEndTyped(frame, retroFrame);
    constexpr std::uint32_t kColour = 0xFF000000u | kTheEndText;
    std::vector<text::Item> items;
    if (!typed.empty())
        items.push_back({kTheEndX, kTheEndY, 1, kColour, typed});
    if (beatCursorOn(frame, retroFrame, kTheEndRetroRate))
        items.push_back(
            {static_cast<float>(kTheEndX + 8 * static_cast<int>(typed.size())), kTheEndY, 1, kColour, "_"});
    return items;
}

} // namespace evo::retro
