#include "show/cues.hpp"
#include "show/generated/beat_data.hpp"
#include "show/timeline.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

using evo::show::Effect;
using evo::show::sampleTimeline;

bool unit(float value) {
    return std::isfinite(value) && value >= 0.f && value <= 1.f;
}

void checkChapters() {
    CHECK(std::abs(evo::show::showDuration() - 711.16) < 1e-9);
    CHECK(evo::show::chapterCount() == 31);
    CHECK(evo::show::chapterStart(0) == 0.0);
    CHECK(evo::show::chapterStart(11) == 194.0);
    CHECK(evo::show::chapterStart(20) == 412.4);
    for (int i = 1; i < evo::show::chapterCount(); ++i)
        CHECK(evo::show::chapterStart(i) > evo::show::chapterStart(i - 1));

    CHECK(sampleTimeline(0).effect == Effect::Boot);
    CHECK(sampleTimeline(evo::show::chapterStart(7) + .5).effect == Effect::Raytrace);

    const evo::show::TimelineFrame studio = sampleTimeline(194.0);
    CHECK(studio.effect == Effect::StudioNight);
    CHECK(studio.chapter == 11);
    CHECK(studio.track == 1);
    CHECK(studio.trackSeconds == 0.0);
    CHECK(sampleTimeline(193.99).track == 0);

    const evo::show::TimelineFrame reboot = sampleTimeline(412.4);
    CHECK(reboot.effect == Effect::Reboot);
    CHECK(reboot.chapter == 20);
    CHECK(reboot.track == 2);
    CHECK(reboot.trackSeconds == 0.0);
    CHECK(sampleTimeline(412.39).track == 1);

    CHECK(sampleTimeline(711.159).chapter == 30);
    CHECK(sampleTimeline(711.159).effect == Effect::TheEnd);
    CHECK(sampleTimeline(711.16).chapter == 0); // the end wraps to the start
    CHECK(sampleTimeline(-5).chapter == 0);
    CHECK(sampleTimeline(std::nan("")).chapter == 0);
    CHECK(std::abs(evo::show::wrapShowTime(711.16 + 10) - 10) < 1e-9);

    // No chapter straddles two tracks.
    for (int i = 0; i + 1 < evo::show::chapterCount(); ++i)
        CHECK(sampleTimeline(evo::show::chapterStart(i)).track ==
              sampleTimeline(evo::show::chapterStart(i + 1) - 1e-6).track);

    // Accents sit in the middle of their chapter, where the parent looked for them.
    for (int i = 0; i < evo::show::chapterCount(); ++i) {
        const evo::show::TimelineFrame frame = sampleTimeline(evo::show::chapterStart(i) + .01);
        CHECK(frame.chapter == i);
        CHECK(frame.effect == evo::show::cues[i].effect);
        CHECK(frame.accentSeconds >= .28 * frame.chapterDuration);
        CHECK(frame.accentSeconds <= .76 * frame.chapterDuration);
    }
}

void checkMusic() {
    for (double t = 0; t < evo::show::showDuration(); t += .25) {
        const evo::show::TimelineFrame frame = sampleTimeline(t);
        for (float band : frame.spectrum)
            CHECK(unit(band));
        CHECK(unit(frame.energy) && unit(frame.bass) && unit(frame.hit));
        CHECK(unit(frame.kick) && unit(frame.snare) && unit(frame.treble) && unit(frame.lowOnsetPhase));
        CHECK(unit(frame.drive) && unit(frame.cameraDrive) && unit(frame.chapterProgress));
        CHECK(unit(frame.barPhase));
    }
}

void checkBeats() {
    // Within one beat the phase only rises; it stays in [0, 1] everywhere.
    evo::show::TimelineFrame previous = sampleTimeline(0);
    CHECK(previous.beatPhase == 0.f); // before the first tracked beat
    for (int ms = 1; ms < 711160; ++ms) {
        const evo::show::TimelineFrame frame = sampleTimeline(ms / 1000.0);
        CHECK(unit(frame.beatPhase));
        CHECK(frame.beatSeconds >= .2f);
        if (frame.track == previous.track && frame.beatIndex == previous.beatIndex)
            CHECK(frame.beatPhase >= previous.beatPhase);
        previous = frame;
    }

    const evo::show::BeatGridView grid = evo::show::beatGrid(1);
    CHECK(grid.count() == evo::show::generated::beatOffsets[2] - evo::show::generated::beatOffsets[1]);
    CHECK(grid.beatAt(-1) == -1);
    CHECK(grid.beatAt(grid.beatTime(5)) == 5);
    CHECK(grid.beatAt(grid.beatTime(5) - .001) == 4);
    CHECK(grid.bar(5) == evo::show::generated::beatBar[evo::show::generated::beatOffsets[1] + 5]);
}

void checkFlow() {
    double previous = sampleTimeline(0).flowSeconds;
    for (int step = 1; step < 71116; ++step) {
        const double flow = sampleTimeline(step / 100.0).flowSeconds;
        CHECK(std::isfinite(flow));
        CHECK(flow >= previous);
        previous = flow;
    }
}

void checkShowInfo() {
    const std::string json = evo::show::showInfoJson();
    CHECK(json.find("\"duration\":711.16") != std::string::npos);
    CHECK(json.find("\"chapters\":[") != std::string::npos);
    CHECK(json.find("\"title\":\"RAYTRACE\"") != std::string::npos);
    CHECK(json.find("\"effect\":\"StudioNight\"") != std::string::npos);
    CHECK(json.find("\"effect\":\"TheEnd\"") != std::string::npos);
    // The eras a chapter shows: a Cut holds its eraFixed, the other transitions show both.
    CHECK(json.find("\"title\":\"BOOT\",\"effect\":\"Boot\",\"transition\":\"Cut\",\"eras\":[1993]") !=
          std::string::npos);
    CHECK(json.find(
              "\"title\":\"DOTS\",\"effect\":\"Dots\",\"transition\":\"BeatSwap\",\"eras\":[1993,2026]") !=
          std::string::npos);
    CHECK(json.find("\"title\":\"NEBULA\",\"effect\":\"Nebula\",\"transition\":\"Cut\",\"eras\":[2026]") !=
          std::string::npos); // No scene names given: no "scene" field. Given (main.cpp passes
                              // modern::sceneFor's), every chapter
    // ends with its scene, last.
    CHECK(json.find("\"scene\"") == std::string::npos);
    const std::string named = evo::show::showInfoJson([](evo::show::Effect effect) -> const char* {
        return effect == evo::show::Effect::StudioNight ? "Studio" : "Placeholder";
    });
    CHECK(named.find("\"accent\":209.8,\"scene\":\"Studio\"}") != std::string::npos);
    CHECK(named.find("\"scene\":\"Placeholder\"}]}") != std::string::npos);
    std::size_t scenes = 0;
    for (std::size_t at = named.find("\"scene\":"); at != std::string::npos;
         at = named.find("\"scene\":", at + 1))
        ++scenes;
    CHECK(scenes == static_cast<std::size_t>(evo::show::chapterCount()));
}

} // namespace

int main() {
    try {
        checkChapters();
        checkMusic();
        checkBeats();
        checkFlow();
        checkShowInfo();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED after %d checks: %s\n", checksPassed, error.what());
        return 1;
    }
    std::printf("timeline: %d checks passed\n", checksPassed);
    return 0;
}
