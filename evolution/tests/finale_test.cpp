// FINALE, chapter 9: the megamix. On the CPU: the cuts fall on every kFinaleBeatsPerCut-th tracked beat of
// the chapter (the flash frame is the retro frame the beat falls in), every sub-effect shows, in order, with
// exactly one flash frame per cut, and each one gets its home chapter's frame and a home retro frame that
// advances at its own rate. On the GPU, through the retro stage: a FINALE frame equals the sub-effect's own
// frame at the same home retro frame byte for byte (indices and colours; the own frame without its text,
// since FINALE draws its own text, none, over a sub-effect), a flash frame is all white, and a FINALE frame
// is the same after another frame and from a fresh stage. Runs with the validation layer on and fails on any
// message it reports.

#include "gfx/context.hpp"
#include "retro/dots.hpp"
#include "retro/effects.hpp"
#include "retro/finale.hpp"
#include "retro/tunnel.hpp"
#include "retro_stage_fixture.hpp"
#include "show/cues.hpp"
#include "show/music_curves.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::EffectFrame;
using evo::retro::EffectInfo;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::test::RetroStageFixture;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

constexpr int kFinaleChapter = 9;
constexpr std::uint32_t kRate = evo::retro::kFinaleRetroRate;

// A FINALE frame at `fraction` of the way through retro frame `retroFrame`.
TimelineFrame finale(std::uint32_t retroFrame, double fraction = 0.5) {
    const TimelineFrame frame =
        evo::show::sampleTimeline(evo::show::chapterStart(kFinaleChapter) + (retroFrame + fraction) / kRate);
    if (frame.chapter != kFinaleChapter || frame.effect != Effect::Finale)
        throw std::runtime_error("expected a FINALE frame at retro frame " + std::to_string(retroFrame));
    return frame;
}

std::uint32_t frameCount() {
    return static_cast<std::uint32_t>(std::floor(finale(0, 0).chapterDuration * kRate));
}

EffectFrame resolved(std::uint32_t retroFrame) {
    return evo::retro::resolveEffect(evo::retro::effectFor(Effect::Finale), finale(retroFrame), retroFrame);
}

// The chapters FLYBY, DOTS, TUNNEL, GLENZ, LENS and RAYTRACE have of their own.
int homeChapter(Effect effect) {
    switch (effect) {
    case Effect::Flyby:
        return 1;
    case Effect::Dots:
        return 2;
    case Effect::Tunnel:
        return 4;
    case Effect::Glenz:
        return 5;
    case Effect::Lens:
        return 6;
    case Effect::Raytrace:
        return 7;
    default:
        return -1;
    }
}

// The registry entry: its own flash entry with the resolver, no shader and no text.
void testRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::Finale);
    CHECK(info.resolve == evo::retro::finaleResolve);
    CHECK(info.shader == nullptr && info.scene == nullptr && info.text == nullptr && info.texture == nullptr);
    CHECK(info.retroRate == kRate);
    for (Effect sub : evo::retro::kFinaleCuts)
        CHECK(evo::retro::effectFor(sub).resolve == nullptr);
    // An entry without a resolver resolves to itself.
    const TimelineFrame dots = evo::show::sampleTimeline(evo::show::chapterStart(2) + 1);
    const EffectFrame self = evo::retro::resolveEffect(evo::retro::effectFor(Effect::Dots), dots, 35);
    CHECK(self.effect == Effect::Dots && self.retroFrame == 35 && self.frame.showSeconds == dots.showSeconds);
}

