// The palettes of the 1993 effects that cycle colours: built on the CPU per retro frame (retro/palette.hpp).

#include "retro/palette.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace evo::retro {

namespace {

struct Colour {
    int red, green, blue;
};

// The plasma's colour ring, six stops round the circle; the last runs back into the first. Neighbouring
// stops differ by at least 50 in some channel, so in the 37-entry halves of a gradient every entry differs
// from the next and a rotation by one changes all 224.
constexpr Colour kDeepViolet{40, 0, 90};
constexpr Colour kMagenta{210, 20, 170};
constexpr Colour kOrange{255, 120, 20};
constexpr Colour kYellow{255, 235, 90};
constexpr Colour kCyan{40, 220, 235};
constexpr Colour kDeepBlue{10, 30, 140};

// A gradient over `length` ring entries, from `from` through `middle` at its half to just short of `to` (the
// next gradient's first entry).
struct Gradient {
    Colour from, middle, to;
    std::size_t length;
};

constexpr std::array<Gradient, 3> kGradients = {
    Gradient{kDeepViolet, kMagenta, kOrange, 75},
    Gradient{kOrange, kYellow, kCyan, 75},
    Gradient{kCyan, kDeepBlue, kDeepViolet, 74},
};
static_assert(kGradients[0].length + kGradients[1].length + kGradients[2].length == kPlasmaColours);

std::uint32_t mix(const Colour& a, const Colour& b, double t) {
    const auto channel = [t](int from, int to) {
        return static_cast<std::uint8_t>(std::lround(from + (to - from) * t));
    };
    return rgba(channel(a.red, b.red), channel(a.green, b.green), channel(a.blue, b.blue));
}

// The ring in order, built once.
std::array<std::uint32_t, kPlasmaColours> buildRing() {
    std::array<std::uint32_t, kPlasmaColours> ring{};
    std::size_t at = 0;
    for (const Gradient& gradient : kGradients) {
        for (std::size_t i = 0; i < gradient.length; ++i) {
            const double t = 2.0 * static_cast<double>(i) / static_cast<double>(gradient.length); // 0..2
            ring[at++] =
                t < 1 ? mix(gradient.from, gradient.middle, t) : mix(gradient.middle, gradient.to, t - 1);
        }
    }
    return ring;
}

} // namespace

void plasmaPalette(Palette& palette, const show::TimelineFrame&, std::uint32_t retroFrame) {
    static const std::array<std::uint32_t, kPlasmaColours> kRing = buildRing();

    // 0 black, then the shadow ramp up to (not quite) the darkest violet; neither cycles.
    palette[0] = rgba(0, 0, 0);
    for (std::size_t i = 1; i < kPlasmaFirstIndex; ++i)
        palette[i] = mix({0, 0, 0}, kDeepViolet, static_cast<double>(i) / kPlasmaFirstIndex);

    // The ring, rotated by one entry per retro frame.
    const std::size_t shift = retroFrame % kPlasmaColours;
    for (std::size_t j = 0; j < kPlasmaColours; ++j)
        palette[kPlasmaFirstIndex + j] = kRing[(j + shift) % kPlasmaColours];
}

} // namespace evo::retro
