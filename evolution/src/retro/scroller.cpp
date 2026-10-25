#include "retro/scroller.hpp"

#include "retro/boot.hpp"
#include "show/era.hpp"
#include "show/music_curves.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace evo::retro {

namespace {

constexpr double kWidth = 320; // the retro stage's index image
constexpr double kHeight = 200;
constexpr double kGlyph = 8; // pixels from one glyph to the next, and a glyph's height

double smoothstep01(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

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

std::vector<std::string> creditGlyphs(std::string_view utf8) {
    std::vector<std::string> glyphs;
    for (std::size_t i = 0; i < utf8.size();) {
        std::size_t next = i + 1;
        while (next < utf8.size() && (static_cast<unsigned char>(utf8[next]) & 0xC0u) == 0x80u)
            ++next;
        glyphs.emplace_back(utf8.substr(i, next - i));
        i = next;
    }
    return glyphs;
}

double creditEntry(const show::TimelineFrame& frame, int credit) {
    if (credit < static_cast<int>(kCreditsEntryBars.size()))
        return chapterDownbeat(frame, kCreditsEntryBars[static_cast<std::size_t>(credit)]);
    // THE END? on the bar line of the downbeat D before the accent.
    const show::TransitionMarks marks = show::transitionMarks(frame);
    return marks.downbeat - marks.start;
}

namespace {

// THE END?'s travel from its entry to its stop on the accent, in [0, 1] (1 from the accent on).
double endProgress(const show::TimelineFrame& frame, double seconds) {
    const double from = creditEntry(frame, 3), to = frame.accentSeconds;
    return std::clamp((seconds - from) / (to - from), 0.0, 1.0);
}

// The paper's DAC level: white at the chapter's first retro frame, black by its first beat after the start.
double paperLevel(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    // The chapter starts on a beat: the fade runs to the next one.
    const double first = chapterBeat(frame, 0);
    const double fade = first > 0.05 ? first : chapterBeat(frame, 1);
    const double u = std::clamp(retroFrame / (fade * kScrollerRetroRate), 0.0, 1.0);
    return (1 - u) * (1 - u);
}

} // namespace

double creditsEndX(const show::TimelineFrame& frame, double seconds) {
    const double from = creditEntry(frame, 3);
    const double length = frame.accentSeconds - from;
    if (seconds < from)
        return kWidth + kCreditsSpeed * (from - seconds);
    const double u = endProgress(frame, seconds);
    // Hermite from x = 320 moving left at the scroller's speed to the stop at rest.
    const double h00 = 2 * u * u * u - 3 * u * u + 1, h10 = u * u * u - 2 * u * u + u;
    const double h01 = -2 * u * u * u + 3 * u * u;
    return h00 * kWidth + h10 * length * -kCreditsSpeed + h01 * kCreditsEndX;
}

double creditsAmplitude(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const double show = show::chapterStart(frame.chapter) + (retroFrame + 0.5) / kScrollerRetroRate;
    const int track = show::trackAt(show);
    const float bass = show::envelope(track, show - show::trackStart(track), show::EnvelopeChannel::Bass);
    return kCreditsAmplitude + kCreditsBassAmp * std::clamp(static_cast<double>(bass), 0.0, 1.0);
}

void creditsPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    palette.fill(rgba(0, 0, 0));
    const int dac = static_cast<int>(std::lround(63 * paperLevel(frame, retroFrame)));
    const auto paper = static_cast<std::uint8_t>((dac * 255 + 31) / 63);
    palette[kCreditsPaper] = rgba(paper, paper, paper);
    palette[kCreditsStar + 0] = rgba(0x48, 0x48, 0x48);
    palette[kCreditsStar + 1] = rgba(0x88, 0x88, 0x88);
    palette[kCreditsStar + 2] = rgba(0xD0, 0xD0, 0xD0);
    palette[kCreditsWhite] = rgba(0xFF, 0xFF, 0xFF);
}

std::uint8_t creditsBackground(const show::TimelineFrame&, std::uint32_t) {
    return kCreditsPaper;
}

void creditsScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    if (paperLevel(frame, retroFrame) > 0)
        return; // no stars on the white
    // Three layers of 28 stars drifting left, the nearer ones brighter and faster; positions hashed from the
    // star's number, so the field is the same on every run.
    constexpr int kStars = 28;
    constexpr double kSpeeds[3] = {5, 11, 23}; // pixels a second
    const double t = static_cast<double>(retroFrame) / kScrollerRetroRate;
    for (int layer = 0; layer < 3; ++layer)
        for (int i = 0; i < kStars; ++i) {
            std::uint32_t h = static_cast<std::uint32_t>(layer * 7919 + i * 104729 + 12345);
            h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu, h ^= h >> 16;
            const double x0 = static_cast<double>(h % 320u);
            const double y = static_cast<double>((h >> 9) % 192u) + 4;
            double x = std::fmod(x0 - kSpeeds[layer] * t, kWidth);
            if (x < 0)
                x += kWidth;
            RetroPoint p{};
            p.x = toFixed(std::floor(x) + 0.5);
            p.y = toFixed(y + 0.5);
            p.index = static_cast<std::uint8_t>(kCreditsStar + layer);
            p.size = 1;
            scene.points.push_back(p);
        }
}

std::vector<text::Item> creditsText(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const double t = static_cast<double>(retroFrame) / kScrollerRetroRate;
    const double phase = t * kCreditsSpeed * .5;
    const double amplitude = creditsAmplitude(frame, retroFrame);
    std::vector<text::Item> items;
    const auto place = [&](const std::vector<std::string>& glyphs, double first, double wave) {
        // Glyph i at first + 8 i: rounding the start once keeps the glyphs exactly 8 pixels apart.
        const double start = std::round(first);
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            const double x = start + kGlyph * static_cast<double>(i);
            if (x <= -kGlyph || glyphs[i] == " ")
                continue;
            if (x >= kWidth)
                break;
            const double y = std::round(kCreditsBaseY + wave * std::sin((x + phase) / kCreditsWavelength));
            items.push_back(
                {static_cast<float>(x), static_cast<float>(y), 1, indexColour(kCreditsWhite), glyphs[i]});
        }
    };
    const std::string_view credits[3] = {kCreditsTitle, kCreditsLine, kCreditsGreetings};
    for (int c = 0; c < 3; ++c) {
        const double entry = creditEntry(frame, c);
        if (t < entry)
            continue;
        place(creditGlyphs(credits[c]), kWidth - kCreditsSpeed * (t - entry), amplitude);
    }
    if (t >= creditEntry(frame, 3))
        place(creditGlyphs(kCreditsEnd), creditsEndX(frame, t),
              amplitude * (1 - smoothstep01(endProgress(frame, t))));
    return items;
}

std::vector<text::Item> flybyText(const show::TimelineFrame&, std::uint32_t retroFrame) {
    return scrollerItems(kFlybyText, retroFrame, kFlybySpeed, kFlybyAmplitude, kFlybyWavelength, kFlybyBaseY,
                         kFlybyIndex, /*shadow=*/true);
}

} // namespace evo::retro
