// The director on the CPU: the FINALE megamix's cut list and REBOOT's look ahead at the galaxy, the frames
// their scenes render, the era the cards follow and the compositor's flash policy.
//   (a) the megamix rows start where the table says, on the beat grid (quarter beats, within 1 ms), from the
//       chapter's first beat to D, which ends the last row exactly;
//   (b) at least 8 distinct sources, each row's scene time inside its shot of the source chapter from the
//       first to the last frame of the row (WORMHOLE's round its ship pass, which follows Act III's track),
//       the drop held on one time;
//   (c) the room holds over [D, 182.52) and around the thank-you card; every 1/60 s frame of the show has a
//       cut only in FINALE's cut rows and in REBOOT from D on; era is 1 in cuts and 0 in the room; the card
//       is exactly the three frames 10420-10422, which alone in FINALE do not hold the 2026 picture;
//   (d) a cut frame is the source's own frame but for the era fields; REBOOT's GALAXY frame is GALAXY's
//   chapter
//       with a negative local time that runs continuously into the chapter's first frame;
//   (e) the flash: none in FINALE or a Portal chapter, kEraFlash * eraCut in the other BeatSwaps.

#include "show/cues.hpp"
#include "show/director.hpp"
#include "show/era.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

namespace show = evo::show;
using show::Effect;
using show::kMegamixShots;
using show::sampleTimeline;
using show::SceneCut;
using show::TimelineFrame;

constexpr double kFrame = 1.0 / 60.0;
constexpr int kFinale = 9, kReboot = 20, kGalaxy = 21;
constexpr std::size_t kRows = kMegamixShots.size();

// The table's start times, rounded as written down, and the end (D).
constexpr std::array<double, kRows + 1> kListedStarts = {170.29, 171.14, 171.99, 172.84, 173.24, 173.45,
                                                         173.66, 174.09, 174.30, 174.41, 174.51, 174.93};

// The shot of each cut's source chapter the cut must stay inside, in show seconds ([0, 0] for the room and
// for WORMHOLE, whose range is wormholeRange()).
struct Range {
    double from, to;
};
constexpr std::size_t kWormholeRow = 9;
constexpr std::array<Range, kRows> kShotRanges = {{
    {131.5, 140.0},   // RAYTRACE S3
    {158.05, 165.0},  // VECTOR CITY S2, the puddle
    {38.4, 44.13},    // DOTS S2, the logo
    {104.80, 115.38}, // LENS S2, the sphere
    {96.3, 98.87},    // GLENZ S2, the floor
    {0, 0},
    {0, 0},
    {75.35, 80.65},   // TUNNEL S2
    {278.15, 289.70}, // NEBULA S2, the star
    {0, 0},           // WORMHOLE, the ship crossing: wormholeRange()
    {128.07, 131.5},  // RAYTRACE S2, the drop
}};

// WORMHOLE's ship crossing: a tenth of a second either side of its pass, which lies in the chapter's first
// half, at least 2 s before D (modern::shipTimes; scene_ship_test checks the two agree).
Range wormholeRange() {
    const int chapter = static_cast<int>(Effect::Wormhole);
    const double start = show::chapterStart(chapter), end = show::chapterStart(chapter + 1);
    const double pass = show::wormholePass();
    const double d = show::transitionMarks(sampleTimeline(start + .01)).downbeat;
    CHECK(pass > start + 0.1 && pass < start + .5 * (end - start) && pass <= d - 2.0 + 1e-9);
    return {pass - 0.1, pass + 0.1};
}

TimelineFrame finaleAt(double t) {
    const TimelineFrame frame = sampleTimeline(t);
    CHECK(frame.effect == Effect::Finale);
    return frame;
}

double finaleDownbeat() {
    return show::transitionMarks(sampleTimeline(show::chapterStart(kFinale) + .01)).downbeat;
}

// Whether a show time lies on a quarter of a tracked beat of track 0 (FINALE's), within 1 ms.
bool onQuarterBeat(double t) {
    const show::BeatGridView grid = show::beatGrid(0);
    const int beat = grid.beatAt(t + 1e-6);
    if (beat < 0 || beat + 1 >= grid.count())
        return false;
    const double from = grid.beatTime(beat), length = grid.beatTime(beat + 1) - from;
    const double quarters = (t - from) / length * 4.0;
    return std::abs(quarters - std::round(quarters)) * length / 4.0 < 1e-3;
}

// --- (a) ---------------------------------------------------------------------------------------------------

