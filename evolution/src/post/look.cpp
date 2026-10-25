#include "post/look.hpp"

#include "retro/effects.hpp"
#include "show/director.hpp"
#include "show/era.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace evo::post {

namespace {

constexpr int kFirstActTwo = 11;   // STUDIO NIGHT
constexpr int kFirstActThree = 20; // REBOOT
constexpr int kLastGraded = 28;    // ASCENDANT; CREDITS and THE END are 1993 only
constexpr double kJoinBars = 2;    // an act's first chapter eases from the previous act's look
constexpr double kFps = 60;        // the grain's frame clock

double smooth(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

Look mix(const Look& a, const Look& b, float t) {
    Look out = b;
    out.bloom = lerp(a.bloom, b.bloom, t);
    out.grade.contrast = lerp(a.grade.contrast, b.grade.contrast, t);
    for (int i = 0; i < 3; ++i)
        out.grade.lift[i] = lerp(a.grade.lift[i], b.grade.lift[i], t);
    out.grade.splitTone = lerp(a.grade.splitTone, b.grade.splitTone, t);
    out.grade.shadowDesaturation = lerp(a.grade.shadowDesaturation, b.grade.shadowDesaturation, t);
    out.grade.grain = lerp(a.grade.grain, b.grade.grain, t);
    return out;
}

// The transition marks of a chapter (from its middle: every frame of a chapter gives the same).
show::TransitionMarks marksOf(int chapter) {
    return show::transitionMarks(
        show::sampleTimeline(0.5 * (show::chapterStart(chapter) + show::chapterStart(chapter + 1))));
}

// The keys, from the show's own chapter starts, accents, downbeats and bars wherever the direction ties an
// event to them; the few shot times the direction names in seconds are written as such.
std::vector<ExposureKey> buildKeys() {
    constexpr int kRaytrace = 7, kStudioNight = 11, kSingularity = 18, kReturn = 19, kWormhole = 23,
                  kMetropolis = 27, kAscendant = 28;
    const double raytraceEnd = show::chapterStart(kRaytrace + 1);
    const show::TransitionMarks night = marksOf(kStudioNight);
    const double singularityEnd = show::chapterStart(kSingularity + 1);
    const double returnEnd = show::chapterStart(kReturn + 1);
    const double rebalance = marksOf(kReturn).accent; // the CRT comes on
    const double rebalanceBeat = show::sampleTimeline(rebalance).beatSeconds;
    const double wormholeEnd = show::chapterStart(kWormhole + 1);
    const double blink = marksOf(kMetropolis).accent; // every window of the city blinks on
    const double blinkBeat = show::sampleTimeline(blink).beatSeconds;
    const double ascendantEnd = show::chapterStart(kAscendant + 1);
    return {
        {0.0, 0.f},
        // RAYTRACE: the wide eases down one stop over its last two bars; VECTOR CITY is a cut.
        {raytraceEnd - 2 * marksOf(kRaytrace).bar, 0.f},
        {raytraceEnd, -1.f},
        {raytraceEnd, 0.f},
        // STUDIO NIGHT: exposure rises from D to the accent as the camera nears the pane (the window's
        // reflection of the room fades), held to the cut to ASCENT.
        {night.downbeat, 0.f},
        {night.accent, 0.7f},
        {show::chapterStart(kStudioNight + 1), 0.7f},
        {show::chapterStart(kStudioNight + 1), 0.f},
        // SINGULARITY: the slow fall toward the horizon (from 378 s) ends in white, which RETURN resolves from
        // by 394 s.
        {378.0, 0.f},
        {singularityEnd, 6.f},
        {394.0, 0.f},
        // RETURN: one beat as the room rebalances to the screen. The Studio's own exposure is 0.2 lower in
        // REBOOT than in RETURN, so the offset hands over exactly to the next chapter's base: the join stays
        // one continuous exposure.
        {rebalance, 0.f},
        {rebalance + rebalanceBeat, -0.2f},
        {returnEnd, -0.2f},
        {returnEnd, 0.f},
        // WORMHOLE: the white-out over its last two bars, held into CRYSTAL CITY, whose sunrise resolves it by
        // 520 s.
        {wormholeEnd - 2 * marksOf(kWormhole).bar, 0.f},
        {wormholeEnd, 6.f},
        {520.0, 0.f},
        // METROPOLIS: the city-wide blink is the brightest beat of the chapter.
        {blink, 0.f},
        {blink + 0.5 * blinkBeat, 0.3f},
        {blink + blinkBeat, 0.f},
        // ASCENDANT: from the downbeat a bar before the release (666.71) the frame goes to white; CREDITS is
        // 1993 only.
        {664.97, 0.f},
        {ascendantEnd, 6.f},
        {ascendantEnd, 0.f},
    };
}

} // namespace

int actOf(int chapter) {
    if (chapter < 0 || chapter > kLastGraded)
        return 0;
    return chapter < kFirstActTwo ? 1 : chapter < kFirstActThree ? 2 : 3;
}

Look actLook(int act) {
    Look look;
    look.act = act;
    switch (act) {
    case 1: // phosphor and tungsten: a film of a room
        look.bloom = 0.020f;
        look.grade.splitTone = 0.02f;
        look.grade.grain = 0.004f;
        break;
    case 2: // sodium and ice: night exteriors
        look.bloom = 0.025f;
        look.grade.contrast = 0.05f;
        look.grade.lift = {0.004f * 0.6f, 0.004f * 0.8f, 0.004f};
        look.grade.grain = 0.006f;
        break;
    case 3: // solar and mineral: highlights clip into glare, the shadows step back
        look.bloom = 0.035f;
        look.grade.shadowDesaturation = 0.08f;
        look.grade.grain = 0.003f;
        break;
    default:
        look.act = 0;
        break;
    }
    return look;
}

double easedCruise(double u) {
    constexpr double a = 0.25;             // the easing share at each end
    constexpr double speed = 1.0 / (1 - a); // the cruise speed; the mean is 1
    u = std::clamp(u, 0.0, 1.0);
    const auto ease = [&](double v) { // the distance after v of the acceleration: speed a (x^3 - x^4 / 2)
        const double x = v / a;
        return speed * a * (x * x * x - 0.5 * x * x * x * x);
    };
    if (u < a)
        return ease(u);
    if (u > 1 - a)
        return 1.0 - ease(1.0 - u);
    return speed * (0.5 * a + (u - a));
}

std::span<const ExposureKey> exposureKeys() {
    static const std::vector<ExposureKey> keys = buildKeys();
    return keys;
}

float exposureOffset(double showSeconds) {
    const std::span<const ExposureKey> keys = exposureKeys();
    if (showSeconds < keys.front().seconds)
        return keys.front().ev;
    // The last key at or before t (after a jump, the later of the two).
    std::size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].seconds <= showSeconds)
        ++i;
    if (i + 1 == keys.size())
        return keys.back().ev;
    const ExposureKey& a = keys[i];
    const ExposureKey& b = keys[i + 1];
    const double u = (showSeconds - a.seconds) / (b.seconds - a.seconds);
    return static_cast<float>(a.ev + (b.ev - a.ev) * easedCruise(u));
}

