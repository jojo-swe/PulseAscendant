// The retro effect registry, effect by effect: everything an effect hands the retro stage (palette, text,
// background and scene) is a pure function of the chapter and the retro frame. The stage caches a retro
// frame and calls the builders and providers only when it re-renders, so two frames inside one retro frame
// must give byte-identical results however much their show time, local time and music signals differ. This
// one test covers every effect, present and future, instead of a purity check per effect. It also builds
// every retro frame of every chapter whose effect has a scene and checks it the way the rasteriser does
// (retro::checkScene), since a frame it refuses would end the show. CPU only.

#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/cues.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::EffectInfo;
using evo::show::Effect;
using evo::show::TimelineFrame;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

// Everything the stage takes from an effect for one retro frame.
struct Output {
    evo::retro::Palette palette{};
    std::vector<evo::text::Item> text;
    int background = -1; // -1: no provider
    evo::retro::RetroScene scene;
};

Output outputOf(const EffectInfo& info, const TimelineFrame& frame, std::uint32_t retroFrame) {
    Output out;
    info.palette(out.palette, frame, retroFrame);
    if (info.text)
        out.text = info.text(frame, retroFrame);
    if (info.background)
        out.background = info.background(frame, retroFrame);
    if (info.scene)
        info.scene(out.scene, frame, retroFrame);
    return out;
}

bool sameItems(const std::vector<evo::text::Item>& a, const std::vector<evo::text::Item>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::memcmp(&a[i].x, &b[i].x, sizeof(float)) != 0 ||
            std::memcmp(&a[i].y, &b[i].y, sizeof(float)) != 0 ||
            std::memcmp(&a[i].scale, &b[i].scale, sizeof(float)) != 0 || a[i].rgba != b[i].rgba ||
            a[i].text != b[i].text)
            return false;
    }
    return true;
}