void testSchedule() {
    const double d = finaleDownbeat();
    std::printf("FINALE: start %.4f, D %.4f\n", show::chapterStart(kFinale), d);
    CHECK(std::abs(d - 174.93) < 0.01);
    CHECK(show::megamixStart(0) == show::chapterStart(kFinale));
    CHECK(std::abs(show::megamixStart(static_cast<int>(kRows)) - d) < 1e-9); // stops dead on D
    for (std::size_t row = 0; row <= kRows; ++row) {
        const double start = show::megamixStart(static_cast<int>(row));
        std::printf("  row %2zu at %.4f\n", row, start);
        CHECK(onQuarterBeat(start));
        CHECK(std::abs(start - kListedStarts[row]) < 0.025);
        if (row > 0)
            CHECK(start > show::megamixStart(static_cast<int>(row) - 1));
    }
    // The fastest cuts are a quarter beat long, the slowest two beats.
    for (std::size_t row = 0; row < kRows; ++row) {
        const double length =
            show::megamixStart(static_cast<int>(row) + 1) - show::megamixStart(static_cast<int>(row));
        CHECK(length > 0.09 && length < 0.86);
    }
}

// --- (b) ---------------------------------------------------------------------------------------------------

void testSources() {
    std::set<Effect> sources;
    for (std::size_t row = 0; row < kRows; ++row) {
        const show::MegamixShot& shot = kMegamixShots[row];
        const double start = show::megamixStart(static_cast<int>(row));
        const double end = show::megamixStart(static_cast<int>(row) + 1);
        if (shot.source == Effect::Finale) {
            CHECK(!show::sceneCut(finaleAt(start)).has_value());
            continue;
        }
        sources.insert(shot.source);
        const Range range = row == kWormholeRow ? wormholeRange() : kShotRanges[row];
        CHECK((shot.from == show::MegamixFrom::ShipPass) == (row == kWormholeRow));
        CHECK(show::megamixSource(static_cast<int>(row)) ==
              (row == kWormholeRow ? show::wormholePass() + shot.sourceSeconds : shot.sourceSeconds));
        for (const double t : {start, start + kFrame, (start + end) * .5, end - 1e-4}) {
            const std::optional<SceneCut> cut = show::sceneCut(finaleAt(t));
            CHECK(cut.has_value());
            CHECK(cut->chapter == static_cast<int>(shot.source));
            CHECK(cut->frozen == shot.frozen);
            const double source = show::megamixSource(static_cast<int>(row));
            const double expected = shot.frozen ? source : source + (t - start);
            CHECK(std::abs(cut->showSeconds - expected) < 1e-9);
            CHECK(cut->showSeconds >= range.from && cut->showSeconds < range.to);
            CHECK(sampleTimeline(cut->showSeconds).effect == shot.source);
        }
    }
    CHECK(sources.size() >= 8);
    CHECK(kMegamixShots.back().frozen && kMegamixShots.back().source == Effect::Raytrace);
}

// --- (c) ---------------------------------------------------------------------------------------------------

// The megamix row a FINALE time lies in, or kRows outside the megamix.
std::size_t rowAt(double t) {
    for (std::size_t row = 0; row < kRows; ++row)
        if (t + 1e-9 >= show::megamixStart(static_cast<int>(row)) &&
            t + 1e-9 < show::megamixStart(static_cast<int>(row) + 1))
            return row;
    return kRows;
}