// The cuts are every kFinaleBeatsPerCut-th tracked beat of the chapter, counted from the beat it starts on;
// each flash is the retro frame its beat falls in.
void testCutsOnTheBeatGrid() {
    const TimelineFrame frame = finale(0, 0);
    const double start = evo::show::chapterStart(kFinaleChapter);
    const int track = evo::show::trackAt(start);
    const double offset = start - evo::show::trackStart(track);
    const evo::show::BeatGridView grid = evo::show::beatGrid(track);
    std::vector<double> beats; // the chapter's beats, chapter seconds
    for (int beat = 0; beat < grid.count(); ++beat) {
        const double local = grid.beatTime(beat) - offset;
        if (local > -1e-6 && local < frame.chapterDuration)
            beats.push_back(local);
    }
    CHECK(beats.size() > 20);
    CHECK(std::abs(beats[0]) < 1e-6); // the chapter starts on a beat

    const std::vector<std::uint32_t> cuts = evo::retro::finaleCutFrames(frame);
    std::size_t expected = 0;
    for (std::size_t i = evo::retro::kFinaleBeatsPerCut; i < beats.size();
         i += evo::retro::kFinaleBeatsPerCut) {
        CHECK(expected < cuts.size());
        const double at = beats[i] * kRate;
        CHECK(cuts[expected] <= at && at < cuts[expected] + 1.0);
        ++expected;
    }
    CHECK(cuts.size() == expected);
    CHECK(cuts.size() >= evo::retro::kFinaleCuts.size());
    // The same for any frame of the chapter: fixed facts only.
    CHECK(evo::retro::finaleCutFrames(finale(200, 0.9)) == cuts);
    std::printf("finale: %zu cuts every %d beats, at retro frames %u .. %u of %u\n", cuts.size(),
                evo::retro::kFinaleBeatsPerCut, cuts.front(), cuts.back(), frameCount());
}

// Every retro frame of the chapter: the flash exactly on the cut frames, the sub-effects in order between
// them (FLYBY first), all six shown, each in its home chapter's frame, advancing at its own rate.
void testEveryFrameResolves() {
    const std::vector<std::uint32_t> cuts = evo::retro::finaleCutFrames(finale(0));
    const std::set<std::uint32_t> flashes(cuts.begin(), cuts.end());
    std::set<Effect> shown;
    std::size_t flashCount = 0;
    int segment = 0;
    std::uint32_t segmentStart = 0;
    for (std::uint32_t retroFrame = 0; retroFrame < frameCount(); ++retroFrame) {
        const EffectFrame r = resolved(retroFrame);
        if (flashes.contains(retroFrame)) {
            CHECK(r.effect == Effect::Finale && r.retroFrame == retroFrame &&
                  r.frame.chapter == kFinaleChapter);
            ++flashCount;
            ++segment;
            segmentStart = retroFrame;
            continue;
        }
        CHECK(r.effect == evo::retro::kFinaleCuts[segment % evo::retro::kFinaleCuts.size()]);
        shown.insert(r.effect);
        // The home chapter's frame: its chapter and effect, the retro frame's middle.
        const int home = evo::retro::finaleHomeChapter(r.effect);
        const EffectInfo& sub = evo::retro::effectFor(r.effect);
        CHECK(home == homeChapter(r.effect));
        CHECK(r.frame.chapter == home && r.frame.effect == r.effect);
        CHECK(r.frame.chapterDuration ==
              evo::show::sampleTimeline(evo::show::chapterStart(home)).chapterDuration);
        CHECK(static_cast<std::uint32_t>(std::floor(r.frame.localSeconds * sub.retroRate)) == r.retroFrame);
        // From the sub-effect's start, at its own rate.
        const std::uint32_t elapsed = retroFrame - segmentStart;
        CHECK(r.retroFrame == evo::retro::finaleHomeStart(r.effect) + elapsed * sub.retroRate / kRate);
    }
    CHECK(flashCount == cuts.size());
    CHECK(shown.size() == evo::retro::kFinaleCuts.size());
    CHECK(resolved(0).effect == Effect::Flyby);
}

// The home starting moments show each effect at its best, well inside its chapter.
void testHomeStarts() {
    for (Effect sub : evo::retro::kFinaleCuts) {
        const int home = evo::retro::finaleHomeChapter(sub);
        const double duration = evo::show::sampleTimeline(evo::show::chapterStart(home)).chapterDuration;
        const std::uint32_t rate = evo::retro::effectFor(sub).retroRate;
        // Room for a whole cut of eight beats after the start.
        CHECK(evo::retro::finaleHomeStart(sub) + 4.0 * rate < duration * rate);
    }
    const TimelineFrame tunnel = evo::retro::finaleHomeFrame(Effect::Tunnel, 0);
    CHECK(evo::retro::tunnelTubeFront(tunnel, evo::retro::finaleHomeStart(Effect::Tunnel)) ==
          evo::retro::kTunnelTubeNear);
    const TimelineFrame dots = evo::retro::finaleHomeFrame(Effect::Dots, 0);
    const evo::retro::DotsMorph year =
        evo::retro::dotsMorph(dots, evo::retro::finaleHomeStart(Effect::Dots) + 15);
    CHECK(year.from == evo::retro::DotsShape::Year && year.to == evo::retro::DotsShape::Year);
}

