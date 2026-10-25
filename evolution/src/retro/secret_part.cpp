#include "retro/secret_part.hpp"

#include "modern/scenes/glass.hpp"
#include "modern/scenes/particles.hpp"
#include "modern/scenes/ship_asset.hpp"
#include "retro/boot.hpp"
#include "retro/glenz.hpp"
#include "retro/palette.hpp"
#include "show/music_curves.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>

namespace evo::retro {

namespace {

constexpr double kRate = kSecretRetroRate;

// --- Timing ---------------------------------------------------------------------------------------------

// The landmarks in beats of track 0, counted from screen 2's start, which is the fifth downbeat. Screen 2
// lists its heading on that downbeat and then a line every second beat for the numbers, every fourth for
// the closing lines; screen 3 comes two bars after the last of them, the puzzle two bars later.
constexpr int kScreen2Downbeat = 4;
constexpr std::array<int, 8> kListBeats = {0, 2, 4, 6, 8, 10, 14, 18};
constexpr int kScreen3Beats = 26;
constexpr int kPuzzleBeats = 34;

constexpr double kTypingRate = 35; // characters a second: one a retro frame
constexpr double kBoxGrowSeconds = 0.6;
constexpr double kBodyPause = 0.3;    // between the title and the first line of screen 1
constexpr int kClearFramesPerRow = 2; // screen 2 clears screen 1 a row at a time
constexpr double kRewardHoldSeconds = 3.0;

// The first retro frame at or after `seconds` (boot.cpp's rule).
std::uint32_t frameAt(double seconds) {
    return static_cast<std::uint32_t>(std::ceil(std::max(seconds, 0.0) * kRate - 1e-6));
}

std::uint32_t frameOf(double seconds) {
    return static_cast<std::uint32_t>(std::floor(std::max(seconds, 0.0) * kRate));
}

double trackBeat(int beat) {
    return show::beatGrid(kSecretTrack).beatTime(beat);
}

// UTF-8 code points, so a typed "·" never shows half a character.
std::size_t codePoints(std::string_view text) {
    std::size_t count = 0;
    for (const char c : text)
        count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return count;
}

std::string firstCodePoints(std::string_view text, std::size_t count) {
    std::size_t end = 0, seen = 0;
    while (end < text.size()) {
        if ((static_cast<unsigned char>(text[end]) & 0xC0) != 0x80) {
            if (seen == count)
                break;
            ++seen;
        }
        ++end;
    }
    return std::string(text.substr(0, end));
}

// Characters typed by retro frame `frame` of a line that starts typing at frame `start`; the first appears
// one character's time after the start, as at the DOS prompt.
std::size_t typedCount(std::uint32_t frame, std::uint32_t start, double rate) {
    if (frame < start)
        return 0;
    return static_cast<std::size_t>(std::floor(static_cast<double>(frame - start) * rate / kRate + 1e-9));
}

// --- Text ------------------------------------------------------------------------------------------------

constexpr std::uint8_t kBlack = 0;
constexpr std::uint8_t kGrey = 7;      // the DOS text
constexpr std::uint8_t kDim = 8;       // the patient's row
constexpr std::uint8_t kPowerOff = 14; // the collapsing beam
constexpr std::uint8_t kPhosphor = 15; // the part's own white

constexpr int kColumns = 39; // BOOT's: 8-pixel cells right of its 4-pixel margin

// "label ....... value", filled with dots to kColumns.
std::string leader(std::string_view label, std::string_view value) {
    const int dots = kColumns - static_cast<int>(label.size() + value.size()) - 2;
    return std::string(label) + " " + std::string(static_cast<std::size_t>(std::max(dots, 3)), '.') + " " +
           std::string(value);
}

const std::string& deviceLine(int which) {
    static const std::array<std::string, 4> kLines = {
        leader("Sound Blaster 16 at 220h", "found"),
        leader("Ray tracing hardware", "found"),
        leader("Applause device", "press L"),
        leader("Applause device", "found. thank you."),
    };
    return kLines[static_cast<std::size_t>(which)];
}

constexpr std::string_view kTitle = "EVOLUTION \xC2\xB7 SECRET PART";
constexpr std::array<std::string_view, 5> kWelcome = {
    "CONGRATULATIONS. ROUGHLY 0.3% OF", "VIEWERS FIND THIS.",     "",
    "WE ARE NOT SMARTER THAN YOU.",     "WE JUST HAD MORE TIME.",
};
constexpr std::string_view kTonight = "TONIGHT, FOR YOU:";
constexpr std::array<std::string_view, 3> kClosing = {
    "ALL OF IT TO SAY HELLO.",
    "A LIKE WOULD BE NICE.",
    "A SMILE IS ENOUGH.",
};
constexpr std::string_view kSawThat = "WE SAW THAT.";
constexpr std::string_view kPatient = "FOR THE PATIENT:";
constexpr std::array<std::string_view, 3> kReward = {
    "YOU SOLVED IT.",
    "THAT TOOK LONGER THAN THE DEMO.",
    "RESPECT. NOW GO MAKE SOMETHING.",
};

// Screen 1: the box, then the title centred in it, then the welcome.
constexpr int kBoxLeft = 16, kBoxRight = 304, kBoxTop = 36, kBoxBottom = 68;
constexpr int kBoxGap = 3; // between the two lines of the double box
constexpr int kTitleY = 48;
constexpr int kWelcomeX = 24, kWelcomeY = 88, kWelcomeStep = 10;
// Screen 2: the list on the left, the glenz in the right third.
constexpr int kListX = 16;
constexpr std::array<int, 8> kListY = {40, 56, 66, 76, 86, 106, 116, 126};
// Screen 3: BOOT's margin and line height.
constexpr int kBootX = 4, kBootY = 4, kBootStep = 8;
constexpr int kSawThatY = kBootY + 4 * kBootStep;
constexpr int kPatientY = 172;
constexpr int kRewardY = 164, kRewardStep = 10;

// One line as drawn: where, in which index, and the text typed so far.
struct Line {
    int x, y;
    std::uint8_t index;
    std::string text;
};

std::string numberLine(std::uint64_t value, std::string_view label) {
    std::string number = groupDigits(value);
    if (number.size() < 10)
        number.insert(0, 10 - number.size(), ' ');
    return number + " " + std::string(label);
}

std::array<std::string, 8> listLines() {
    const SecretStats stats = secretStats();
    return {std::string(kTonight),
            numberLine(stats.photons, "PHOTONS"),
            numberLine(stats.stars, "STARS"),
            numberLine(stats.particles, "PARTICLES"),
            numberLine(stats.frames, "FRAMES"),
            std::string(kClosing[0]),
            std::string(kClosing[1]),
            std::string(kClosing[2])};
}

std::string patientRow() {
    std::string row;
    for (const std::uint32_t n : patientNumbers())
        row += (row.empty() ? "" : " ") + std::to_string(n);
    return row;
}

// The screen-3 frame when the device line is typed out, for typing and for the applause rule.
std::uint32_t lastDeviceFrame(double screen3) {
    const std::uint32_t start = frameAt(screen3);
    std::size_t characters = 0;
    for (int line = 0; line < 3; ++line)
        characters += deviceLine(line).size() + 1; // the newline takes a frame too
    return start + static_cast<std::uint32_t>(characters - 1);
}

std::size_t rewardCharacters() {
    std::size_t characters = 0;
    for (const std::string_view line : kReward)
        characters += line.size() + 1;
    return characters;
}

// Text typed one line after another from `start`, a frame per newline; returns the rows typed so far.
std::vector<std::string> typeLines(std::span<const std::string> lines, std::uint32_t frame,
                                   std::uint32_t start, double rate) {
    std::size_t budget = typedCount(frame, start, rate);
    std::vector<std::string> rows;
    for (const std::string& line : lines) {
        const std::size_t length = codePoints(line);
        rows.push_back(firstCodePoints(line, std::min(budget, length)));
        if (budget <= length)
            break;
        budget -= length + 1;
    }
    return rows;
}

double powerOffStart(const SecretInput& settled) {
    return secretDuration(settled) - kSecretPowerOffSeconds;
}

// Every line on the screen at retro frame `frame`, and where the cursor is (if anywhere).
struct Screen {
    std::vector<Line> lines;
    bool cursor = false;
    int cursorX = 0, cursorY = 0;
};

Screen screenAt(std::uint32_t frame, const SecretInput& input) {
    const SecretMarks& marks = secretMarks();
    Screen screen;
    if (frame >= frameOf(powerOffStart(input)))
        return screen;

    const std::uint32_t screen2 = frameAt(marks.screen2), screen3 = frameAt(marks.screen3);
    if (frame < screen3) {
        // Screen 1, cleared a row at a time from screen 2's start: the box with its title is row 0.
        const auto shown = [&](int row) {
            return frame < screen2 || frame - screen2 < static_cast<std::uint32_t>(kClearFramesPerRow * row);
        };
        const std::uint32_t titleStart = frameAt(kBoxGrowSeconds);
        const std::size_t title = std::min(typedCount(frame, titleStart, kTypingRate), codePoints(kTitle));
        if (shown(0) && title > 0) {
            const int x = (320 - 8 * static_cast<int>(codePoints(kTitle))) / 2;
            screen.lines.push_back({x, kTitleY, kPhosphor, firstCodePoints(kTitle, title)});
        }
        const std::uint32_t bodyStart =
            frameAt(kBoxGrowSeconds + codePoints(kTitle) / kTypingRate + kBodyPause);
        std::vector<std::string> welcome(kWelcome.begin(), kWelcome.end());
        const std::vector<std::string> rows = typeLines(welcome, frame, bodyStart, kTypingRate);
        int row = 1;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (welcome[i].empty())
                continue; // the blank line between the paragraphs
            if (shown(row) && !rows[i].empty())
                screen.lines.push_back(
                    {kWelcomeX, kWelcomeY + kWelcomeStep * static_cast<int>(i), kPhosphor, rows[i]});
            ++row;
        }
        if (frame < screen2) {
            screen.cursor = true;
            if (rows.empty() || (rows.size() == 1 && rows[0].empty() && frame < bodyStart)) {
                const int x = (320 - 8 * static_cast<int>(codePoints(kTitle))) / 2;
                screen.cursorX = x + 8 * static_cast<int>(title);
                screen.cursorY = kTitleY;
            } else {
                const std::size_t i = rows.size() - 1;
                screen.cursorX = kWelcomeX + 8 * static_cast<int>(codePoints(rows[i]));
                screen.cursorY = kWelcomeY + kWelcomeStep * static_cast<int>(i);
            }
        }

        // Screen 2's list: the heading and closing lines typed, the numbers at once on their beat.
        if (frame >= screen2) {
            const std::array<std::string, 8> list = listLines();
            for (std::size_t i = 0; i < list.size(); ++i) {
                const std::uint32_t start = frameAt(marks.list[i]);
                if (frame < start)
                    break;
                const bool number = i >= 1 && i <= 4;
                const std::size_t count = number ? list[i].size() : typedCount(frame, start, kTypingRate);
                const std::string text = firstCodePoints(list[i], count);
                if (!text.empty())
                    screen.lines.push_back({kListX, kListY[i], kPhosphor, text});
            }
        }
        return screen;
    }

