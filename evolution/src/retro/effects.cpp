// The retro effect registry: which shader, rate, palette, text and background each chapter's 1993 side uses.
// Kept out of retro/effects.hpp so the header pulls in no effect's own header.

#include "retro/effects.hpp"

#include "retro/boot.hpp"
#include "retro/city.hpp"
#include "retro/dots.hpp"
#include "retro/flyby.hpp"
#include "retro/glenz.hpp"
#include "retro/palette.hpp"
#include "retro/portal.hpp"
#include "retro/raytrace.hpp"
#include "retro/rotozoom.hpp"
#include "retro/scroller.hpp"
#include "retro/the_end.hpp"
#include "retro/tunnel.hpp"

namespace evo::retro {

// FLYBY's greeting scrolls by the retro frame at the scroller's rate.
static_assert(kFlybyRetroRate == kScrollerRetroRate);

const EffectInfo& effectFor(show::Effect effect) {
    static constexpr EffectInfo kTestPattern{kTestPatternShader, kDefaultRetroRate, grayPalette};
    static constexpr EffectInfo kRaytrace{kRaytraceShader, kRaytraceRetroRate, raytracePalette};
    static constexpr EffectInfo kBoot{nullptr, kBootRetroRate, bootPalette, bootText, bootBackground};
    static constexpr EffectInfo kPlasma{kPlasmaShader, kPlasmaRetroRate, plasmaPalette};
    static constexpr EffectInfo kFlyby{nullptr,   kFlybyRetroRate, flybyPalette,
                                       flybyText, nullptr,         flybyScene};
    static constexpr EffectInfo kDots{nullptr, kDotsRetroRate, dotsPalette, nullptr, nullptr, dotsScene};
    static constexpr EffectInfo kTunnel{nullptr, kTunnelRetroRate, tunnelPalette,
                                        nullptr, nullptr,          tunnelScene};
    static constexpr EffectInfo kGlenzEffect{
        kGlenzCopperShader, kGlenzRetroRate, glenzPalette, nullptr, nullptr, glenzScene};
    static constexpr EffectInfo kCity{nullptr, kCityRetroRate, cityPalette,
                                      nullptr, cityBackground, cityScene};
    static constexpr EffectInfo kLens{kRotozoomShader, kRotozoomRetroRate, rotozoomPalette, nullptr, nullptr,
                                      nullptr,         rotozoomTexture};
    static constexpr EffectInfo kCredits{nullptr, kScrollerRetroRate, creditsPalette, creditsText,
                                         creditsBackground};
    static constexpr EffectInfo kPortal{nullptr,    kPortalRetroRate, portalPalette,
                                        portalText, nullptr,          portalScene};
    static constexpr EffectInfo kReboot{nullptr, kBootRetroRate, bootPalette, rebootText};
    static constexpr EffectInfo kTheEnd{nullptr, kTheEndRetroRate, theEndPalette, theEndText};
    switch (effect) {
    case show::Effect::Boot:
        return kBoot;
    case show::Effect::Flyby:
        return kFlyby;
    case show::Effect::Dots:
        return kDots;
    case show::Effect::Plasma:
        return kPlasma;
    case show::Effect::Tunnel:
        return kTunnel;
    case show::Effect::Glenz:
        return kGlenzEffect;
    case show::Effect::Lens:
        return kLens;
    case show::Effect::City:
        return kCity;
    case show::Effect::Credits:
        return kCredits;
    case show::Effect::Raytrace:
        return kRaytrace;
    case show::Effect::Portal:
        return kPortal;
    case show::Effect::Reboot:
        return kReboot;
    case show::Effect::TheEnd:
        return kTheEnd;
    default:
        return kTestPattern;
    }
}

} // namespace evo::retro