// The own frame of a sub-effect without its text, as FINALE shows it.
const EffectInfo& withoutText(Effect effect) {
    static const std::array<EffectInfo, 31> entries = [] {
        std::array<EffectInfo, 31> all{};
        for (int e = 0; e < 31; ++e) {
            all[static_cast<std::size_t>(e)] = evo::retro::effectFor(static_cast<Effect>(e));
            all[static_cast<std::size_t>(e)].text = nullptr;
        }
        return all;
    }();
    return entries[static_cast<std::size_t>(effect)];
}

bool allWhite(const evo::test::RetroPicture& picture) {
    return std::all_of(picture.indices.begin(), picture.indices.end(),
                       [](std::uint8_t i) { return i == evo::retro::kFinaleWhite; }) &&
           std::all_of(picture.rgb.begin(), picture.rgb.end(), [](std::uint8_t c) { return c == 255; });
}

void testImages(evo::gfx::Context& ctx) {
    RetroStageFixture mix(ctx, true);
    RetroStageFixture own(ctx, true, withoutText);
    const std::vector<std::uint32_t> cuts = evo::retro::finaleCutFrames(finale(0));

    // Each sub-effect's first cut, at its first frame after the flash and in its middle: the sub-effect's own
    // frame at the same home retro frame.
    std::set<Effect> compared;
    for (std::size_t c = 0; c + 1 < cuts.size() && compared.size() < evo::retro::kFinaleCuts.size(); ++c) {
        for (std::uint32_t retroFrame : {cuts[c] + 1, (cuts[c] + cuts[c + 1]) / 2}) {
            const EffectFrame r = resolved(retroFrame);
            const evo::test::RetroPicture picture = mix.renderPicture(finale(retroFrame));
            const evo::test::RetroPicture reference =
                own.renderPicture(evo::retro::finaleHomeFrame(r.effect, r.retroFrame));
            CHECK(picture.indices == reference.indices);
            CHECK(picture.rgb == reference.rgb);
            const std::set<std::uint8_t> distinct(picture.indices.begin(), picture.indices.end());
            CHECK(distinct.size() > 4);
            compared.insert(r.effect);
        }
    }
    // FLYBY at the chapter's start too, before the first cut: without its greeting.
    CHECK(mix.renderPicture(finale(3)).indices ==
          own.renderPicture(
                 evo::retro::finaleHomeFrame(Effect::Flyby, evo::retro::finaleHomeStart(Effect::Flyby) + 3))
              .indices);
    CHECK(compared.size() == evo::retro::kFinaleCuts.size());

    // One white frame at each cut, the frames either side not white.
    for (std::uint32_t cut : cuts) {
        CHECK(allWhite(mix.renderPicture(finale(cut, 0.1))));
        CHECK(!allWhite(mix.renderPicture(finale(cut - 1))));
        if (cut + 1 < frameCount())
            CHECK(!allWhite(mix.renderPicture(finale(cut + 1))));
    }

    // Deterministic: the same retro frame (another moment of it) after another frame, and from a fresh stage.
    for (std::uint32_t retroFrame : {cuts[2] + 5, cuts[4] + 3, cuts[5] + 9}) {
        const std::vector<std::uint8_t> first = mix.render(finale(retroFrame, 0.1));
        mix.render(finale(retroFrame + 7));
        CHECK(mix.render(finale(retroFrame, 0.9)) == first);
        RetroStageFixture fresh(ctx);
        CHECK(fresh.render(finale(retroFrame, 0.5)) == first);
    }
}

} // namespace

int main() {
    try {
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testCutsOnTheBeatGrid();
        testEveryFrameResolves();
        testHomeStarts();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testImages(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("finale_test: %d checks passed\n", checksPassed);
    return 0;
}
