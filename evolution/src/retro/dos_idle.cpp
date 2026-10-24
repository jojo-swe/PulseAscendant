#include "retro/dos_idle.hpp"

#include <array>

namespace evo::retro {

namespace {

// DIR's output in MS-DOS 6's layout (names padded to 8, extensions to 3, sizes right-aligned in 10 columns),
// every row inside kBootColumns. The sizes add up.
constexpr std::array<const char*, 14> kScreen = {
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
    "        5 file(s)    912,512 bytes",
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

} // namespace evo::retro