    // Screen 3: the devices, typed as BOOT types; the last waits for L.
    const std::uint32_t applause = input.applauseAt >= 0 ? frameOf(input.applauseAt) : 0;
    const bool clapped = input.applauseAt >= 0 && frame >= applause;
    std::vector<std::string> devices = {deviceLine(0), deviceLine(1), deviceLine(2)};
    std::vector<std::string> rows = typeLines(devices, frame, screen3, kTypingRate);
    if (clapped) {
        rows.resize(3);
        rows[2] = firstCodePoints(deviceLine(3), typedCount(frame, applause, kTypingRate));
    }
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (!rows[i].empty())
            screen.lines.push_back({kBootX, kBootY + kBootStep * static_cast<int>(i), kGrey, rows[i]});
    screen.cursor = true;
    screen.cursorX = kBootX + 8 * static_cast<int>(rows.empty() ? 0 : rows.back().size());
    screen.cursorY = kBootY + kBootStep * static_cast<int>(rows.empty() ? 0 : rows.size() - 1);
    if (clapped) {
        const std::uint32_t sawStart = applause + static_cast<std::uint32_t>(deviceLine(3).size()) + 1;
        const std::string saw = firstCodePoints(kSawThat, typedCount(frame, sawStart, kTypingRate));
        if (!saw.empty()) {
            screen.lines.push_back({kBootX, kSawThatY, kPhosphor, saw});
            screen.cursorX = kBootX + 8 * static_cast<int>(saw.size());
            screen.cursorY = kSawThatY;
        }
    }

