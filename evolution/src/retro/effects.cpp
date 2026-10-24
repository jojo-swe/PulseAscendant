// The retro effect registry: which shader, rate, palette, text, background, scene, texture and resolver each
// chapter's 1993 side uses. Kept out of retro/effects.hpp so the header pulls in no effect's own header.

#include "retro/effects.hpp"

#include "retro/boot.hpp"
#include "retro/city.hpp"
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

namespace evo::retro {

// FLYBY's greeting scrolls by the retro frame at the scroller's rate.
static_assert(kFlybyRetroRate == kScrollerRetroRate);
// PORTAL reuses FLYBY's ship, stars and flicker, all keyed by FLYBY's retro frame.
static_assert(kPortalRetroRate == kFlybyRetroRate);
// PORTAL's caption types with BOOT's prompt engine (promptWritten, promptCursorOn), keyed by BOOT's rate.
static_assert(kPortalRetroRate == kBootRetroRate);

EffectFrame resolveEffect(const EffectInfo& info, const show::TimelineFrame& frame,
                          std::uint32_t retroFrame) {
    return info.resolve ? info.resolve(frame, retroFrame) : EffectFrame{frame.effect, frame, retroFrame};
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
                                      .background = bootBackground};
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
    // The megamix: its own entry is the white flash between two cuts.
    static constexpr EffectInfo kFinale{.shader = nullptr,
                                        .retroRate = kFinaleRetroRate,
                                        .palette = finalePalette,
                                        .background = finaleBackground,
                                        .resolve = finaleResolve};
    static constexpr EffectInfo kCredits{.shader = nullptr,
                                         .retroRate = kScrollerRetroRate,
                                         .palette = creditsPalette,
                                         .text = creditsText,
                                         .background = creditsBackground};
    static constexpr EffectInfo kPortal{.shader = nullptr,
                                        .retroRate = kPortalRetroRate,
                                        .palette = portalPalette,
                                        .text = portalText,
                                        .scene = portalScene};
    static constexpr EffectInfo kReboot{
        .shader = nullptr, .retroRate = kBootRetroRate, .palette = bootPalette, .text = rebootText};
    static constexpr EffectInfo kTheEnd{
        .shader = nullptr, .retroRate = kTheEndRetroRate, .palette = theEndPalette, .text = theEndText};
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
    case show::Effect::Reboot:
        return kReboot;
    case show::Effect::TheEnd:
        return kTheEnd;
    default:
        return kTestPattern;
    }
}

} // namespace evo::retro
