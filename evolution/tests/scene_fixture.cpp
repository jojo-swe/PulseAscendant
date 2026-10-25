#include "scene_fixture.hpp"

#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "modern/scene_gpu.hpp"
#include "post/look.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace evo::test {

namespace {

using modern::Vec3;

constexpr double kFrameSeconds = 1.0 / 60.0;
constexpr float kMaxCoordinate = 1e4f;

std::string describe(modern::SceneId scene, int chapter, double seconds) {
    char text[160];
    std::snprintf(text, sizeof text, "%s, chapter %d (%s) at %.4f s", modern::sceneName(scene), chapter,
                  show::sampleTimeline(show::chapterStart(chapter)).title, seconds);
    return text;
}

[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw std::runtime_error(where + ": " + what);
}

// A frame forced into 2026, as EVO_FORCE_ERA=1 makes it (app/capture.cpp's applyForcedEra).
show::TimelineFrame forced2026(double seconds) {
    show::TimelineFrame frame = show::sampleTimeline(seconds);
    frame.era = 1.f;
    frame.wipe = 1.f;
    frame.eraCut = 0.f;
    return frame;
}

// One Export renderer at the options' size and detail, with the given scene map, played frame by frame.
class Player {
public:
    Player(gfx::Context& ctx, const PictureOptions& options, modern::SceneMap map)
        : readback_(ctx, options.width, options.height), ring_(ctx),
          renderer_(ctx, ring_, settings(options, std::move(map))) {}

    std::vector<std::uint8_t> render(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return readback_.rgb();
    }

private:
    static render::RenderSettings settings(const PictureOptions& options, modern::SceneMap map) {
        render::RenderSettings s{options.width, options.height};
        s.hud = false;
        s.mode = render::RenderMode::Export;
        s.spp = options.spp;
        s.detail = options.detail;
        s.sceneFactory = options.factory;
        s.sceneMap = std::move(map);
        return s;
    }