    // The patient's row, or what it turns into once answered.
    const std::uint32_t puzzle = frameAt(marks.puzzle);
    const bool solved = input.solvedAt >= 0 && frame >= frameOf(input.solvedAt);
    if (solved) {
        std::vector<std::string> reward(kReward.begin(), kReward.end());
        const std::vector<std::string> typed = typeLines(reward, frame, frameOf(input.solvedAt), kTypingRate);
        for (std::size_t i = 0; i < typed.size(); ++i)
            if (!typed[i].empty())
                screen.lines.push_back(
                    {kBootX, kRewardY + kRewardStep * static_cast<int>(i), kPhosphor, typed[i]});
    } else if (frame >= puzzle) {
        screen.lines.push_back({kBootX, kPatientY, kDim, std::string(kPatient)});
        screen.lines.push_back({kBootX, kPatientY + 10, kDim, patientRow()});
    }
    return screen;
}

// --- The glenz -------------------------------------------------------------------------------------------

constexpr double kGlenzSize = 0.75;    // the star's scale
constexpr double kGlenzX = 3.93;       // world x: the centre of the right third at kGlenzDistance
constexpr double kGlenzFloorY = -1.62; // its resting height (screen row ~135)
constexpr double kGlenzLiftY = 3.0;    // a normal bounce's height (~65 rows)
constexpr double kApplauseLift = 1.8;  // the bounce after L, in normal bounces
constexpr double kGlenzDistance = 8.0;
constexpr double kGlenzFovY = 60.0;

Camera secretCamera() {
    return Camera{{0, 0, -kGlenzDistance}, {0, 0, 0}, {0, 1, 0}, kGlenzFovY};
}

std::array<double, 3> turned(const GlenzPoint& p, const double angle[3]) {
    double x = p[0], y = p[1], z = p[2];
    const double y1 = y * std::cos(angle[0]) - z * std::sin(angle[0]);
    const double z1 = y * std::sin(angle[0]) + z * std::cos(angle[0]);
    const double x2 = x * std::cos(angle[1]) + z1 * std::sin(angle[1]);
    const double z2 = -x * std::sin(angle[1]) + z1 * std::cos(angle[1]);
    const double x3 = x2 * std::cos(angle[2]) - y1 * std::sin(angle[2]);
    const double y3 = x2 * std::sin(angle[2]) + y1 * std::cos(angle[2]);
    return {x3, y3, z2};
}

void addGlenz(RetroScene& scene, double t, double lift) {
    const double angle[3] = {kGlenzStartAngle[0] + kGlenzSpin[0] * t, kGlenzStartAngle[1] + kGlenzSpin[1] * t,
                             kGlenzStartAngle[2] + kGlenzSpin[2] * t};
    const GlenzMesh& mesh = glenzMesh();
    std::array<std::array<double, 3>, kGlenzVertices> world{};
    for (std::size_t i = 0; i < kGlenzVertices; ++i) {
        const GlenzPoint scaled = {mesh.vertices[i][0] * kGlenzSize, mesh.vertices[i][1] * kGlenzSize,
                                   mesh.vertices[i][2] * kGlenzSize};
        const auto p = turned(scaled, angle);
        world[i] = {p[0] + kGlenzX, p[1] + kGlenzFloorY + kGlenzLiftY * lift, p[2]};
    }
    const Camera camera = secretCamera();
    // GLENZ's rule: back faces, then front faces, each OR-ing its code (glenz.cpp).
    for (const bool frontPass : {false, true})
        for (std::size_t f = 0; f < kGlenzTriangles; ++f) {
            const auto& tri = mesh.triangles[f];
            const auto& a = world[static_cast<std::size_t>(tri[0])];
            const auto& b = world[static_cast<std::size_t>(tri[1])];
            const auto& c = world[static_cast<std::size_t>(tri[2])];
            const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const double v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                 u[0] * v[1] - u[1] * v[0]};
            const double toFace[3] = {a[0] - camera.eye[0], a[1] - camera.eye[1], a[2] - camera.eye[2]};
            const bool front = n[0] * toFace[0] + n[1] * toFace[1] + n[2] * toFace[2] > 0;
            if (front != frontPass)
                continue;
            const bool odd = f % 2 == 1;
            const std::uint8_t code = front
                                          ? static_cast<std::uint8_t>(kGlenzFront | (odd ? kGlenzFrontB : 0))
                                          : static_cast<std::uint8_t>(kGlenzBack | (odd ? kGlenzBackB : 0));
            const double corners[3][3] = {{a[0], a[1], a[2]}, {b[0], b[1], b[2]}, {c[0], c[1], c[2]}};
            const std::uint8_t index[3] = {code, code, code};
            addTriangle(scene, camera, corners, index, kGlenz);
        }
}

