#include "retro/palette.hpp"

namespace evo::retro {

void grayPalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    for (std::uint32_t i = 0; i < palette.size(); ++i) {
        const auto level = static_cast<std::uint8_t>(i);
        palette[i] = rgba(level, level, level);
    }
}

} // namespace evo::retro
