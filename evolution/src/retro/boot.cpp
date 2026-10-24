#include "retro/boot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace evo::retro {

namespace {

// One write to the screen: at `at` seconds into the chapter, `text` appears all at once
// (charactersPerSecond 0) or one character after another, the first 1 / charactersPerSecond after `at`.
struct Write {
    double at;
    std::string_view text;
    std::uint32_t charactersPerSecond;
};

// The script, verbatim from the plan. Typing runs at 25 characters a second; the Loading dots come one every
// 0.25 s from 6.25 s, up to six, and stop at the flash (see bootFlashFrame).
constexpr std::uint32_t kTyping = 25;
constexpr std::array<Write, 10> kScript = {{
    {0.0, "C:\\>", 0},
    {0.8, "cd demo", kTyping},
    {1.6, "\nC:\\DEMO>", 0},
    {2.2, "evolution.exe", kTyping},
    {3.4, "\n\nEVOLUTION  1993 -> 2026\n", 0},
    {4.0, "Sound Blaster 16 at 220h, IRQ 5, DMA 1 ... OK\n", 0},
    {4.8, "Gravis Ultrasound ... not found\n", 0},
    {5.4, "VGA 320x200x256 ... OK\n", 0},
    {6.0, "Loading", 0},
    {6.0, "......", 4},
}};

constexpr std::uint32_t kFlashFrames = 2;
// The flash and the black after it: the last retro frames of the chapter.
constexpr double kCutFrames = 4;

// The first retro frame at or after `seconds`. Every script time is a whole number of 35 Hz frames; the
// epsilon keeps a product like 1.6 * 35 = 56.000000000000007 on its frame.
std::uint32_t frameAt(double seconds) {
    return static_cast<std::uint32_t>(std::ceil(seconds * kBootRetroRate - 1e-6));
}

// Everything written up to and including retro frame `retroFrame`: the script's text with its newlines.
std::string written(std::uint32_t retroFrame) {
    std::string text;
    for (const Write& write : kScript) {
        const std::uint32_t start = frameAt(write.at);
        if (retroFrame < start)
            break; // the script is in time order
        std::size_t count = write.text.size();
        if (write.charactersPerSecond > 0)
            count = std::min<std::size_t>(count,
                                          (retroFrame - start) * write.charactersPerSecond / kBootRetroRate);
        text.append(write.text.substr(0, count));
    }
    return text;
}

// The screen rows of a text: a row per line, a line longer than kBootColumns wrapped after its last space
// that fits (the space dropped), or hard at kBootColumns when it has none. The last row is where the cursor
// is, so a text ending in a newline ends in an empty row.
std::vector<std::string> rows(std::string_view text) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = std::min(text.find('\n', begin), text.size());
        std::string_view line = text.substr(begin, end - begin);
        while (line.size() > kBootColumns) {
            const std::size_t space = line.substr(0, kBootColumns + 1).rfind(' ');
            const std::size_t cut = space == std::string_view::npos || space == 0 ? kBootColumns : space;
            result.emplace_back(line.substr(0, cut));
            line.remove_prefix(cut == space ? cut + 1 : cut);
        }
        result.emplace_back(line);
        if (end == text.size())
            return result;
        begin = end + 1;
    }
}

bool cursorOn(std::uint32_t retroFrame) {
    // 2 Hz: on for the first quarter second of every half second.
    return retroFrame * 4 / kBootRetroRate % 2 == 0;
}

} // namespace

// The flash is the cut out of the prompt into FLYBY, so it belongs at the end of the chapter: its last four
// retro frames are two white ones and black. BOOT does not flash on its accent as such, because the accent
// rule (the strongest hit between 28 % and 76 % of the chapter) puts it at about 3 s, in the middle of the
// script, and the flash would blank everything after `evolution.exe`. The accent still wins if it ever comes
// later than that.
std::uint32_t bootFlashFrame(const show::TimelineFrame& frame) {
    const double at = std::max(frame.accentSeconds, frame.chapterDuration - kCutFrames / kBootRetroRate);
    return static_cast<std::uint32_t>(std::floor(std::max(at, 0.0) * kBootRetroRate));
}

std::vector<std::string> bootScreen(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    if (retroFrame >= bootFlashFrame(frame))
        return {};
    std::vector<std::string> screen = rows(written(retroFrame));
    if (screen.size() > 1 && screen.back().empty())
        screen.pop_back(); // the cursor's empty row
    return screen;
}

void bootPalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    // The 16 colours of the DOS text mode; BOOT draws in 7 on 0 and flashes 15.
    static constexpr std::array<std::uint32_t, 16> kDos = {
        rgba(0x00, 0x00, 0x00), rgba(0x00, 0x00, 0xAA), rgba(0x00, 0xAA, 0x00), rgba(0x00, 0xAA, 0xAA),
        rgba(0xAA, 0x00, 0x00), rgba(0xAA, 0x00, 0xAA), rgba(0xAA, 0x55, 0x00), rgba(0xAA, 0xAA, 0xAA),
        rgba(0x55, 0x55, 0x55), rgba(0x55, 0x55, 0xFF), rgba(0x55, 0xFF, 0x55), rgba(0x55, 0xFF, 0xFF),
        rgba(0xFF, 0x55, 0x55), rgba(0xFF, 0x55, 0xFF), rgba(0xFF, 0xFF, 0x55), rgba(0xFF, 0xFF, 0xFF),
    };
    palette.fill(rgba(0, 0, 0));
    std::copy(kDos.begin(), kDos.end(), palette.begin());
}

std::uint8_t bootBackground(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::uint32_t flash = bootFlashFrame(frame);
    return retroFrame >= flash && retroFrame < flash + kFlashFrames ? kBootWhite : kBootBlack;
}

std::vector<text::Item> bootText(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    if (retroFrame >= bootFlashFrame(frame))
        return {};
    constexpr std::uint32_t kGrey = 0xFF000000u | kBootGrey; // drawIndex writes the low byte
    const auto x = [](std::size_t column) { return static_cast<float>(kBootMargin + 8 * column); };
    const auto y = [](std::size_t row) { return static_cast<float>(kBootMargin + kBootLineHeight * row); };

    const std::vector<std::string> screen = rows(written(retroFrame));
    std::vector<text::Item> items;
    for (std::size_t row = 0; row < screen.size(); ++row)
        if (!screen[row].empty())
            items.push_back({x(0), y(row), 1, kGrey, screen[row]});
    if (cursorOn(retroFrame))
        items.push_back({x(screen.back().size()), y(screen.size() - 1), 1, kGrey, "_"});
    return items;
}

} // namespace evo::retro