// Screen-space rectangles come from boot.hpp's addRect.

// --- The effect's providers -------------------------------------------------------------------------------

void secretPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    // The glass on black: GLENZ's face colours (glenz.cpp), the back faces seen through the front ones, lit
    // brighter than over the copper so the star reads on the dark screen.
    constexpr std::array<double, 3> kFront[2] = {{70, 140, 255}, {215, 235, 255}};
    constexpr std::array<double, 3> kBack[2] = {{15, 25, 125}, {110, 35, 190}};
    palette.fill(rgba(0, 0, 0));
    for (int index = kGlenzFront; index < 256; ++index) {
        std::array<double, 3> c = {0, 0, 0};
        if (index & (kGlenzBack | kGlenzBackB))
            c = kBack[(index & kGlenzBackB) ? 1 : 0];
        if (index & (kGlenzFront | kGlenzFrontB)) {
            const std::array<double, 3>& f = kFront[(index & kGlenzFrontB) ? 1 : 0];
            for (std::size_t k = 0; k < 3; ++k)
                c[k] = 0.35 * c[k] + 0.75 * f[k];
        }
        const auto channel = [&](std::size_t k) {
            return static_cast<std::uint8_t>(std::clamp(std::lround(c[k]), 0L, 255L));
        };
        palette[static_cast<std::size_t>(index)] = rgba(channel(0), channel(1), channel(2));
    }
    palette[kGrey] = rgba(0xAA, 0xAA, 0xAA);
    palette[kDim] = rgba(0x55, 0x55, 0x55);
    palette[kPhosphor] = rgba(0xEB, 0xF0, 0xFF);
    // The beam fades on the last stretch of the power-off.
    const SecretInput input = secretInputOf(frame);
    const double t = retroFrame / kRate, start = powerOffStart(input);
    const double fade = std::clamp((t - (start + 0.35)) / (kSecretPowerOffSeconds - 0.35), 0.0, 1.0);
    const auto level = static_cast<std::uint8_t>(std::lround(255 * (1 - fade)));
    palette[kPowerOff] = rgba(level, level, level);
}