void testCutsAcrossTheShow() {
    const double d = finaleDownbeat();
    const double finaleEnd = show::chapterStart(kFinale + 1);
    CHECK(std::abs(finaleEnd - 182.52) < 0.01);
    for (double t = d; t < finaleEnd; t += kFrame) {
        const TimelineFrame frame = finaleAt(t);
        CHECK(!show::sceneCut(frame).has_value());
        CHECK(frame.era == 0.f);
    }
    CHECK(!show::sceneCut(finaleAt(173.66 - kFrame)).has_value());
    CHECK(!show::sceneCut(finaleAt(173.71 + kFrame)).has_value());

    const show::TransitionMarks reboot =
        show::transitionMarks(sampleTimeline(show::chapterStart(kReboot) + .01));
    const double rebootD = reboot.downbeat;
    std::printf("REBOOT: D %.4f\n", rebootD);
    // D is a downbeat of Act III's track with the Portal's two bars either side inside the chapter, and the
    // accent follows it within the bar.
    const show::BeatGridView act3 = show::beatGrid(2);
    const int dBeat = act3.beatAt(rebootD - show::chapterStart(kReboot) + 1e-6);
    CHECK(act3.bar(dBeat) == 0);
    CHECK(std::abs(act3.beatTime(dBeat) + show::chapterStart(kReboot) - rebootD) < 1e-6);
    CHECK(rebootD - 2 * reboot.bar > show::chapterStart(kReboot));
    CHECK(rebootD + 2 * reboot.bar < show::chapterStart(kGalaxy));
    CHECK(reboot.accent > rebootD && reboot.accent < rebootD + reboot.bar);

    long cutFrames = 0, frames = 0, cardFrames = 0;
    for (long i = 0; i / 60.0 < show::showDuration(); ++i, ++frames) {
        const double t = i / 60.0;
        const TimelineFrame frame = sampleTimeline(t);
        const std::optional<SceneCut> cut = show::sceneCut(frame);
        CHECK(show::holdsModern(frame) == (frame.chapter == kFinale && !show::thankYouCard(frame)));
        cardFrames += show::thankYouCard(frame) ? 1 : 0;
        if (frame.chapter == kFinale) {
            const std::size_t row = rowAt(t);
            const bool cutRow = row < kRows && kMegamixShots[row].source != Effect::Finale;
            CHECK(cut.has_value() == cutRow);
            CHECK(frame.era == (cutRow ? 1.f : 0.f));
        } else if (frame.chapter == kReboot) {
            CHECK(cut.has_value() == (t + 1e-9 >= rebootD));
            if (cut)
                CHECK(cut->chapter == kGalaxy && cut->showSeconds == t && !cut->frozen);
        } else {
            CHECK(!cut.has_value());
        }
        cutFrames += cut ? 1 : 0;
    }
    std::printf("sweep: %ld frames, %ld with a cut\n", frames, cutFrames);
    CHECK(frames >= static_cast<long>(60 * show::showDuration()));
    CHECK(cutFrames > 60 * 4); // the megamix's cut rows (about 3.9 s) and REBOOT from D to GALAXY
    CHECK(cardFrames == show::kThankYouFrames);
}

// The thank-you card: exactly the 60 fps frames 10420, 10421 and 10422 (173.6667 to 173.7000), the first
// three of the card row from 173.6631; the room (no cut, era 0, the 2026 picture held) on either side.
void testThankYouCard() {
    const double start = show::megamixStart(static_cast<int>(show::kThankYouRow));
    std::printf("thank-you card from %.4f\n", start);
    CHECK(std::abs(start - 173.6631) < 1e-3);
    CHECK(show::kMegamixShots[show::kThankYouRow].source == Effect::Finale);
    for (long i = 10416; i <= 10426; ++i) {
        const TimelineFrame frame = finaleAt(i / 60.0);
        const bool card = i >= 10420 && i <= 10422;
        CHECK(show::thankYouCard(frame) == card);
        CHECK(show::holdsModern(frame) == !card);
        CHECK(frame.era == 0.f); // the room's rows 5 and 6 (from 173.4523), the card included
        CHECK(!show::sceneCut(frame).has_value());
    }
    CHECK(!show::thankYouCard(sampleTimeline(173.68 - 60.0))); // only in FINALE
}

// --- (d) ---------------------------------------------------------------------------------------------------

