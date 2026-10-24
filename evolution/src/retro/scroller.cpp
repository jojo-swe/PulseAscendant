#include "retro/scroller.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace evo::retro {

namespace {

constexpr double kWidth = 320; // the retro stage's index image
constexpr double kHeight = 200;
constexpr double kGlyph = 8; // pixels from one glyph to the next, and a glyph's height

// An item's rgba whose low byte, the palette index, is `index`.
constexpr std::uint32_t indexColour(std::uint8_t index) {
    return 0xFF000000u | index;
}

} // namespace

std::vector<text::Item> scrollerItems(std::string_view text, std::uint32_t retroFrame, float speedPxPerSec,
                                      float amplitude, float wavelength, float baseY, std::uint8_t index,
                                      bool shadow) {
    std::vector<text::Item> items;
    if (text.empty())
        return items;

    // The scroll, wrapped once the whole text has passed: one screen width plus the text's width.
    const double t = static_cast<double>(retroFrame) / kScrollerRetroRate;
    const double loop = kWidth + kGlyph * static_cast<double>(text.size());
    double scrolled = std::fmod(static_cast<double>(speedPxPerSec) * t, loop);
    if (scrolled < 0)
        scrolled += loop;
    const double phase = scrolled * .5;
    // Glyph i stands at first + 8 i: rounding the start once keeps the glyphs exactly 8 pixels apart.
    const double first = std::round(kWidth - scrolled);

    for (std::size_t i = 0; i < text.size(); ++i) {
        const double x = first + kGlyph * static_cast<double>(i);
        if (x <= -kGlyph || text[i] == ' ')
            continue;
        if (x >= kWidth)
            break;
        const double y = std::round(baseY + amplitude * std::sin((x + phase) / wavelength));
        if (y <= -kGlyph || y >= kHeight)
            continue;
        items.push_back(
            {static_cast<float>(x), static_cast<float>(y), 1, indexColour(index), std::string(1, text[i])});
    }
    if (shadow) {
        std::vector<text::Item> shadowed;
        shadowed.reserve(2 * items.size());
        for (const text::Item& glyph : items)
            shadowed.push_back({glyph.x + 1, glyph.y + 1, 1, indexColour(kScrollerShadowIndex), glyph.text});
        shadowed.insert(shadowed.end(), items.begin(), items.end());
        return shadowed;
    }
    return items;
}

void creditsPalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    palette.fill(rgba(0, 0, 0));
    palette[kCreditsBlue] = rgba(0x00, 0x00, 0x40);
    palette[kCreditsWhite] = rgba(0xFF, 0xFF, 0xFF);
}

std::uint8_t creditsBackground(const show::TimelineFrame&, std::uint32_t) {
    return kCreditsBlue;
}

std::vector<text::Item> creditsText(const show::TimelineFrame&, std::uint32_t retroFrame) {
    const float titleX =
        static_cast<float>((kWidth - kGlyph * static_cast<double>(kCreditsTitle.size())) / 2);
    std::vector<text::Item> items = {
        {titleX, kCreditsTitleY, 1, indexColour(kCreditsWhite), std::string(kCreditsTitle)}};
    const std::vector<text::Item> scroller = scrollerItems(
        kCreditsText, retroFrame, kCreditsSpeed, kCreditsAmplitude, kCreditsWavelength, kCreditsBaseY);
    items.insert(items.end(), scroller.begin(), scroller.end());
    return items;
}

std::vector<text::Item> flybyText(const show::TimelineFrame&, std::uint32_t retroFrame) {
    return scrollerItems(kFlybyText, retroFrame, kFlybySpeed, kFlybyAmplitude, kFlybyWavelength, kFlybyBaseY,
                         kFlybyIndex, /*shadow=*/true);
}

} // namespace evo::retro