std::uint8_t secretBackground(const show::TimelineFrame&, std::uint32_t) {
    return kBlack;
}

std::vector<text::Item> secretText(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const Screen screen = screenAt(retroFrame, secretInputOf(frame));
    std::vector<text::Item> items;
    for (const Line& line : screen.lines)
        items.push_back(
            {static_cast<float>(line.x), static_cast<float>(line.y), 1, 0xFF000000u | line.index, line.text});
    if (screen.cursor && promptCursorOn(retroFrame))
        items.push_back({static_cast<float>(screen.cursorX), static_cast<float>(screen.cursorY), 1,
                         0xFF000000u | kGrey, "_"});
    return items;
}

void secretScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const SecretInput input = secretInputOf(frame);
    const SecretMarks& marks = secretMarks();
    const double t = retroFrame / kRate;
    const double off = powerOffStart(input);
    if (t >= off) {
        // The CRT power-off: the picture collapses to a bright line, the line to a dot, the dot fades.
        const double u = t - off;
        if (u < 0.15) {
            const double s = 1 - u / 0.15;
            const int half = std::max(1, static_cast<int>(std::lround(100 * s * s)));
            addRect(scene, 0, 100 - half, kScreenWidth, 100 + half, kPowerOff);
        } else if (u < 0.35) {
            const double s = 1 - (u - 0.15) / 0.2;
            const int half = std::max(1, static_cast<int>(std::lround(160 * s * s)));
            addRect(scene, 160 - half, 99, 160 + half, 101, kPowerOff);
        } else {
            addRect(scene, 159, 99, 161, 101, kPowerOff);
        }
        return;
    }
    const std::uint32_t screen2 = frameAt(marks.screen2);
    if (retroFrame < screen2) {
        // The double box: its rules grow from the left, the sides close it.
        const double p = std::clamp(t / kBoxGrowSeconds, 0.0, 1.0);
        const int right = kBoxLeft + static_cast<int>(std::lround((kBoxRight - kBoxLeft) * p));
        for (const int inset : {0, kBoxGap}) {
            addRect(scene, kBoxLeft + inset, kBoxTop + inset, right - inset, kBoxTop + inset + 1, kPhosphor);
            addRect(scene, kBoxLeft + inset, kBoxBottom - inset - 1, right - inset, kBoxBottom - inset,
                    kPhosphor);
            if (p >= 1) {
                addRect(scene, kBoxLeft + inset, kBoxTop + inset, kBoxLeft + inset + 1, kBoxBottom - inset,
                        kPhosphor);
                addRect(scene, kBoxRight - inset - 1, kBoxTop + inset, kBoxRight - inset, kBoxBottom - inset,
                        kPhosphor);
            }
        }
        return;
    }
    addGlenz(scene, t, secretGlenzLift(t, input));
}

} // namespace