    gfx::Readback readback_;
    gfx::FrameRing ring_;
    render::Renderer renderer_; // last: its destructor waits for the ring
};

struct PictureStats {
    double meanLuminance = 0, clipped = 0, black = 0;
};

PictureStats measure(const std::vector<std::uint8_t>& rgb) {
    PictureStats s;
    const std::size_t pixels = rgb.size() / 3;
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t r = rgb[3 * i], g = rgb[3 * i + 1], b = rgb[3 * i + 2];
        s.meanLuminance += (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0;
        s.clipped += r == 255 || g == 255 || b == 255;
        s.black += r == 0 && g == 0 && b == 0;
    }
    s.meanLuminance /= double(pixels);
    s.clipped /= double(pixels);
    s.black /= double(pixels);
    return s;
}

double changedFraction(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    std::size_t changed = 0;
    const std::size_t pixels = a.size() / 3;
    for (std::size_t i = 0; i < pixels; ++i)
        changed += a[3 * i] != b[3 * i] || a[3 * i + 1] != b[3 * i + 1] || a[3 * i + 2] != b[3 * i + 2];
    return double(changed) / double(pixels);
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool within(Vec3 v) {
    return std::abs(v.x) < kMaxCoordinate && std::abs(v.y) < kMaxCoordinate && std::abs(v.z) < kMaxCoordinate;
}

bool sameCamera(const modern::Camera& a, const modern::Camera& b) {
    const auto same = [](Vec3 x, Vec3 y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
    return same(a.eye, b.eye) && same(a.target, b.target) && same(a.up, b.up) &&
           a.fovYDegrees == b.fovYDegrees && a.aperture == b.aperture && a.focusDistance == b.focusDistance &&
           a.shot == b.shot;
}

} // namespace

std::vector<int> chaptersOf(modern::SceneId scene) {
    std::vector<int> chapters;
    for (int c = 0; c < show::chapterCount(); ++c)
        if (modern::sceneFor(show::sampleTimeline(show::chapterStart(c)).effect) == scene)
            chapters.push_back(c);
    return chapters;
}

void checkChapterPictures(gfx::Context& ctx, modern::SceneId scene, const PictureOptions& options) {
    const std::vector<int> chapters = chaptersOf(scene);
    if (chapters.empty())
        throw std::runtime_error(std::string("checkChapterPictures: no chapter shows ") +
                                 modern::sceneName(scene));
    Player player(ctx, options, {});
    Player placeholder(ctx, options, [](show::Effect) { return modern::SceneId::Placeholder; });
    for (const int chapter : chapters) {
        const double start = show::chapterStart(chapter);
        const show::TimelineFrame head = show::sampleTimeline(start);
        const double accent = start + head.accentSeconds;
        std::vector<std::uint8_t> accentPicture;
        for (const double seconds :
             {start + 0.2 * head.chapterDuration, accent, start + 0.8 * head.chapterDuration}) {
            const std::string where = describe(scene, chapter, seconds);
            const show::TimelineFrame frame = forced2026(seconds);
            const std::vector<std::uint8_t> rgb = player.render(frame);
            const PictureStats s = measure(rgb);
            const double changed = changedFraction(rgb, placeholder.render(frame));
            std::printf("checkChapterPictures: %s: mean %.3f, clipped %.3f, black %.3f, changed %.3f\n",
                        where.c_str(), s.meanLuminance, s.clipped, s.black, changed);
            if (!(s.meanLuminance >= kMinMeanLuminance && s.meanLuminance <= kMaxMeanLuminance))
                fail(where, "mean display luminance " + std::to_string(s.meanLuminance) + " outside [" +
                                std::to_string(kMinMeanLuminance) + ", " + std::to_string(kMaxMeanLuminance) +
                                "]");
            if (!(s.clipped < kMaxClippedFraction))
                fail(where, std::to_string(s.clipped) + " of the pixels clip at 255");
            // Just after a Portal chapter's glass the grade maps the picture onto the 1993 palette, whose
            // dark entries are black (post::paletteWeight): the black limit holds once that has faded.
            if (!(s.black < kMaxBlackFraction) && post::paletteWeight(frame) < .5f)
                fail(where, std::to_string(s.black) + " of the pixels are pure black");
            if (!(changed > kMinChangedFraction))
                fail(where, "only " + std::to_string(changed) + " of the pixels differ from the placeholder");
            if (seconds == accent)
                accentPicture = rgb;
        }
        if (player.render(forced2026(accent)) != accentPicture)
            fail(describe(scene, chapter, accent), "the accent frame rendered again differs");
    }
}

SweepStats checkSceneSweep(modern::SceneId scene, const SweepLimits& limits) {
    const std::vector<int> chapters = chaptersOf(scene);
    if (chapters.empty())
        throw std::runtime_error(std::string("checkSceneSweep: no chapter shows ") +
                                 modern::sceneName(scene));
    const std::unique_ptr<modern::ModernScene> object =
        limits.factory ? limits.factory(scene, limits.detail) : modern::makeScene(scene, limits.detail);
    if (!object)
        throw std::runtime_error(std::string("checkSceneSweep: no scene object for ") +
                                 modern::sceneName(scene));
    const modern::SceneStatic& statics = object->staticScene();
    SweepStats stats;
    for (const int chapter : chapters) {
        const double start = show::chapterStart(chapter), end = show::chapterStart(chapter + 1);
        const auto frames = static_cast<long>(std::ceil((end - start) / kFrameSeconds));
        std::optional<modern::Camera> previous;
        for (long i = 0; i < frames; ++i) {
            const double seconds = std::min(start + double(i) * kFrameSeconds, end - 1e-6);
            const std::string where = describe(scene, chapter, seconds);
            const show::TimelineFrame timeline = show::sampleTimeline(seconds);
            if (timeline.chapter != chapter)
                fail(where, "the frame belongs to chapter " + std::to_string(timeline.chapter));
            const modern::SceneFrame frame = object->frame(timeline);
            const modern::Camera& camera = frame.camera;
            if (!sameCamera(camera, object->camera(timeline)))
                fail(where, "camera() differs from frame().camera");
            if (!finite(camera.eye) || !finite(camera.target) || !finite(camera.up) ||
                !(camera.fovYDegrees > 0.f && camera.fovYDegrees < 180.f) || !(camera.aperture >= 0.f) ||
                !(camera.focusDistance > 0.f))
                fail(where, "the camera is not finite (or its fov, aperture or focus is out of range)");
            if (!within(camera.eye) || !within(camera.target))
                fail(where, "the camera lies beyond |coordinate| < 10^4");
            // A NaN exposure makes the tonemap NaN; clampIndirect limits every contribution after a rough
            // vertex, so it must be a positive number too.
            if (!std::isfinite(frame.exposureEV))
                fail(where, "the exposure (" + std::to_string(frame.exposureEV) + " EV) is not finite");
            if (!(std::isfinite(frame.clampIndirect) && frame.clampIndirect > 0.f))
                fail(where, "clampIndirect (" + std::to_string(frame.clampIndirect) +
                                ") is not a positive finite number");
            if (previous) {
                if (camera.shot != previous->shot && camera.shot != previous->shot + 1)
                    fail(where, "the shot goes from " + std::to_string(previous->shot) + " to " +
                                    std::to_string(camera.shot));
                if (camera.shot == previous->shot) {
                    const float step = std::max(modern::length(camera.eye - previous->eye),
                                                modern::length(camera.target - previous->target));
                    stats.maxCameraStep = std::max(stats.maxCameraStep, step);
                    if (!(step <= limits.maxCameraStep))
                        fail(where, "the camera moves " + std::to_string(step) + " in one frame, more than " +
                                        std::to_string(limits.maxCameraStep));
                } else {
                    ++stats.shots;
                }
            } else {
                if (camera.shot != 0)
                    fail(where, "the chapter starts on shot " + std::to_string(camera.shot));
                ++stats.shots;
            }
            for (std::size_t k = 0; k < frame.instances.size(); ++k) {
                const rt::Mat34& m = frame.instances[k].transform;
                for (const auto& row : m.m)
                    for (const float v : row)
                        if (!std::isfinite(v))
                            fail(where, "instance " + std::to_string(k) + " has a non-finite transform");
                if (!(modern::linearDeterminant(m) > 0.f))
                    fail(where, "instance " + std::to_string(k) + "'s transform mirrors or collapses");
                if (!within({m.m[0][3], m.m[1][3], m.m[2][3]}))
                    fail(where, "instance " + std::to_string(k) + " lies beyond |coordinate| < 10^4");
            }
            try {
                modern::checkSceneFrame(statics, frame);
            } catch (const std::exception& error) {
                fail(where, error.what());
            }
            stats.maxInstances = std::max(stats.maxInstances, std::uint32_t(frame.instances.size()));
            stats.maxLights = std::max(stats.maxLights, std::uint32_t(frame.lights.size()));
            stats.maxMaterials =
                std::max(stats.maxMaterials, std::uint32_t(frame.materials.empty() ? statics.materials.size()
                                                                                   : frame.materials.size()));
            ++stats.frames;
            previous = camera;
        }
    }
    std::printf(
        "checkSceneSweep: %s: %u frames in %zu chapters, %u shots, at most %u instances, %u lights, %u "
        "materials, camera step %.4f\n",
        modern::sceneName(scene), stats.frames, chapters.size(), stats.shots, stats.maxInstances,
        stats.maxLights, stats.maxMaterials, stats.maxCameraStep);
    return stats;
}

} // namespace evo::test
