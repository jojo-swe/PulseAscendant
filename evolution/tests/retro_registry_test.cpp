// The retro effect registry, effect by effect: everything an effect hands the retro stage (palette, text,
// background, scene and grid layers) is a pure function of the chapter and the retro frame. The stage caches
// a retro frame and calls the builders and providers only when it re-renders, so two frames inside one retro
// frame must give byte-identical results however much their show time, local time and music signals differ.
// This one test covers every effect, present and future, instead of a purity check per effect. It also builds
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
#include <utility>
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

// What the stage takes from one entry for one picture.
struct Part {
    int effect = -1;
    std::uint32_t retroFrame{};
    evo::retro::Palette palette{};
    int background = -1; // -1: no provider
    evo::retro::RetroScene scene;
    evo::retro::PixelRect from, to;
    float gain = 1;
    bool masked = false;
};

// Everything the stage takes from an effect for one retro frame: its own picture's parts and text, and for
// a grid each layer's source parts and rects.
struct Output {
    Part own;
    std::vector<evo::text::Item> text;
    std::vector<Part> layers;
};

Part partOf(Effect effect, const TimelineFrame& frame, std::uint32_t retroFrame) {
    const EffectInfo& entry = evo::retro::effectFor(effect);
    Part part;
    part.effect = static_cast<int>(effect);
    part.retroFrame = retroFrame;
    entry.palette(part.palette, frame, retroFrame);
    if (entry.background)
        part.background = entry.background(frame, retroFrame);
    if (entry.scene)
        entry.scene(part.scene, frame, retroFrame);
    return part;
}

Output outputOf(const EffectInfo& info, const TimelineFrame& frame, std::uint32_t retroFrame) {
    Output out;
    out.own = partOf(frame.effect, frame, retroFrame);
    if (info.text)
        out.text = info.text(frame, retroFrame);
    if (info.grid)
        for (const evo::retro::GridLayer& layer : info.grid(frame, retroFrame)) {
            // The stage's recursion guard: a layer's source never composes a grid of its own.
            CHECK(layer.source.effect == frame.effect ||
                  evo::retro::effectFor(layer.source.effect).grid == nullptr);
            Part part = partOf(layer.source.effect, layer.source.frame, layer.source.retroFrame);
            part.from = layer.from;
            part.to = layer.to;
            part.gain = layer.gain;
            part.masked = layer.masked;
            out.layers.push_back(std::move(part));
        }
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
    for (std::size_t e = 0; e < evo::show::kEffectCount; ++e) {
        const auto effect = static_cast<Effect>(e);
        const EffectInfo& info = evo::retro::effectFor(effect);
        CHECK(info.palette != nullptr);
        CHECK(info.retroRate > 0);
        // A texture is built once, so it must be the same every time, width x height texels, for a shader.
        if (info.texture) {
            const evo::retro::EffectTexture texture = info.texture();
            CHECK(info.shader != nullptr && texture.width > 0 && texture.height > 0);
            CHECK(texture.texels.size() == std::size_t{texture.width} * texture.height);
            CHECK(info.texture().texels == texture.texels);
        }

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
                CHECK(sameItems(out.text, reference.text));
                CHECK(out.layers.size() == reference.layers.size());
                for (std::size_t l = 0; l <= out.layers.size(); ++l) {
                    const Part& p = l < out.layers.size() ? out.layers[l] : out.own;
                    const Part& q = l < out.layers.size() ? reference.layers[l] : reference.own;
                    CHECK(p.effect == q.effect && p.retroFrame == q.retroFrame);
                    CHECK(p.palette == q.palette && p.background == q.background);
                    CHECK(p.from == q.from && p.to == q.to && p.gain == q.gain && p.masked == q.masked);
                    CHECK(sameBytes(p.scene.triangles, q.scene.triangles));
                    CHECK(sameBytes(p.scene.points, q.scene.points));
                    CHECK(sameDoubles(p.scene.triangleDepth, q.scene.triangleDepth));
                    CHECK(sameDoubles(p.scene.pointDepth, q.scene.pointDepth));
                }
            }
            // A provider runs on a scene the stage cleared, and leaves it within the rasterisers' limits.
            CHECK(reference.own.scene.triangles.size() <= evo::retro::kMaxTriangles);
            CHECK(reference.own.scene.points.size() <= evo::retro::kMaxPoints);
            ++frames;
            scenes += !reference.own.scene.triangles.empty() || !reference.own.scene.points.empty();
            texts += !reference.text.empty();
        }
    }
    std::printf("retro_registry: %zu effects pure over %zu retro frames (%zu with a scene, %zu with text)\n",
                evo::show::kEffectCount, frames, scenes, texts);
}

// Every retro frame of every chapter whose effect has a scene provider (or a grid of effects that do) passes
// retro::checkScene: within kMaxTriangles and kMaxPoints and inside the guard band, as SceneRaster::draw
// requires (it throws otherwise, which would end the show). Clipping can split one triangle into five, so the
// counts depend on the frame and none can be skipped.
void testEverySceneFrameFits() {
    std::size_t chapters = 0, frames = 0, maxTriangles = 0, maxPoints = 0;
    for (int chapter = 0; chapter < evo::show::chapterCount(); ++chapter) {
        const EffectInfo& info =
            evo::retro::effectFor(evo::show::cues[static_cast<std::size_t>(chapter)].effect);
        if (info.scene == nullptr && info.grid == nullptr)
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
            // Its own scene, and for a grid every layer source's scene.
            std::vector<std::pair<Effect, std::pair<TimelineFrame, std::uint32_t>>> sources = {
                {frame.effect, {frame, retroFrame}}};
            if (info.grid)
                for (const evo::retro::GridLayer& layer : info.grid(frame, retroFrame))
                    if (layer.source.effect != frame.effect)
                        sources.push_back(
                            {layer.source.effect, {layer.source.frame, layer.source.retroFrame}});
            for (const auto& [effect, at] : sources) {
                const EffectInfo& entry = evo::retro::effectFor(effect);
                scene.clear();
                if (entry.scene)
                    entry.scene(scene, at.first, at.second);
                try {
                    evo::retro::checkScene(scene);
                } catch (const std::exception& error) {
                    throw std::runtime_error("chapter " + std::to_string(chapter) + " retro frame " +
                                             std::to_string(retroFrame) + ": " + error.what());
                }
                maxTriangles = std::max(maxTriangles, scene.triangles.size());
                maxPoints = std::max(maxPoints, scene.points.size());
            }
            ++checksPassed;
            ++frames;
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
