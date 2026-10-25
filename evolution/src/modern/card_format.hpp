#pragma once

// How the epoch cards print the numbers they take from the configuration.

#include <cstdint>
#include <string>

namespace evo::modern {

// The separator between a card's items: " · " (U+00B7).
inline constexpr const char* kCardDot = " · ";

// A count with thousands separators: 2097152 -> "2,097,152", 49 -> "49".
inline std::string groupThousands(std::uint64_t n) {
    const std::string digits = std::to_string(n);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0)
            out += ',';
        out += digits[i];
    }
    return out;
}

// The frame rate the show is rendered at (the exports' and the window's default --fps), which the RAYTRACE
// card states.
inline constexpr std::uint32_t kShowFps = 60;

} // namespace evo::modern
