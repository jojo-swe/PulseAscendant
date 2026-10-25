// The retro effect registry: which shader, rate, palette, text, background, scene, texture, grid and
// phosphor each chapter's 1993 side uses. Kept out of retro/effects.hpp so the header pulls in no effect's
// own header.

#include "retro/effects.hpp"

#include "retro/boot.hpp"
#include "retro/city.hpp"
#include "retro/dos_idle.hpp"
#include "retro/dots.hpp"
#include "retro/finale.hpp"
#include "retro/flyby.hpp"
#include "retro/glenz.hpp"
#include "retro/palette.hpp"
#include "retro/portal.hpp"
#include "retro/raytrace.hpp"
#include "retro/rotozoom.hpp"
#include "retro/scroller.hpp"
#include "retro/the_end.hpp"
#include "retro/tunnel.hpp"

#include <cmath>

namespace evo::retro {

// FLYBY's greeting scrolls by the retro frame at the scroller's rate.
static_assert(kFlybyRetroRate == kScrollerRetroRate);
// PORTAL runs FINALE's grid on, frame for frame.
static_assert(kPortalRetroRate == kFinaleRetroRate);
// PORTAL's prompt, REBOOT's and RETURN's type with BOOT's prompt engine, keyed by BOOT's rate.
static_assert(kPortalRetroRate == kBootRetroRate && kDosIdleRetroRate == kBootRetroRate);

double phosphorDecay(std::uint32_t retroRate, double seconds) {
    return std::exp(-(1.0 / retroRate) / seconds);
}

const EffectInfo& effectFor(show::Effect effect) {
    static constexpr EffectInfo kTestPattern{
        .shader = kTestPatternShader, .retroRate = kDefaultRetroRate, .palette = grayPalette};
    static constexpr EffectInfo kRaytrace{
        .shader = kRaytraceShader, .retroRate = kRaytraceRetroRate, .palette = raytracePalette};
    static constexpr EffectInfo kBoot{.shader = nullptr,
                                      .retroRate = kBootRetroRate,
                                      .palette = bootPalette,
                                      .text = bootText,
                                      .background = bootBackground,
                                      .phosphor = true};
    static constexpr EffectInfo kPlasma{
        .shader = kPlasmaShader, .retroRate = kPlasmaRetroRate, .palette = plasmaPalette};
    static constexpr EffectInfo kFlyby{.shader = nullptr,
                                       .retroRate = kFlybyRetroRate,
                                       .palette = flybyPalette,
                                       .text = flybyText,
                                       .scene = flybyScene};
    static constexpr EffectInfo kDots{
        .shader = nullptr, .retroRate = kDotsRetroRate, .palette = dotsPalette, .scene = dotsScene};
    static constexpr EffectInfo kTunnel{
        .shader = nullptr, .retroRate = kTunnelRetroRate, .palette = tunnelPalette, .scene = tunnelScene};
    static constexpr EffectInfo kGlenzEffect{.shader = kGlenzCopperShader,
                                             .retroRate = kGlenzRetroRate,
                                             .palette = glenzPalette,
                                             .scene = glenzScene};
    static constexpr EffectInfo kCity{.shader = nullptr,
                                      .retroRate = kCityRetroRate,
                                      .palette = cityPalette,
                                      .background = cityBackground,
                                      .scene = cityScene};
    static constexpr EffectInfo kLens{.shader = kRotozoomShader,
                                      .retroRate = kRotozoomRetroRate,
                                      .palette = rotozoomPalette,
                                      .texture = rotozoomTexture};
    // The 3x3 grid; its own picture is the centre's scroller and the card.
    static constexpr EffectInfo kFinale{.shader = nullptr,
                                        .retroRate = kFinaleRetroRate,
                                        .palette = finalePalette,
                                        .text = finaleText,
                                        .scene = finaleScene,
                                        .grid = finaleGrid};
    static constexpr EffectInfo kCredits{.shader = nullptr,
                                         .retroRate = kScrollerRetroRate,
                                         .palette = creditsPalette,
                                         .text = creditsText,
                                         .background = creditsBackground,
                                         .scene = creditsScene,
                                         .phosphor = true,
                                         .phosphorSeconds = kPhosphorFastSeconds};
    static constexpr EffectInfo kPortal{.shader = nullptr,
                                        .retroRate = kPortalRetroRate,
                                        .palette = finalePalette,
                                        .text = portalText,
                                        .scene = portalScene,
                                        .grid = portalGrid};
    static constexpr EffectInfo kReboot{.shader = nullptr,
                                        .retroRate = kBootRetroRate,
                                        .palette = rebootPalette,
                                        .text = rebootText,
                                        .scene = rebootScene,
                                        .phosphor = true};
    // The Studio's CRT in STUDIO NIGHT: the DOS prompt the demo exited to.
    static constexpr EffectInfo kDosIdle{.shader = nullptr,
                                         .retroRate = kDosIdleRetroRate,
                                         .palette = bootPalette,
                                         .text = dosIdleText,
                                         .phosphor = true};
    static constexpr EffectInfo kReturn{.shader = nullptr,
                                        .retroRate = kDosIdleRetroRate,
                                        .palette = returnPalette,
                                        .text = returnText,
                                        .background = returnBackground,
                                        .phosphor = true};
    static constexpr EffectInfo kTheEnd{.shader = nullptr,
                                        .retroRate = kTheEndRetroRate,
                                        .palette = theEndPalette,
                                        .text = theEndText,
                                        .scene = theEndScene,
                                        .phosphor = true};
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
    case show::Effect::Finale:
        return kFinale;
    case show::Effect::Credits:
        return kCredits;
    case show::Effect::Raytrace:
        return kRaytrace;
    case show::Effect::Portal:
        return kPortal;
    case show::Effect::StudioNight:
        return kDosIdle;
    case show::Effect::Return:
        return kReturn;
    case show::Effect::Reboot:
        return kReboot;
    case show::Effect::TheEnd:
        return kTheEnd;
    default:
        return kTestPattern;
    }
}

const EffectInfo& finaleCardEffect() {
    static constexpr EffectInfo kCard{.shader = nullptr,
                                      .retroRate = kFinaleRetroRate,
                                      .palette = finaleCardPalette,
                                      .text = finaleCardText};
    return kCard;
}

} // namespace evo::retro