// --- Public ----------------------------------------------------------------------------------------------

const SecretMarks& secretMarks() {
    static const SecretMarks marks = [] {
        const show::BeatGridView grid = show::beatGrid(kSecretTrack);
        int downbeats = 0, first = 0;
        for (int beat = 0; beat < grid.count(); ++beat)
            if (grid.bar(beat) == 0 && downbeats++ == kScreen2Downbeat) {
                first = beat;
                break;
            }
        SecretMarks m;
        m.screen2 = trackBeat(first);
        for (std::size_t i = 0; i < kListBeats.size(); ++i)
            m.list[i] = trackBeat(first + kListBeats[i]);
        m.screen3 = trackBeat(first + kScreen3Beats);
        m.puzzle = trackBeat(first + kPuzzleBeats);
        m.lastDevice = lastDeviceFrame(m.screen3) / kRate;
        return m;
    }();
    return marks;
}

SecretInput settledInput(const SecretInput& input) {
    const SecretMarks& marks = secretMarks();
    SecretInput settled;
    if (input.applauseAt >= 0 && frameOf(input.applauseAt) >= lastDeviceFrame(marks.screen3))
        settled.applauseAt = frameOf(input.applauseAt) / kRate;
    if (input.solvedAt >= 0)
        settled.solvedAt = frameOf(std::max(input.solvedAt, marks.puzzle)) / kRate;
    return settled;
}

