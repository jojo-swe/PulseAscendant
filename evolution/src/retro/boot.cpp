#include "retro/boot.hpp"

#include "retro/dots.hpp"
#include "show/cues.hpp"
#include "show/era.hpp"
#include "show/music_curves.hpp"
#include "text/font8x8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace evo::retro {

namespace {

constexpr std::uint32_t kFlashFrames = 2;
// The flash and the black after it: the last retro frames of the chapter.
constexpr double kCutFrames = 4;
// Chapter starts lie on tracked beats; converting them to track seconds rounds by far less than this.
constexpr double kEdge = 1e-6;

// The first retro frame at or after `seconds`. A script time need not fall on a 35 Hz frame (1.5 s is frame
// 52.5, so it starts on 53); the epsilon keeps one that does, like 1.6 * 35 = 56.000000000000007, on that
// frame rather than the next.
std::uint32_t frameAt(double seconds) {
    return static_cast<std::uint32_t>(std::ceil(std::max(seconds, 0.0) * kBootRetroRate - 1e-6));
}

// A detection line split for typing: the label shows at once, the dot leader types, the result shows at
// once later. The leader is the first run of " ." to the last dot.
struct Detection {
    std::string_view label, leader, result;
};
Detection split(std::string_view line) {
    const std::size_t leader = line.find(" .");
    const std::size_t result = line.find_last_of('.') + 1;
    return {line.substr(0, leader), line.substr(leader, result - leader), line.substr(result)};
}

// The chapter's start in its track's seconds, and the first beat of the track at or after it.
struct ChapterGrid {
    show::BeatGridView grid;
    double start; // track seconds
    int first;    // beat index
};
ChapterGrid chapterGrid(const show::TimelineFrame& frame) {
    const double showStart = show::chapterStart(frame.chapter);
    const int track = show::trackAt(showStart + kEdge);
    const show::BeatGridView grid = show::beatGrid(track);
    const double start = showStart - show::trackStart(track);
    int first = grid.beatAt(start + kEdge);
    if (first < 0)
        first = 0;
    else if (grid.beatTime(first) < start - kEdge)
        ++first;
    return {grid, start, first};
}

// Typed one key at a time: key j of `keys` at from + j * (to - from) / (n - 1), each at the start of the
// retro frame it falls in.
void addKeys(std::vector<PromptWrite>& script, std::string_view keys, double from, double to) {
    const std::size_t n = keys.size();
    for (std::size_t j = 0; j < n; ++j) {
        const double at =
            n > 1 ? from + (to - from) * static_cast<double>(j) / static_cast<double>(n - 1) : to;
        script.push_back({frameStart(at, kBootRetroRate), keys.substr(j, 1), 0});
    }
}

// A detection line: the label on `labelAt`, the leader typed from there, the result on `resultAt`.
void addDetection(std::vector<PromptWrite>& script, std::string_view line, double labelAt, double resultAt) {
    const Detection d = split(line);
    const double at = frameStart(labelAt, kBootRetroRate);
    script.push_back({at, d.label, 0});
    script.push_back({at, d.leader, kBootDetectRate});
    script.push_back({frameStart(resultAt, kBootRetroRate), d.result, 0});
}

// The UTF-8 prefix of the first `count` code points.
std::string_view codePoints(std::string_view utf8, int count) {
    std::size_t i = 0;
    for (int seen = 0; i < utf8.size(); ++i) {
        if ((static_cast<unsigned char>(utf8[i]) & 0xC0u) != 0x80u) {
            if (seen == count)
                break;
            ++seen;
        }
    }
    return utf8.substr(0, i);
}

int codePointCount(std::string_view utf8) {
    return static_cast<int>(std::count_if(
        utf8.begin(), utf8.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0u) != 0x80u; }));
}

std::string_view thenCard(const show::TimelineFrame& frame) {
    return show::cues[static_cast<std::size_t>(frame.chapter)].thenCard;
}

} // namespace

CardSpan cardSpan(const show::TimelineFrame& frame, std::uint32_t rate) {
    const show::TransitionMarks marks = show::transitionMarks(frame);
    const double accent = marks.accent - marks.start;
    const double out = std::min(accent + marks.bar, frame.chapterDuration - 2 * marks.bar);
    const auto at = [rate](double seconds) {
        return static_cast<std::uint32_t>(std::ceil(std::max(seconds, 0.0) * rate - 1e-6));
    };
    return {at(marks.bar), at(out)};
}

double chapterBeat(const show::TimelineFrame& frame, int beat) {
    const ChapterGrid g = chapterGrid(frame);
    return g.grid.beatTime(g.first + beat) - g.start;
}

double chapterDownbeat(const show::TimelineFrame& frame, int bar) {
    const ChapterGrid g = chapterGrid(frame);
    int seen = 0;
    for (int beat = g.first; beat < g.grid.count(); ++beat)
        if (g.grid.bar(beat) == 0 && seen++ == bar)
            return g.grid.beatTime(beat) - g.start;
    return g.grid.beatTime(g.grid.count() - 1) - g.start;
}