float portalBloomScale(const show::TimelineFrame& frame) {
    if (frame.transition != show::Transition::Portal)
        return 1.f;
    const show::PortalPhase phase = show::portalPhase(frame);
    if (phase.sinceGlass < 0)
        return static_cast<float>(1.0 - smooth(phase.glass));
    return static_cast<float>(smooth(phase.sinceGlass / show::transitionMarks(frame).bar));
}

float paletteWeight(const show::TimelineFrame& frame) {
    if (frame.transition != show::Transition::Portal)
        return 0.f;
    const show::PortalPhase phase = show::portalPhase(frame);
    if (phase.sinceGlass < 0)
        return 0.f;
    return static_cast<float>(1.0 - smooth(phase.sinceGlass / show::transitionMarks(frame).bar));
}

GradePalette crossingPalette(const show::TimelineFrame& frame) {
    const show::TimelineFrame atGlass = show::sampleTimeline(show::transitionMarks(frame).downbeat);
    const retro::EffectInfo& info = retro::effectFor(atGlass.effect);
    const auto retroFrame = static_cast<std::uint32_t>(std::floor(atGlass.localSeconds * info.retroRate));
    const retro::EffectFrame shown = retro::resolveEffect(info, atGlass, retroFrame);
    retro::Palette palette{};
    retro::effectFor(shown.effect).palette(palette, shown.frame, shown.retroFrame);
    return palette;
}

Look lookFor(const show::TimelineFrame& frame) {
    const int act = actOf(frame.chapter);
    Look look = actLook(act);
    if (act == 0)
        return look;
    const bool firstOfAct = frame.chapter == kFirstActTwo || frame.chapter == kFirstActThree;
    if (firstOfAct && frame.localSeconds < kJoinBars * show::transitionMarks(frame).bar) {
        const double t = frame.localSeconds / (kJoinBars * show::transitionMarks(frame).bar);
        look = mix(actLook(act - 1), look, static_cast<float>(smooth(t)));
    }
    look.bloom *= portalBloomScale(frame);
    look.grade.grainFrame = static_cast<std::uint32_t>(std::llround(std::max(frame.showSeconds, 0.0) * kFps));
    look.exposureOffset = exposureOffset(frame.showSeconds);
    look.grade.palette = paletteWeight(frame);
    if (look.grade.palette > 0.f)
        look.palette = crossingPalette(frame);
    return look;
}

} // namespace evo::post