void testSceneFrames() {
    // A cut frame renders its source's frame, but for the era fields, which stay the frame's own.
    const double t = (show::megamixStart(1) + show::megamixStart(2)) * .5; // the puddle
    const TimelineFrame frame = finaleAt(t);
    const SceneCut cut = *show::sceneCut(frame);
    const TimelineFrame scene = show::sceneFrame(frame, cut);
    const TimelineFrame source = sampleTimeline(cut.showSeconds);
    CHECK(scene.chapter == source.chapter && scene.effect == Effect::City);
    CHECK(scene.showSeconds == source.showSeconds && scene.localSeconds == source.localSeconds);
    CHECK(scene.kick == source.kick && scene.flowSeconds == source.flowSeconds);
    CHECK(scene.cameraDrive == source.cameraDrive && scene.beatIndex == source.beatIndex);
    CHECK(scene.era == frame.era && scene.wipe == frame.wipe && scene.eraCut == frame.eraCut &&
          scene.swapCount == frame.swapCount);
    CHECK(show::sceneFrame(frame).chapter == scene.chapter);
    // The room renders the frame itself.
    const TimelineFrame room = finaleAt(178.0);
    CHECK(show::sceneFrame(room).chapter == kFinale && show::sceneFrame(room).showSeconds == 178.0);

    // The held drop: the same scene time on every frame of its row.
    const double dropStart = show::megamixStart(static_cast<int>(kRows) - 1);
    const TimelineFrame a = show::sceneFrame(finaleAt(dropStart + kFrame));
    const TimelineFrame b = show::sceneFrame(finaleAt(dropStart + 10 * kFrame));
    CHECK(a.showSeconds == b.showSeconds && a.showSeconds == 128.10 && a.effect == Effect::Raytrace);

    // REBOOT from D: GALAXY's chapter, local time negative until GALAXY starts, then continuous with it.
    const double galaxyStart = show::chapterStart(kGalaxy);
    const TimelineFrame galaxyFirst = sampleTimeline(galaxyStart);
    CHECK(galaxyFirst.effect == Effect::Galaxy);
    for (const double lead : {5.0, 1.0, kFrame, 1e-4}) {
        const TimelineFrame outer = sampleTimeline(galaxyStart - lead);
        CHECK(outer.effect == Effect::Reboot);
        const TimelineFrame ahead = show::sceneFrame(outer);
        CHECK(ahead.chapter == kGalaxy && ahead.effect == Effect::Galaxy);
        CHECK(ahead.transition == galaxyFirst.transition);
        CHECK(std::abs(ahead.localSeconds + lead) < 1e-9);
        CHECK(ahead.chapterDuration == galaxyFirst.chapterDuration);
        CHECK(ahead.accentSeconds == galaxyFirst.accentSeconds);
        CHECK(ahead.chapterProgress == 0.f);
        CHECK(ahead.showSeconds == outer.showSeconds && ahead.kick == outer.kick);
        CHECK(ahead.era == outer.era && ahead.eraCut == outer.eraCut);
        CHECK(ahead.flowSeconds == outer.flowSeconds);
        CHECK(ahead.flowSeconds <= galaxyFirst.flowSeconds);
        if (lead < 1e-3)
            CHECK(galaxyFirst.flowSeconds - ahead.flowSeconds < 0.01); // no jump into the chapter
    }
}

// --- (e) ---------------------------------------------------------------------------------------------------

void testFlash() {
    // FINALE: no flash on its flips, whatever eraCut says.
    for (double t = show::chapterStart(kFinale); t < show::chapterStart(kFinale + 1); t += kFrame)
        CHECK(show::eraFlash(sampleTimeline(t)) == 0.f);
    TimelineFrame forced = finaleAt(171.0);
    forced.eraCut = 1.f;
    CHECK(show::eraFlash(forced) == 0.f);

    // Portal chapters: none, even on the accent with an era cut forced (the era steps on the glass, with
    // no eraCut of its own).
    for (const int chapter : {1, 4, 10, 20}) {
        const TimelineFrame start = sampleTimeline(show::chapterStart(chapter) + .01);
        CHECK(start.transition == show::Transition::Portal);
        const double accent = show::transitionMarks(start).accent;
        TimelineFrame onAccent = sampleTimeline(accent + .01);
        CHECK(onAccent.eraCut == 0.f);
        CHECK(show::eraFlash(onAccent) == 0.f);
        onAccent.eraCut = 1.f;
        CHECK(show::eraFlash(onAccent) == 0.f);
    }

    // DOTS, GLENZ and RAYTRACE: kEraFlash on the flip at D, fading with eraCut.
    int beatSwaps = 0;
    for (int chapter = 0; chapter < show::chapterCount(); ++chapter) {
        const TimelineFrame start = sampleTimeline(show::chapterStart(chapter) + .01);
        if (start.transition != show::Transition::BeatSwap || chapter == kFinale)
            continue;
        ++beatSwaps;
        const double d = show::transitionMarks(start).downbeat;
        const TimelineFrame flip = sampleTimeline(d);
        CHECK(flip.eraCut == 1.f);
        CHECK(show::eraFlash(flip) == show::kEraFlash);
        const TimelineFrame after = sampleTimeline(d + .05);
        CHECK(std::abs(show::eraFlash(after) - show::kEraFlash * after.eraCut) < 1e-7f);
        CHECK(show::eraFlash(after) < show::kEraFlash);
    }
    CHECK(beatSwaps == 3);
    CHECK(show::kEraFlash == .25f);
}

} // namespace

int main() {
    try {
        testSchedule();
        testSources();
        testCutsAcrossTheShow();
        testThankYouCard();
        testSceneFrames();
        testFlash();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("director_test: %d checks passed\n", checksPassed);
    return 0;
}
