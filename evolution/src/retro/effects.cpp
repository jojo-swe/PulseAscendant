// The retro effect registry: which shader, rate, palette, text and background each chapter's 1993 side uses.
// Kept out of retro/effects.hpp so the header pulls in no effect's own header.

#include "retro/effects.hpp"

#include "retro/boot.hpp"
#include "retro/flyby.hpp"
#include "retro/palette.hpp"
#include "retro/scroller.hpp"

namespace evo::retro {

// FLYBY's greeting scrolls by the retro frame at the scroller's rate.
static_assert(kFlybyRetroRate == kScrollerRetroRate);

const EffectInfo& effectFor(show::Effect effect) {
    static constexpr EffectInfo kTestPattern{kTestPatternShader, kDefaultRetroRate, grayPalette};
    static constexpr EffectInfo kRaytrace{kTestPatternShader, kRaytraceRetroRate, grayPalette};
    static constexpr EffectInfo kBoot{nullptr, kBootRetroRate, bootPalette, bootText, bootBackground};
    static constexpr EffectInfo kPlasma{kPlasmaShader, kPlasmaRetroRate, plasmaPalette};
    static constexpr EffectInfo kFlyby{nullptr,   kFlybyRetroRate, flybyPalette,
                                       flybyText, nullptr,         flybyScene};
    static constexpr EffectInfo kCredits{nullptr, kScrollerRetroRate, creditsPalette, creditsText,
                                         creditsBackground};
    switch (effect) {
    case show::Effect::Boot:
        return kBoot;
    case show::Effect::Flyby:
        return kFlyby;
    case show::Effect::Plasma:
        return kPlasma;
    case show::Effect::Credits:
        return kCredits;
    case show::Effect::Raytrace:
        return kRaytrace;
    default:
        return kTestPattern;
    }
}

} // namespace evo::retro