double secretDuration(const SecretInput& input) {
    const SecretInput settled = settledInput(input);
    if (settled.solvedAt < 0)
        return kSecretSeconds;
    const double readEnd = settled.solvedAt + rewardCharacters() / kTypingRate + kRewardHoldSeconds;
    return std::max(kSecretSeconds, readEnd + kSecretPowerOffSeconds);
}

SecretStats secretStats() {
    SecretStats stats;
    stats.photons = modern::glassCard(show::Effect::Cathedral).exportPhotons;
    stats.stars = modern::kShipStars;
    stats.particles = modern::kDotsParticles;
    stats.frames = static_cast<std::uint64_t>(std::ceil(60 * show::showDuration() - 1e-9));
    return stats;
}

std::string groupDigits(std::uint64_t value) {
    std::string digits = std::to_string(value);
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3)
        digits.insert(static_cast<std::size_t>(at), ",");
    return digits;
}

std::array<std::uint32_t, 7> patientNumbers() {
    // The tally, one entry per number, kept as the cards' own arithmetic leaves it.
    constexpr std::array<std::uint32_t, 7> kTally = {26, 3, 20, 1, 28, 26, 27};
    constexpr std::uint64_t kNow = 2026;
    const SecretStats stats = secretStats();
    const std::array<std::uint64_t, 4> cards = {stats.photons, stats.stars, stats.particles, stats.frames};
    std::array<std::uint32_t, 7> numbers{};
    for (std::size_t i = 0; i < numbers.size(); ++i)
        numbers[i] = static_cast<std::uint32_t>(cards[i % cards.size()] % kNow) + (kTally[i] ^ 0x15u);
    return numbers;
}

std::string sha256Hex(std::string_view bytes) {
    static constexpr std::array<std::uint32_t, 64> k = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string message(bytes);
    const std::uint64_t bits = static_cast<std::uint64_t>(bytes.size()) * 8;
    message.push_back(static_cast<char>(0x80));
    while (message.size() % 64 != 56)
        message.push_back('\0');
    for (int shift = 56; shift >= 0; shift -= 8)
        message.push_back(static_cast<char>((bits >> shift) & 0xff));
    const auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t block = 0; block < message.size(); block += 64) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i)
            for (std::size_t j = 0; j < 4; ++j)
                w[i] = (w[i] << 8) | static_cast<unsigned char>(message[block + 4 * i + j]);
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::array<std::uint32_t, 8> v = h;
        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const std::uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const std::uint32_t t2 = s0 + maj;
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (std::size_t i = 0; i < 8; ++i)
            h[i] += v[i];
    }
    std::string hex;
    char digits[9];
    for (const std::uint32_t word : h) {
        std::snprintf(digits, sizeof digits, "%08x", word);
        hex += digits;
    }
    return hex;
}

bool isPatientAnswer(std::string_view word) {
    static constexpr std::string_view kDigest =
        "3209972ac774161ef2e811a3b1dd34016e87c10a198062a2737212d1861fe0a1";
    std::string upper(word);
    for (char& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return !upper.empty() && sha256Hex(upper) == kDigest;
}

std::vector<std::string> secretScreenText(double t, const SecretInput& input) {
    const Screen screen = screenAt(frameOf(t), settledInput(input));
    std::vector<Line> lines = screen.lines;
    std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.y < b.y; });
    std::vector<std::string> texts;
    for (const Line& line : lines)
        texts.push_back(line.text);
    return texts;
}

