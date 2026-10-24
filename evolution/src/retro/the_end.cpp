#include "retro/the_end.hpp"

#include <algorithm>
#include <cmath>

namespace evo::retro {

std::uint32_t theEndFadeFrames(const show::TimelineFrame& frame) {
    const double seconds = std::max(frame.chapterDuration - kTheEndBlackSeconds, 0.0);
    return static_cast<std::uint32_t>(std::floor(seconds * kTheEndRetroRate));
}

std::uint8_t theEndLevel(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::uint32_t end = theEndFadeFrames(frame);
    if (retroFrame >= end)
        return 0;
    // The DAC level, 63 at frame 0 down to 1 on the last lit frame (rounded up, so no lit frame is black).
    const std::uint32_t dac = (63 * (end - retroFrame) + end - 1) / end;
    return static_cast<std::uint8_t>((dac * 255 + 31) / 63);
}

void theEndPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    palette.fill(rgba(0, 0, 0));
    const std::uint8_t level = theEndLevel(frame, retroFrame);
    palette[kTheEndText] = rgba(level, level, level);
}

std::vector<text::Item> theEndText(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    if (theEndLevel(frame, retroFrame) == 0)
        return {};
    const std::string caption = kTheEndCaption;
    const float width = 8 * kTheEndScale * static_cast<float>(caption.size());
    return {
        {(320 - width) / 2, (200 - 8 * kTheEndScale) / 2, kTheEndScale, 0xFF000000u | kTheEndText, caption}};
}

} // namespace evo::retro