// Byte by byte: what the GPU would read, padding included (the scene functions zero it).
template <typename T> bool sameBytes(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

bool sameDoubles(const std::vector<double>& a, const std::vector<double>& b) {
    return a.size() == b.size() &&
           (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0);
}

int chapterOf(Effect effect) {
    for (int chapter = 0; chapter < evo::show::chapterCount(); ++chapter)
        if (evo::show::cues[static_cast<std::size_t>(chapter)].effect == effect)
            return chapter;
    throw std::runtime_error("no chapter shows effect " + std::to_string(static_cast<int>(effect)));
}

// For every effect, at retro frames from the start to the end of its chapter: an early and a late moment of
// the retro frame from the timeline (different show time, local time, music and beat), the late one also
// with its music signals pushed to their extremes, give the same output.
void testEveryEffectIsPure() {
    std::size_t frames = 0, scenes = 0, texts = 0;
    for (int e = 0; e <= static_cast<int>(Effect::TheEnd); ++e) {
        const auto effect = static_cast<Effect>(e);
        const EffectInfo& info = evo::retro::effectFor(effect);
        CHECK(info.palette != nullptr);
        CHECK(info.retroRate > 0);

        const int chapter = chapterOf(effect);
        const double start = evo::show::chapterStart(chapter);
        const double duration = evo::show::sampleTimeline(start).chapterDuration;
        const auto lastFrame = static_cast<std::uint32_t>(std::floor(duration * info.retroRate)) - 1;
        for (std::uint32_t retroFrame :
             {0u, 1u, 7u, lastFrame / 3, lastFrame / 2, lastFrame - 1, lastFrame}) {
            const double early = start + (retroFrame + 0.02) / info.retroRate;
            const double late = start + (retroFrame + 0.97) / info.retroRate;
            const TimelineFrame a = evo::show::sampleTimeline(early);
            TimelineFrame b = evo::show::sampleTimeline(late);
            CHECK(a.chapter == chapter && b.chapter == chapter && a.effect == effect && b.effect == effect);
            CHECK(static_cast<std::uint32_t>(std::floor(a.localSeconds * info.retroRate)) == retroFrame);
            CHECK(static_cast<std::uint32_t>(std::floor(b.localSeconds * info.retroRate)) == retroFrame);
            CHECK(a.localSeconds != b.localSeconds && a.showSeconds != b.showSeconds);

            const Output reference = outputOf(info, a, retroFrame);
            TimelineFrame extreme = b;
            extreme.kick = a.kick > .5f ? 0.f : 1.f;
            extreme.energy = a.energy > .5f ? 0.f : 1.f;
            extreme.beatPhase = a.beatPhase > .5f ? 0.f : .99f;
            extreme.showSeconds += 100;
            for (const TimelineFrame& other : {b, extreme}) {
                const Output out = outputOf(info, other, retroFrame);
                CHECK(out.palette == reference.palette);
                CHECK(sameItems(out.text, reference.text));
                CHECK(out.background == reference.background);
                CHECK(sameBytes(out.scene.triangles, reference.scene.triangles));
                CHECK(sameBytes(out.scene.points, reference.scene.points));
                CHECK(sameDoubles(out.scene.triangleDepth, reference.scene.triangleDepth));
                CHECK(sameDoubles(out.scene.pointDepth, reference.scene.pointDepth));
            }
            // A provider runs on a scene the stage cleared, and leaves it within the rasterisers' limits.
            CHECK(reference.scene.triangles.size() <= evo::retro::kMaxTriangles);
            CHECK(reference.scene.points.size() <= evo::retro::kMaxPoints);
            ++frames;
            scenes += !reference.scene.triangles.empty() || !reference.scene.points.empty();
            texts += !reference.text.empty();
        }
    }
    std::printf("retro_registry: %d effects pure over %zu retro frames (%zu with a scene, %zu with text)\n",
                static_cast<int>(Effect::TheEnd) + 1, frames, scenes, texts);
}

// Every retro frame of every chapter whose effect has a scene provider passes retro::checkScene: within
// kMaxTriangles and kMaxPoints and inside the guard band, as SceneRaster::draw requires (it throws
// otherwise, which would end the show). Clipping can split one triangle into five, so the counts depend on
// the frame and none can be skipped.
void testEverySceneFrameFits() {
    std::size_t chapters = 0, frames = 0, maxTriangles = 0, maxPoints = 0;
    for (int chapter = 0; chapter < evo::show::chapterCount(); ++chapter) {
        const EffectInfo& info =
            evo::retro::effectFor(evo::show::cues[static_cast<std::size_t>(chapter)].effect);
        if (info.scene == nullptr)
            continue;
        ++chapters;
        const double start = evo::show::chapterStart(chapter);
        const double duration = evo::show::sampleTimeline(start).chapterDuration;
        const auto count = static_cast<std::uint32_t>(std::ceil(duration * info.retroRate));
        evo::retro::RetroScene scene;
        for (std::uint32_t retroFrame = 0; retroFrame < count; ++retroFrame) {
            // The middle of the retro frame, or just before the chapter's end for the last, partial one.
            const double t = std::min(start + (retroFrame + 0.5) / info.retroRate, start + duration - 1e-6);
            const TimelineFrame frame = evo::show::sampleTimeline(t);
            CHECK(frame.chapter == chapter);
            scene.clear();
            info.scene(scene, frame, retroFrame);
            try {
                evo::retro::checkScene(scene);
            } catch (const std::exception& error) {
                throw std::runtime_error("chapter " + std::to_string(chapter) + " retro frame " +
                                         std::to_string(retroFrame) + ": " + error.what());
            }
            ++checksPassed;
            ++frames;
            maxTriangles = std::max(maxTriangles, scene.triangles.size());
            maxPoints = std::max(maxPoints, scene.points.size());
        }
    }
    std::printf("retro_registry: every retro frame of %zu scene chapters fits the rasteriser (%zu frames, at "
                "most %zu triangles and %zu points)\n",
                chapters, frames, maxTriangles, maxPoints);
}

} // namespace

int main() {
    try {
        testEveryEffectIsPure();
        testEverySceneFrameFits();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("retro_registry_test: %d checks passed\n", checksPassed);
    return 0;
}
