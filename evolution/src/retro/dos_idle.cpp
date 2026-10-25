#include "retro/dos_idle.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace evo::retro {

namespace {

// DIR's output in MS-DOS 6's layout (names padded to 8, extensions to 3, sizes right-aligned in 10 columns),
// every row inside kBootColumns. The sizes add up.
constexpr std::array<const char*, 15> kScreen = {
    "C:\\DEMO>dir",
    "",
    " Volume in drive C is DEMOSCENE",
    " Directory of C:\\DEMO",
    "",
    ".            <DIR>     10-24-93",
    "..           <DIR>     10-24-93",
    "EVOLUTN  EXE   199,326 10-24-93",
    "MUSIC    MOD   711,160 10-24-93",
    "README   TXT     2,026 10-24-93",
    "CLAP     WAV         0 10-24-93",
    "        4 file(s)    912,512 bytes",
    "                   2,026,993 bytes free",
    "",
    kDosIdlePrompt,
};

} // namespace

std::vector<std::string> dosIdleScreen(const show::TimelineFrame& /*frame*/, std::uint32_t /*retroFrame*/) {
    return {kScreen.begin(), kScreen.end()};
}

std::vector<text::Item> dosIdleText(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    constexpr std::uint32_t kGrey = 0xFF000000u | kBootGrey; // drawIndex writes the low byte
    const auto x = [](std::size_t column) { return static_cast<float>(kBootMargin + 8 * column); };
    const auto y = [](std::size_t row) { return static_cast<float>(kBootMargin + kBootLineHeight * row); };
    const std::vector<std::string> screen = dosIdleScreen(frame, retroFrame);
    std::vector<text::Item> items;
    for (std::size_t row = 0; row < screen.size(); ++row)
        if (!screen[row].empty())
            items.push_back({x(0), y(row), 1, kGrey, screen[row]});
    if (promptCursorOn(retroFrame))
        items.push_back({x(screen.back().size()), y(screen.size() - 1), 1, kGrey, "_"});
    return items;
}

// --- RETURN ----------------------------------------------------------------------------------------------

namespace {

// The beat that ends the switch-on flash: the first after the accent (by more than a tenth of a second, so
// an accent on a beat still gets a whole one), and the last beat of the chapter.
int flashEndBeat(const show::TimelineFrame& frame) {
    int beat = 0;
    while (chapterBeat(frame, beat) <= frame.accentSeconds + 0.1)
        ++beat;
    return beat;
}
int lastBeat(const show::TimelineFrame& frame) {
    int beat = 0;
    while (chapterBeat(frame, beat + 1) < frame.chapterDuration - 1e-6 &&
           chapterBeat(frame, beat + 1) > chapterBeat(frame, beat))
        ++beat;
    return beat;
}

std::uint32_t frameOf(double seconds) {
    return static_cast<std::uint32_t>(std::floor(std::max(seconds, 0.0) * kBootRetroRate + 1e-9));
}

std::vector<PromptWrite> returnScript(const show::TimelineFrame& frame) {
    const auto on = [](double seconds) { return frameStart(seconds, kBootRetroRate); };
    const auto beat = [&frame](double i) {
        const int whole = static_cast<int>(std::floor(i));
        const double a = chapterBeat(frame, whole);
        return i == whole ? a : a + (i - whole) * (chapterBeat(frame, whole + 1) - a);
    };
    const int post = flashEndBeat(frame); // the POST screen when the flash has faded
    // The prompt on the first bar line after the BIOS line.
    double prompt = chapterDownbeat(frame, 0);
    for (int bar = 1; prompt <= chapterBeat(frame, post) + 1e-6; ++bar)
        prompt = chapterDownbeat(frame, bar);
    const int last = lastBeat(frame);
    static constexpr std::string_view kCommand = kRebootCommand; // "EVOLUTION.EXE /2026"
    const std::size_t split = kCommand.find(' ');
    std::vector<PromptWrite> script;
    script.push_back({on(beat(post)), kBootBiosLine, 0});
    script.push_back({on(beat(post)), "\n\n", 0});
    script.push_back({on(prompt), kBootPrompt, 0});
    // EVOLUTION.EXE: one key a beat, the first 16 beats before the last.
    const std::string_view first = kCommand.substr(0, split), second = kCommand.substr(split);
    const int start = last - 16;
    for (std::size_t j = 0; j < first.size(); ++j)
        script.push_back({on(beat(start + static_cast<int>(j))), first.substr(j, 1), 0});
    // " /2026": a key every half beat, the last on the last beat.
    for (std::size_t j = 0; j < second.size(); ++j)
        script.push_back(
            {on(beat(last - 0.5 * static_cast<double>(second.size() - 1 - j))), second.substr(j, 1), 0});
    return script;
}

} // namespace

std::uint32_t returnFlashFrame(const show::TimelineFrame& frame) {
    return frameOf(frame.accentSeconds);
}

std::uint32_t returnLastKeyFrame(const show::TimelineFrame& frame) {
    return frameOf(chapterBeat(frame, lastBeat(frame)));
}

std::vector<std::string> returnScreen(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::string written = promptWritten(returnScript(frame), retroFrame);
    if (written.empty())
        return {};
    std::vector<std::string> screen = promptRows(written);
    while (screen.size() > 1 && screen.back().empty())
        screen.pop_back();
    return screen;
}

void returnPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    bootPalette(palette, frame, retroFrame);
    // The switch-on flash: from white to black through the DAC's 64 levels over one beat.
    const std::uint32_t flash = returnFlashFrame(frame);
    double level = 0;
    if (retroFrame >= flash) {
        const double at = chapterBeat(frame, flashEndBeat(frame));
        const double u =
            std::clamp((retroFrame - flash) / ((at - frame.accentSeconds) * kBootRetroRate), 0.0, 1.0);
        level = (1 - u) * (1 - u);
    }
    const int dac = static_cast<int>(std::lround(63 * level));
    const auto byte = static_cast<std::uint8_t>((dac * 255 + 31) / 63);
    palette[kReturnFlash] = rgba(byte, byte, byte);
}

std::uint8_t returnBackground(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    return retroFrame >= returnFlashFrame(frame) ? kReturnFlash : kBootBlack;
}

std::vector<text::Item> returnText(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::string written = promptWritten(returnScript(frame), retroFrame);
    if (written.empty())
        return {};
    return promptItems(promptRows(written), beatCursorOn(frame, retroFrame, kBootRetroRate));
}

} // namespace evo::retro