double frameStart(double seconds, std::uint32_t rate) {
    return std::floor(std::max(seconds, 0.0) * rate + 1e-9) / rate;
}

bool beatCursorOn(const show::TimelineFrame& frame, std::uint32_t retroFrame, std::uint32_t rate) {
    const ChapterGrid g = chapterGrid(frame);
    const double t = g.start + (retroFrame + 0.5) / rate;
    const int beat = g.grid.beatAt(t);
    if (beat < g.first || g.grid.count() < 2)
        return false;
    const int next = std::min(beat + 1, g.grid.count() - 1);
    const double length = next > beat ? g.grid.beatTime(next) - g.grid.beatTime(beat)
                                      : g.grid.beatTime(beat) - g.grid.beatTime(beat - 1);
    return t - g.grid.beatTime(beat) < 0.5 * length;
}

std::string promptWritten(std::span<const PromptWrite> script, std::uint32_t retroFrame) {
    std::string text;
    for (const PromptWrite& write : script) {
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

bool promptCursorOn(std::uint32_t retroFrame) {
    // 2 Hz: on for the first quarter second of every half second (0.0-0.25 s, 0.5-0.75 s, ...), judged at the
    // centre of the retro frame, (retroFrame + 0.5) / rate, so the frame that holds t = 0.5 s (17, from
    // 0.486 s) shows it. floor(centre * 4) in integers: (2 * retroFrame + 1) * 2 / rate.
    return (2 * retroFrame + 1) * 2 / kBootRetroRate % 2 == 0;
}

std::vector<std::string> promptRows(std::string_view text) {
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

std::vector<text::Item> promptItems(const std::vector<std::string>& screen, bool cursor, std::uint8_t index) {
    const std::uint32_t colour = 0xFF000000u | index; // drawIndex writes the low byte
    const auto x = [](std::size_t column) { return static_cast<float>(kBootMargin + 8 * column); };
    const auto y = [](std::size_t row) { return static_cast<float>(kBootMargin + kBootLineHeight * row); };
    std::vector<text::Item> items;
    for (std::size_t row = 0; row < screen.size(); ++row)
        if (!screen[row].empty())
            items.push_back({x(0), y(row), 1, colour, screen[row]});
    if (cursor && !screen.empty())
        items.push_back({x(screen.back().size()), y(screen.size() - 1), 1, colour, "_"});
    return items;
}

// --- BOOT ------------------------------------------------------------------------------------------------

std::vector<PromptWrite> bootScript(const show::TimelineFrame& frame) {
    const auto beat = [&frame](int i) { return chapterBeat(frame, i); };
    const auto on = [](double seconds) { return frameStart(seconds, kBootRetroRate); };
    std::vector<PromptWrite> script;
    script.push_back({on(beat(0)), kBootBiosLine, 0});
    script.push_back({on(beat(0)), "\n\n", 0});
    script.push_back({on(beat(2)), kBootPrompt, 0});
    addKeys(script, kBootCommand, beat(4), beat(6));
    script.push_back({on(frame.accentSeconds), "\n", 0}); // Enter, on the accent
    addDetection(script, kBootSoundCard, beat(7), beat(8));
    script.push_back({on(beat(8)), "\n", 0});
    addDetection(script, kBootRayTracing, beat(9), beat(11));
    script.push_back({on(beat(11)), "\n", 0});
    addDetection(script, kBootApplause, beat(12), beat(14));
    script.push_back({on(beat(14)), "\n", 0});
    script.push_back({on(beat(15)), "LOADING", 0});
    script.push_back({on(beat(15)), "...", kBootDetectRate});
    return script;
}

// The flash is the cut out of the prompt into FLYBY, so it belongs at the end of the chapter: its last four
// retro frames are two white ones and black. BOOT does not flash on its accent as such: the accent (about
// 3 s) is the Enter, in the middle of the script. The accent still wins if it ever comes later than that.
std::uint32_t bootFlashFrame(const show::TimelineFrame& frame) {
    const double at = std::max(frame.accentSeconds, frame.chapterDuration - kCutFrames / kBootRetroRate);
    return static_cast<std::uint32_t>(std::floor(std::max(at, 0.0) * kBootRetroRate));
}

std::vector<std::string> bootScreen(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    if (retroFrame >= bootFlashFrame(frame))
        return {};
    const std::vector<PromptWrite> script = bootScript(frame);
    std::vector<std::string> screen = promptRows(promptWritten(script, retroFrame));
    while (screen.size() > 1 && screen.back().empty())
        screen.pop_back(); // the cursor's empty rows
    if (screen.size() == 1 && screen.front().empty())
        return {};
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
    const std::vector<PromptWrite> script = bootScript(frame);
    const std::string written = promptWritten(script, retroFrame);
    if (written.empty())
        return {}; // black until the BIOS line
    return promptItems(promptRows(written), beatCursorOn(frame, retroFrame, kBootRetroRate));
}

// --- The 1993 card in the picture ------------------------------------------------------------------------

int cardShown(const show::TimelineFrame& frame, std::uint32_t retroFrame, std::uint32_t rate) {
    const std::string_view card = thenCard(frame);
    if (card.empty())
        return -1;
    const CardSpan span = cardSpan(frame, rate);
    if (retroFrame < span.in || retroFrame >= span.out)
        return -1;
    const auto typed = static_cast<int>((retroFrame - span.in) * kCardRate / rate);
    return std::min(typed, codePointCount(card));
}

std::vector<text::Item> cardText(const show::TimelineFrame& frame, std::uint32_t retroFrame,
                                 std::uint32_t rate, std::uint8_t index) {
    const int shown = cardShown(frame, retroFrame, rate);
    if (shown <= 0)
        return {};
    return {{kCardX, kCardY, 1, 0xFF000000u | index, std::string(codePoints(thenCard(frame), shown))}};
}

PixelRect cardRect(const show::TimelineFrame& frame, std::uint32_t retroFrame, std::uint32_t rate) {
    const int shown = cardShown(frame, retroFrame, rate);
    if (shown < 0)
        return {kCardX, kCardY, 0, 8};
    const auto glyphs = static_cast<int>(text::glyphCodes(codePoints(thenCard(frame), shown)).size());
    return {kCardX, kCardY, 8 * (glyphs + 1), 8};
}

void cardCursor(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame,
                std::uint32_t rate, std::uint8_t index) {
    if (cardShown(frame, retroFrame, rate) < 0 || !beatCursorOn(frame, retroFrame, rate))
        return;
    const PixelRect rect = cardRect(frame, retroFrame, rate);
    const int x = rect.x + rect.width - 8;
    addRect(scene, x, rect.y, x + 7, rect.y + 8, index); // the cell less its gap column, as the glyphs
}

void addRect(RetroScene& scene, int x0, int y0, int x1, int y1, std::uint8_t index) {
    if (x1 <= x0 || y1 <= y0)
        return;
    const int corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (const auto& [a, b, c] : {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}}) {
        RetroTriangle t{};
        const int k[3] = {a, b, c};
        for (int v = 0; v < 3; ++v) {
            t.x[v] = corners[k[v]][0] * kSubpixel;
            t.y[v] = corners[k[v]][1] * kSubpixel;
            t.index[v] = index;
        }
        t.mode = kFlat;
        scene.triangles.push_back(t);
        scene.triangleDepth.push_back(0);
    }
}

// --- REBOOT ----------------------------------------------------------------------------------------------

namespace {

// RETURN's last screen, which REBOOT opens on, and the Enter that ends it.
const std::string& rebootOpening() {
    static const std::string opening =
        std::string(kBootBiosLine) + "\n\n" + kBootPrompt + kRebootCommand + "\n";
    return opening;
}

std::vector<PromptWrite> rebootScript(const show::TimelineFrame& frame) {
    const auto beat = [&frame](int i) { return chapterBeat(frame, i); };
    const auto on = [](double seconds) { return frameStart(seconds, kBootRetroRate); };
    static const std::string found =
        std::string(split(kBootRayTracing).label) + std::string(split(kBootRayTracing).leader) + " found";
    std::vector<PromptWrite> script;
    script.push_back({0, rebootOpening(), 0});
    script.push_back({on(beat(0)), "Reinitialising reality", 0});
    script.push_back({on(beat(0)), " ...", kBootDetectRate});
    script.push_back({on(beat(2)), "\n", 0});
    addDetection(script, found, beat(4), beat(6));
    script.push_back({on(beat(6)), "\n", 0});
    script.push_back({on(beat(8)), "LOADING", 0});
    script.push_back({on(beat(8)), "...", kBootDetectRate});
    return script;
}

} // namespace

std::uint32_t rebootCubeFrame(const show::TimelineFrame& frame) {
    return frameAt(frameStart(chapterBeat(frame, 14), kBootRetroRate));
}

std::vector<std::string> rebootScreen(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    if (retroFrame >= rebootCubeFrame(frame))
        return {};
    std::vector<std::string> screen = promptRows(promptWritten(rebootScript(frame), retroFrame));
    while (screen.size() > 1 && screen.back().empty())
        screen.pop_back(); // the cursor's empty rows
    return screen;
}

void rebootPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    dotsPalette(palette, frame, retroFrame);
    Palette dos{};
    bootPalette(dos, frame, retroFrame);
    std::copy(dos.begin(), dos.begin() + 16, palette.begin());
}

void rebootScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::uint32_t cube = rebootCubeFrame(frame);
    if (retroFrame >= cube)
        dotsSceneFrom(scene, dotsShapePoints(DotsShape::Cube, retroFrame - cube));
    cardCursor(scene, frame, retroFrame, kBootRetroRate, kRebootCard);
}

std::vector<text::Item> rebootText(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    std::vector<text::Item> items;
    if (retroFrame < rebootCubeFrame(frame))
        items = promptItems(promptRows(promptWritten(rebootScript(frame), retroFrame)),
                            beatCursorOn(frame, retroFrame, kBootRetroRate));
    const std::vector<text::Item> card = cardText(frame, retroFrame, kBootRetroRate, kRebootCard);
    items.insert(items.end(), card.begin(), card.end());
    return items;
}

} // namespace evo::retro