int secretScreen(double t, const SecretInput& input) {
    const SecretInput settled = settledInput(input);
    const std::uint32_t frame = frameOf(t);
    if (frame >= frameOf(powerOffStart(settled)))
        return 0;
    if (frame < frameAt(secretMarks().screen2))
        return 1;
    return frame < frameAt(secretMarks().screen3) ? 2 : 3;
}

double secretGlenzLift(double t, const SecretInput& input) {
    const int screen = secretScreen(t, input);
    if (screen != 2 && screen != 3)
        return 0;
    const double quantised = frameOf(t) / kRate; // the glenz moves at the retro rate
    const std::span<const double> beats = show::beatGrid(kSecretTrack).times();
    double lift = glenzBounceAt(beats, quantised);
    const SecretInput settled = settledInput(input);
    if (settled.applauseAt >= 0) {
        // The bounce that starts on the first beat at or after the applause goes higher.
        const auto next = std::lower_bound(beats.begin(), beats.end(), settled.applauseAt);
        if (next != beats.end() && next + 1 != beats.end() && quantised >= *next && quantised < *(next + 1))
            lift *= kApplauseLift;
    }
    return lift;
}

show::TimelineFrame secretFrame(double t, const SecretInput& input) {
    const SecretInput settled = settledInput(input);
    const auto field = [](double seconds) {
        return seconds < 0 ? 0 : static_cast<int>(frameOf(seconds)) + 1;
    };
    show::TimelineFrame frame;
    frame.chapter = field(settled.applauseAt) | field(settled.solvedAt) << 12;
    frame.effect = show::Effect::Boot; // any: the secret part's registry has one entry
    frame.transition = show::Transition::Cut;
    frame.showSeconds = t;
    frame.localSeconds = t;
    frame.chapterDuration = secretDuration(settled);
    frame.track = kSecretTrack;
    frame.trackSeconds = t;
    frame.title = "";
    return frame;
}

SecretInput secretInputOf(const show::TimelineFrame& frame) {
    const int applause = frame.chapter & 0xfff, solved = (frame.chapter >> 12) & 0xfff;
    SecretInput input;
    input.applauseAt = applause > 0 ? (applause - 1) / kRate : -1;
    input.solvedAt = solved > 0 ? (solved - 1) / kRate : -1;
    return input;
}

const EffectInfo& secretEffect() {
    static constexpr EffectInfo kSecret{.shader = nullptr,
                                        .retroRate = kSecretRetroRate,
                                        .palette = secretPalette,
                                        .text = secretText,
                                        .background = secretBackground,
                                        .scene = secretScene};
    return kSecret;
}

KeyWord::KeyWord(std::string_view word, double windowSeconds) : word_(word), window_(windowSeconds) {}

bool KeyWord::press(char key, double seconds) {
    if (progress_ > 0 && (key != word_[progress_] || seconds - start_ > window_))
        progress_ = 0;
    if (key != word_[progress_])
        return false;
    if (progress_ == 0)
        start_ = seconds;
    if (++progress_ < word_.size())
        return false;
    progress_ = 0;
    return true;
}

bool AnswerKeys::press(char key) {
    constexpr std::size_t kLongest = 16;
    if (!std::isalpha(static_cast<unsigned char>(key))) {
        typed_.clear();
        return false;
    }
    typed_.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(key))));
    if (typed_.size() > kLongest)
        typed_.erase(0, typed_.size() - kLongest);
    for (std::size_t length = 1; length <= typed_.size(); ++length)
        if (isPatientAnswer(std::string_view(typed_).substr(typed_.size() - length))) {
            typed_.clear();
            return true;
        }
    return false;
}

} // namespace evo::retro
