// The Lens scene: LENS and LENS OF GOD on lavapipe, with the validation layer on; fails on
// any message.
//
// (c) the cards: the feat lines, the full-detail photon count the frames shoot, the 16,384 tesserae, the
//     lens's 200 units;
// (t) the mosaic is the 1993 picture: shaders/rt/tesserae.glsl, run in a compute probe, gives every texel of
//     the tile (and of two far repeats of it) retro::rotozoomTexture's index and rotozoomPalette's colour;
// (o) the optics on the CPU: a ball's paraxial focus is n R / (2 (n - 1)) from its centre, the colossal
//     lens's traced focus lies within 10 % of its thin-lens design, LENS's sun puts the ball's light on the
//     stone just past its best focus, kLensTrail tesserae behind its contact point; LENS OF GOD's lens is out
//     of focus until D and in focus from then on, its focus walks onto the foreground tessera between D and
//     the accent and stays there, and its photon map stays within the receiver capacity, finer as the spot
//     tightens;
// (r) 1993 and 2026 agree: until D LENS's camera looks straight down, turned by the rotozoom's angle, over
//     its centre, as many tesserae wide as the rotozoom shows texels; the ball's contact point and its focus
//     cross a joint on every beat (the accent among them); the shots change on bar lines;
// (f) the physical check: LENS with its sun at 45 degrees (export, spp 4, 160x90, looking down on the ball)
// shows a
//     spot on the stone under the ball at least 3x brighter than the shadow ring round it; with the photons
//     switched off the spot is gone (below 1.5x), so it is the caustic;
// (d) dispersion: seen from above, the caustic's blue light has spread at least 3 % wider than its red (blue
//     focused nearer the ball); with the ball's Abbe number 0 the two agree within 2 %;
// (g) LENS OF GOD on the accent: the focus on the stone is at least 8x brighter than the lens's shadow round
//     it;
// (p) checkChapterPictures for both chapters and (s) checkSceneSweep (scene_fixture.hpp).
//
// `scene_lens_test --look <dir> [spp] [detail]` renders 480x270 stills of both chapters at several moments
// through the renderer (Export, forced 2026, no HUD) instead; `scene_lens_test <letters>` runs those parts.

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/lens.hpp"
#include "render/renderer.hpp"
#include "retro/palette.hpp"
#include "retro/rotozoom.hpp"
#include "show/timeline.hpp"

#include "scene_fixture.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;
using modern::Camera;
using modern::SceneId;
using modern::Vec3;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

int chapterOf(show::Effect effect) {
    for (int c = 0; c < show::chapterCount(); ++c)
        if (show::sampleTimeline(show::chapterStart(c)).effect == effect)
            return c;
    throw std::runtime_error("no chapter shows the effect");
}

// The frame `local` seconds into the chapter showing `effect`.
show::TimelineFrame at(show::Effect effect, double local) {
    return show::sampleTimeline(show::chapterStart(chapterOf(effect)) + local);
}

show::TimelineFrame forced2026(show::TimelineFrame f) {
    f.era = 1.f;
    f.wipe = 1.f;
    f.eraCut = 0.f;
    return f;
}

// --- A scene that edits the Lens scene's frames (tests only) ----------------------------------------------

class EditedLens final : public modern::ModernScene {
public:
    using Edit = std::function<void(modern::SceneFrame&)>;
    EditedLens(modern::SceneDetail detail, Edit edit) : lens_(detail), edit_(std::move(edit)) {}
    const modern::SceneStatic& staticScene() const override { return lens_.staticScene(); }
    modern::SceneFrame frame(const show::TimelineFrame& f) const override {
        modern::SceneFrame frame = lens_.frame(f);
        edit_(frame);
        return frame;
    }
    Camera camera(const show::TimelineFrame& f) const override { return frame(f).camera; }
    bool samplesRetro() const override { return false; }

private:
    modern::Lens lens_;
    Edit edit_;
};

// --- Rendering and measuring
// --------------------------------------------------------------------------------

struct Picture {
    std::uint32_t width, height;
    std::vector<std::uint8_t> rgb;
    // Linear luminance (the sRGB code values decoded): ratios of it are ratios of light where the tonemap is
    // linear (below about 0.76 of display white).
    double luminance(int x, int y) const {
        const std::size_t i = 3 * (std::size_t(y) * width + std::size_t(x));
        const auto decode = [](std::uint8_t c) {
            const double v = c / 255.0;
            return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * decode(rgb[i]) + 0.7152 * decode(rgb[i + 1]) + 0.0722 * decode(rgb[i + 2]);
    }
};

// One Export renderer (160x90, spp 4, detail 1/16) for every picture the checks render, so the pipelines
// are made once: its Lens scene's frames go through whichever edit the next render() names.
class Shots {
public:
    static constexpr std::uint32_t kWidth = 160, kHeight = 90;

    explicit Shots(gfx::Context& ctx)
        : ctx_(ctx), edit_(std::make_shared<EditedLens::Edit>()), readback_(ctx, kWidth, kHeight), ring_(ctx),
          renderer_(ctx, ring_, settings(edit_)) {}

    Picture render(const show::TimelineFrame& frame, EditedLens::Edit edit = {}) {
        *edit_ = std::move(edit);
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, forced2026(frame));
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        Picture picture{kWidth, kHeight, readback_.rgb()};
        idle(ctx_);
        return picture;
    }

private:
    static render::RenderSettings settings(std::shared_ptr<EditedLens::Edit> edit) {
        render::RenderSettings s{kWidth, kHeight};
        s.hud = false;
        s.mode = render::RenderMode::Export;
        s.spp = 4;
        s.detail = {1.f / 16};
        s.sceneFactory = [edit](SceneId id,
                                modern::SceneDetail detail) -> std::unique_ptr<modern::ModernScene> {
            if (id != SceneId::Lens)
                return modern::makeScene(id, detail);
            return std::make_unique<EditedLens>(detail, [edit](modern::SceneFrame& f) {
                if (*edit)
                    (*edit)(f);
            });
        };
        return s;
    }

    gfx::Context& ctx_;
    std::shared_ptr<EditedLens::Edit> edit_;
    gfx::Readback readback_;
    gfx::FrameRing ring_;
    render::Renderer renderer_; // last: its destructor waits for the ring
};

// Where a world point lands in the picture (pixel centres at whole numbers), and how many pixels one unit
// across the view spans at its depth.
struct Projected {
    double x, y, pixelsPerUnit;
};

Projected project(const Camera& camera, Vec3 point, std::uint32_t width, std::uint32_t height) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const Vec3 d = point - camera.eye;
    const double depth = modern::dot(d, b.forward);
    const double tanHalf = std::tan(0.5 * camera.fovYDegrees * 3.14159265358979 / 180.0);
    const double aspect = double(width) / double(height);
    const double ndcX = modern::dot(d, b.right) / (depth * tanHalf * aspect);
    const double ndcY = modern::dot(d, b.up) / (depth * tanHalf);
    return {(ndcX + 1.0) * 0.5 * width - 0.5, (1.0 - ndcY) * 0.5 * height - 0.5,
            0.5 * height / (depth * tanHalf)};
}

// The mean of the pixels within `radius` pixels of the focus's projection, over the mean of the pixels that
// see the ground (y = 0) between `inner` and `outer` units from the focus (and, given a band, within `band`
// of the focus across x: the same row of tesserae, the same stone).
double spotOverRing(const Picture& picture, const Camera& camera, Vec3 focus, double radius, double inner,
                    double outer, double band = 1e30) {
    const Projected p = project(camera, focus, picture.width, picture.height);
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const double tanHalf = std::tan(0.5 * camera.fovYDegrees * 3.14159265358979 / 180.0);
    const double aspect = double(picture.width) / double(picture.height);
    double peak = 0, ring = 0;
    int peakCount = 0, ringCount = 0;
    for (int y = 0; y < int(picture.height); ++y) {
        for (int x = 0; x < int(picture.width); ++x) {
            if (std::hypot(x - p.x, y - p.y) <= radius) {
                peak += picture.luminance(x, y);
                ++peakCount;
                continue;
            }
            const auto nx = float((2.0 * (x + 0.5) / picture.width - 1.0) * aspect * tanHalf);
            const auto ny = float((1.0 - 2.0 * (y + 0.5) / picture.height) * tanHalf);
            const Vec3 d = b.forward + b.right * nx + b.up * ny;
            if (!(d.y < 0.f))
                continue;
            const Vec3 ground = camera.eye + d * (-camera.eye.y / d.y);
            const double r = modern::length(Vec3{ground.x - focus.x, 0.f, ground.z - focus.z});
            if (r >= inner && r <= outer && std::abs(ground.z - focus.z) <= band) {
                ring += picture.luminance(x, y);
                ++ringCount;
            }
        }
    }
    if (ringCount == 0 || peakCount == 0)
        throw std::runtime_error("spotOverRing: the spot or the ring is outside the picture");
    ring /= ringCount;
    peak /= peakCount;
    std::printf("  spot at (%.1f, %.1f): spot mean %.3f, ring mean %.4f over %d pixels\n", p.x, p.y, peak,
                ring, ringCount);
    return peak / std::max(ring, 1e-4);
}

// --- (c) The cards
// --------------------------------------------------------------------------------------------

void testCards() {
    const std::string lens = modern::lensEpochCard(show::Effect::Lens);
    const std::string god = modern::lensEpochCard(show::Effect::LensOfGod);
    std::printf("(c) LENS: %s\n    LENS OF GOD: %s\n", lens.c_str(), god.c_str());
    CHECK(lens == "2026 · THE PIXELS BECAME STONE · GLASS SPHERE · CAUSTICS");
    CHECK(god == "2026 · LENS OF GOD · 200 UNIT LENS · SUN FOCUS · PLANETARY SCALE");
    CHECK(std::string(modern::lensEpochCard(show::Effect::Dots)).empty());
    CHECK(modern::kRotozoomTesserae == retro::kRotozoomSize * retro::kRotozoomSize);
    CHECK(modern::kRotozoomTesserae == 16384u);
    // The full-detail frames shoot kLensPhotons, 1,048,576.
    CHECK(modern::kLensPhotons == 1048576u && modern::kGodLensPhotons == 1048576u);
    const modern::Lens full;
    CHECK(full.photons() == modern::kLensPhotons);
    const modern::SceneFrame lensFrame = full.frame(at(show::Effect::Lens, 9.0));
    const modern::SceneFrame godFrame = full.frame(at(show::Effect::LensOfGod, 20.0));
    CHECK(lensFrame.caustics && lensFrame.caustics->photons == modern::kLensPhotons);
    CHECK(godFrame.caustics && godFrame.caustics->photons == modern::kGodLensPhotons);
    CHECK(modern::lensPhotonCount(modern::kLensPhotons, {1.f / 256}) == 4096u);
    CHECK(modern::lensPhotonCount(modern::kLensPhotons, {1e-6f}) == modern::kMinLensPhotons);
    CHECK(modern::lensPhotonCount(modern::kLensPhotons, {0.3f}) == 262144u);
    // The lens is 200 across.
    float rim = 0;
    for (const auto& v : full.staticScene().meshes[modern::Lens::kGodLensMesh].vertices)
        rim = std::max(rim, std::hypot(v.position[0], v.position[2]));
    CHECK(std::abs(2.f * rim - modern::kGodLensDiameter) < 1e-3f);
}

// --- (t) The mosaic is the 1993 picture
// ------------------------------------------------------------------------

void testTesserae(gfx::Context& ctx) {
    const retro::EffectTexture texture = retro::rotozoomTexture();
    retro::Palette palette{};
    retro::rotozoomPalette(palette, show::sampleTimeline(show::chapterStart(chapterOf(show::Effect::Lens))),
                           0);
    constexpr std::uint32_t kTexels = 128 * 128;
    struct Push {
        std::int32_t wrap;
    };
    static constexpr std::array<gfx::Binding, 1> kBindings{{{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}}};
    gfx::ComputePipeline probe(ctx,
                               std::filesystem::path(EVO_TEST_SPIRV_DIR) / "test_tesserae_probe.comp.spv",
                               kBindings, sizeof(Push));
    int checked = 0;
    for (const std::int32_t wrap : {0, -3, 41}) {
        gfx::Buffer out(ctx, {sizeof(std::uint32_t) * 4 * kTexels, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                              "tesserae probe results"});
        const std::array<VkDescriptorBufferInfo, 1> infos{{{out.handle(), 0, VK_WHOLE_SIZE}}};
        {
            gfx::FrameRing ring(ctx);
            VkCommandBuffer cmd = ring.begin();
            probe.bind(cmd, ring, {}, infos);
            const Push push{wrap};
            probe.push(cmd, &push, sizeof push);
            probe.dispatch(cmd, kTexels, 1, 64, 1);
            gfx::bufferBarrier2(cmd, out.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                VK_ACCESS_2_HOST_READ_BIT);
            ring.wait(ring.submit());
        }
        idle(ctx);
        out.invalidate();
        std::vector<std::array<std::uint32_t, 4>> results(kTexels);
        std::memcpy(results.data(), out.mapped(), sizeof(std::uint32_t) * 4 * kTexels);
        for (std::uint32_t i = 0; i < kTexels; ++i) {
            const std::uint32_t index = texture.texels[i];
            const std::uint32_t c = palette[index];
            CHECK(results[i][0] == index);
            CHECK(results[i][1] == (c & 0xFFu) && results[i][2] == (c >> 8 & 0xFFu) &&
                  results[i][3] == (c >> 16 & 0xFFu));
            ++checked;
        }
    }
    std::printf("(t) the mosaic's %d tesserae (three repeats of the tile) are the 1993 texture's indices and "
                "colours\n",
                checked);
}

// --- (o) The optics
// -------------------------------------------------------------------------------------------

void testOptics() {
    const double n = modern::kLensBallIor, r = modern::kLensBallRadius;
    const double paraxial = n * r / (2.0 * (n - 1.0));
    const double narrow = modern::bestFocusDistance(r, 2 * r, 0.02 * r, n);
    const double best = modern::bestFocusDistance(r, 2 * r, 0.6 * r, n);
    std::printf("(o) ball: paraxial focus %.4f, traced (2 %% aperture) %.4f, best (60 %%) %.4f\n", paraxial,
                narrow, best);
    CHECK(std::abs(narrow - paraxial) < 0.002 * paraxial);
    CHECK(best < narrow); // spherical aberration pulls a wider aperture's best focus in
    // LENS's sun: 45 degrees up, straight ahead of the ball; its light lands kLensTrail tesserae behind the
    // contact point, a little beyond the best focus.
    const Vec3 sun = modern::lensSunDirection();
    CHECK(std::abs(modern::length(sun) - 1.f) < 1e-5f && std::abs(sun.y - std::sqrt(0.5f)) < 1e-5f);
    const show::TimelineFrame lens = at(show::Effect::Lens, 9.0);
    const Vec3 ball = modern::lensBallCentre(lens), spot = modern::lensFocusPoint(lens);
    CHECK(std::abs(spot.y) < 1e-5f && std::abs(ball.y - modern::kLensBallRadius) < 1e-5f);
    CHECK(modern::length(ball - spot) > best && modern::length(ball - spot) < 1.4 * best);
    CHECK(std::abs(ball.x - spot.x - modern::kLensTrail * modern::kTessera) < 1e-4f);

    // The colossal lens.
    const double focus = modern::godLensFocusDistance();
    std::printf("    lens of god: curvature %.1f, thickness %.2f, focus %.2f (design %.0f)\n",
                modern::godLensCurvature(), modern::godLensThickness(), focus,
                double(modern::kGodLensFocalLength));
    CHECK(std::abs(focus - modern::kGodLensFocalLength) < 0.1 * modern::kGodLensFocalLength);
    const show::TimelineFrame god = at(show::Effect::LensOfGod, 0.0);
    const double accent = god.accentSeconds, settle = modern::lensDownbeatBefore(god, accent);
    std::printf("    lens of god: D %.3f s, accent %.3f s into the chapter\n", settle, accent);
    CHECK(settle > 0 && settle <= accent && accent - settle < 1.8);
    double previous = 1e30;
    for (double t = 0; t < god.chapterDuration; t += 0.05) {
        const show::TimelineFrame f = at(show::Effect::LensOfGod, t);
        const double defocus = modern::godLensDefocus(f), spotSize = modern::godSpotDiameter(f);
        const Vec3 walk = modern::godFocusPoint(f);
        if (t < settle - 0.05) {
            CHECK(defocus < 0.0 && std::abs(modern::length(walk) - modern::kGodFocusWalk) < 1e-3f);
            CHECK(spotSize <= previous + 1e-9); // closing
        } else if (t >= settle) {
            CHECK(defocus == 0.0);
        }
        if (t >= accent)
            CHECK(modern::length(walk) < 1e-4f);
        // The lens's centre lies on the sun's line from the focus point, at its focus plus the defocus.
        const Vec3 off = modern::godLensCentre(f) - walk;
        CHECK(std::abs(modern::length(off) - (focus + defocus)) < 1e-2);
        CHECK(modern::dot(modern::normalize(off), modern::godSunDirection()) > 0.99999f);
        previous = spotSize;
    }
    CHECK(modern::godSpotDiameter(at(show::Effect::LensOfGod, 0.0)) >=
          10 * modern::godSpotDiameter(at(show::Effect::LensOfGod, accent)));
    // The receiver follows the spot within the capacity, finer as it tightens.
    const modern::Lens scene{{1.f / 256}};
    std::uint32_t wide = 0, tight = 0;
    for (double t = 0; t < god.chapterDuration; t += 0.25) {
        const modern::SceneFrame f = scene.frame(at(show::Effect::LensOfGod, t));
        CHECK(f.caustics && f.caustics->receivers.size() == 1);
        const std::uint32_t res = f.caustics->receivers[0].resolution;
        CHECK(res >= 128 && res <= scene.staticScene().capacity.causticResolution);
        (t < 1.0 ? wide : tight) = res;
    }
    CHECK(tight > wide);
}

// --- (r) 1993 against 2026
// -----------------------------------------------------------------------------------

void testRotozoomMatch() {
    const show::TimelineFrame head = at(show::Effect::Lens, 0.0);
    const double settle = modern::lensDownbeatBefore(head, head.accentSeconds);
    std::printf("(r) LENS: D %.3f s, accent %.3f s into the chapter\n", settle, head.accentSeconds);
    CHECK(settle > 2.0 && settle <= head.accentSeconds);
    int checked = 0;
    for (double t = 0.05; t < settle; t += 0.1) {
        const Camera c = modern::lensCamera(at(show::Effect::Lens, t));
        const modern::CameraBasis b = modern::cameraBasis(c);
        CHECK(b.forward.y < -0.9999f); // straight down
        // The screen's right along the rotozoom's turned texture x axis, over its centre texel.
        const double a = modern::rotozoomAngle(t);
        CHECK(std::abs(b.right.x - std::cos(a)) < 1e-4 && std::abs(b.right.z - std::sin(a)) < 1e-4);
        const Vec3 centre = modern::rotozoomCentre(t);
        CHECK(std::abs(c.eye.x - centre.x) < 1e-3f && std::abs(c.eye.z - centre.z) < 1e-3f);
        CHECK(std::abs(centre.x / modern::kTessera - (64.0 + 23.0 * t)) < 1e-3);
        // As many tesserae across as 1993's 320 pixels show texels at its zoom.
        const double aspect = 16.0 / 9.0, tanHalf = std::tan(0.5 * c.fovYDegrees * 3.14159265358979 / 180.0);
        const double across = 2.0 * c.eye.y * tanHalf * aspect / modern::kTessera;
        CHECK(std::abs(across - 320.0 * modern::rotozoomZoom(t)) < 1e-3 * across);
        ++checked;
    }
    std::printf("    the plan view matches the rotozoom's turn, centre and zoom at %d times before D\n",
                checked);

    // On every beat the ball's contact point and its focus cross a joint between tesserae (x on a multiple of
    // a tessera); halfway between beats they are mid-tessera.
    const show::BeatGridView grid = show::beatGrid(head.track);
    const double start = head.trackSeconds - head.localSeconds;
    int beats = 0;
    bool accentOnBeat = false;
    for (int b = grid.beatAt(start) + 1; b < grid.count() && grid.beatTime(b) < start + head.chapterDuration;
         ++b) {
        const double local = grid.beatTime(b) - start;
        const show::TimelineFrame f = at(show::Effect::Lens, local);
        const float column = modern::lensBallCentre(f).x / modern::kTessera;
        const float focus = modern::lensFocusPoint(f).x / modern::kTessera;
        CHECK(std::abs(column - std::round(column)) < 2e-3f && std::abs(focus - std::round(focus)) < 2e-3f);
        const double half = 0.5 * (grid.beatTime(b) + grid.beatTime(b + 1)) - start;
        const float between = modern::lensBallCentre(at(show::Effect::Lens, half)).x / modern::kTessera;
        CHECK(std::abs(between - std::round(between)) > 0.45f);
        accentOnBeat = accentOnBeat || std::abs(local - head.accentSeconds) < 0.06; // the accent's own beat
        ++beats;
    }
    std::printf("    the ball crosses a joint on each of the chapter's %d beats%s\n", beats,
                accentOnBeat ? ", the accent's among them" : "");
    CHECK(beats > 30 && accentOnBeat);

    // Shot changes on bar lines.
    for (const show::Effect effect : {show::Effect::Lens, show::Effect::LensOfGod}) {
        const show::TimelineFrame f = at(effect, 0.0);
        std::uint32_t shot = 0;
        for (double t = 0; t < f.chapterDuration; t += 1.0 / 120) {
            const show::TimelineFrame now = at(effect, t);
            const Camera c = modern::lensCamera(now);
            if (c.shot == shot)
                continue;
            shot = c.shot;
            const show::BeatGridView g = show::beatGrid(now.track);
            const int beat = g.beatAt(now.trackSeconds);
            const double since = now.trackSeconds - g.beatTime(beat);
            std::printf("    %s: shot %u at %.3f s (bar position %d, %.4f s after its beat)\n", now.title,
                        shot, t, g.bar(beat), since);
            CHECK(g.bar(beat) == 0 && since < 1.0 / 120 + 1e-6);
        }
        CHECK(shot >= 1);
    }
}

// --- (f) The focus under the ball, and (d) its colours
// ---------------------------------------------------------

// A frame of LENS from D on (the ball rolling, the camera low) seen from `camera` instead.
EditedLens::Edit lookingFrom(Camera camera, std::function<void(modern::SceneFrame&)> more = {}) {
    return [camera, more](modern::SceneFrame& f) {
        f.camera = camera;
        if (more)
            more(f);
    };
}

void testFocusSpot(Shots& shots) {
    constexpr std::uint32_t kW = 160, kH = 90;
    const double local = 9.0;
    const show::TimelineFrame frame = at(show::Effect::Lens, local);
    const Vec3 spot = modern::lensFocusPoint(frame);
    // Down on the ball from 2.2 m, a little to the camera's side, so the stone round the focus shows.
    Camera above;
    above.eye = spot + Vec3{0.f, 2.2f, 0.45f};
    above.target = spot;
    above.up = {1.f, 0.f, 0.f};
    above.fovYDegrees = 30.f;
    std::printf("(f) the focus under the ball (export, spp 4, %ux%u)\n", kW, kH);
    // Three stops down, so that the spot stays where the tonemap is linear.
    const auto darker = [](modern::SceneFrame& f) { f.exposureEV -= 3.f; };
    const double lit = spotOverRing(shots.render(frame, lookingFrom(above, darker)), above, spot, 2.5, 0.17,
                                    0.3, 0.4 * modern::kTessera);
    std::printf("    with the photon map: %.1fx the shadow ring\n", lit);
    CHECK(lit >= 3.0);
    const double dark = spotOverRing(shots.render(frame, lookingFrom(above,
                                                                     [darker](modern::SceneFrame& f) {
                                                                         darker(f);
                                                                         f.caustics->photons = 0;
                                                                     })),
                                     above, spot, 2.5, 0.17, 0.3, 0.4 * modern::kTessera);
    std::printf("    without photons: %.2fx\n", dark);
    CHECK(dark < 1.5);
}

// The caustic alone: `lit` minus `dark` (the same frame without photons), per channel in linear light. Its
// red and blue RMS distances (pixels) from the centroid of its light.
std::pair<double, double> channelRadii(const Picture& lit, const Picture& dark) {
    const auto linear = [](const Picture& picture, int x, int y, int c) {
        const double v =
            picture.rgb[3 * (std::size_t(y) * picture.width + std::size_t(x)) + std::size_t(c)] / 255.0;
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    const auto caustic = [&](int x, int y, int c) {
        return std::max(linear(lit, x, y, c) - linear(dark, x, y, c), 0.0);
    };
    double cx = 0, cy = 0, total = 0;
    for (int y = 0; y < int(lit.height); ++y)
        for (int x = 0; x < int(lit.width); ++x) {
            const double w = caustic(x, y, 0) + caustic(x, y, 1) + caustic(x, y, 2);
            cx += w * x;
            cy += w * y;
            total += w;
        }
    cx /= std::max(total, 1e-9);
    cy /= std::max(total, 1e-9);
    double moment[3] = {0, 0, 0}, weight[3] = {0, 0, 0};
    for (int y = 0; y < int(lit.height); ++y)
        for (int x = 0; x < int(lit.width); ++x) {
            const double d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            for (int c = 0; c < 3; c += 2) {
                moment[c] += caustic(x, y, c) * d2;
                weight[c] += caustic(x, y, c);
            }
        }
    return {std::sqrt(moment[0] / std::max(weight[0], 1e-9)),
            std::sqrt(moment[2] / std::max(weight[2], 1e-9))};
}

void testFringes(Shots& shots) {
    constexpr std::uint32_t kW = 160, kH = 90;
    const show::TimelineFrame frame = at(show::Effect::Lens, 9.0);
    const Vec3 spot = modern::lensFocusPoint(frame);
    // Down on the spot from 1 m up, from its side away from the ball: the frame holds the whole spot, two
    // stops darker so the tonemap stays linear.
    Camera above;
    above.eye = spot + Vec3{-0.4f, 1.f, 0.f};
    above.target = spot;
    above.up = {1.f, 0.f, 0.f};
    above.fovYDegrees = 20.f;
    std::printf("(d) the spot's colours from above (export, spp 4, %ux%u)\n", kW, kH);
    const auto withAbbe = [&](float abbe, bool photons) {
        return lookingFrom(above, [abbe, photons](modern::SceneFrame& f) {
            f.exposureEV -= 2.f;
            if (!photons)
                f.caustics->photons = 0;
            if (abbe < 0.f)
                return;
            f.materials = modern::Lens{{1.f / 256}}.staticScene().materials;
            f.materials[modern::Lens::kBallGlass].abbe = abbe;
        });
    };
    const Picture dispersive = shots.render(frame, withAbbe(-1.f, true));
    const Picture plain = shots.render(frame, withAbbe(0.f, true));
    const Picture dark = shots.render(frame, withAbbe(-1.f, false));
    const Picture darkPlain = shots.render(frame, withAbbe(0.f, false));
    if (const char* dir = std::getenv("EVO_LENS_TEST_OUT")) {
        std::filesystem::create_directories(dir);
        image::writePng(std::filesystem::path(dir) / "spot_abbe22.png", int(kW), int(kH),
                        dispersive.rgb.data());
        image::writePng(std::filesystem::path(dir) / "spot_abbe0.png", int(kW), int(kH), plain.rgb.data());
    }
    // Past the focus, blue (which focused nearer the ball) has spread wider than red.
    const auto [red, blue] = channelRadii(dispersive, dark);
    const auto [red0, blue0] = channelRadii(plain, darkPlain);
    std::printf(
        "    Abbe %.0f: red RMS radius %.2f px, blue %.2f px (%.1f %% wider); Abbe 0: %.2f and %.2f px\n",
        double(modern::kLensBallAbbe), red, blue, 100.0 * (blue / red - 1.0), red0, blue0);
    CHECK(blue > 1.03 * red);
    CHECK(std::abs(blue0 / red0 - 1.0) < 0.02);
}

// --- (g) LENS OF GOD's focus on the accent
// ----------------------------------------------------------------------

void testGodFocus(Shots& shots) {
    constexpr std::uint32_t kW = 160, kH = 90;
    const show::TimelineFrame head = at(show::Effect::LensOfGod, 0.0);
    const show::TimelineFrame frame = at(show::Effect::LensOfGod, head.accentSeconds);
    const Camera camera = modern::lensCamera(frame);
    std::printf("(g) LENS OF GOD's focus on the accent (export, spp 4, %ux%u)\n", kW, kH);
    const double ratio =
        spotOverRing(shots.render(frame), camera, modern::godFocusPoint(frame), 1.6, 18.0, 40.0);
    std::printf("    %.1fx the lens's shadow round it\n", ratio);
    CHECK(ratio >= 8.0);
}

// --- Look
// -------------------------------------------------------------------------------------------------------

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp, float detail) {
    constexpr std::uint32_t kW = 480, kH = 270;
    render::RenderSettings settings{kW, kH};
    settings.hud = false;
    settings.mode = render::RenderMode::Export;
    settings.spp = spp;
    settings.detail = {detail};
    gfx::FrameRing ring(ctx);
    gfx::Readback readback(ctx, kW, kH);
    render::Renderer renderer(ctx, ring, settings);
    struct Shot {
        show::Effect effect;
        double local;
        const char* name;
    };
    const show::TimelineFrame lens = at(show::Effect::Lens, 0.0), god = at(show::Effect::LensOfGod, 0.0);
    std::vector<Shot> shots = {
        {show::Effect::Lens, 1.5, "lens_1_plan"},
        {show::Effect::Lens, lens.accentSeconds - 1.5, "lens_2_wipe"},
        {show::Effect::Lens, lens.accentSeconds + 0.05, "lens_3_accent"},
        {show::Effect::Lens, lens.accentSeconds + 4.0, "lens_4_track"},
        {show::Effect::Lens, lens.chapterDuration - 0.6, "lens_5_end"},
        {show::Effect::LensOfGod, 3.0, "god_1_orbit"},
        {show::Effect::LensOfGod, 11.0, "god_2_descent"},
        {show::Effect::LensOfGod, god.accentSeconds - 1.2, "god_3_rim"},
        {show::Effect::LensOfGod, god.accentSeconds - 0.3, "god_4_walk"},
        {show::Effect::LensOfGod, god.accentSeconds + 0.5, "god_5_ignition"},
        {show::Effect::LensOfGod, god.accentSeconds + 4.0, "god_6_ember"},
        {show::Effect::LensOfGod, god.chapterDuration - 1.0, "god_7_orbit"},
    };
    if (const char* only = std::getenv("EVO_LOOK_ONLY")) {
        std::vector<Shot> kept;
        for (const Shot& s : shots)
            if (std::string(s.name).find(only) != std::string::npos)
                kept.push_back(s);
        shots = kept;
    }
    if (const char* local = std::getenv("EVO_LOOK_AT")) // a development aid: "lens:3.2" or "god:12"
        shots = {{std::string(local).rfind("god", 0) == 0 ? show::Effect::LensOfGod : show::Effect::Lens,
                  std::stod(std::string(local).substr(std::string(local).find(':') + 1)), "at"}};
    std::vector<double> times;
    for (const Shot& s : shots)
        times.push_back(show::chapterStart(chapterOf(s.effect)) + s.local);
    renderer.prepare(times);
    std::filesystem::create_directories(dir);
    for (std::size_t i = 0; i < shots.size(); ++i) {
        const auto start = std::chrono::steady_clock::now();
        const show::TimelineFrame f = forced2026(show::sampleTimeline(times[i]));
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, f);
        readback.record(cmd, renderer.output());
        ring.wait(ring.submit());
        const std::vector<std::uint8_t> rgb = readback.rgb();
        double mean = 0, clipped = 0;
        for (std::size_t p = 0; p < rgb.size(); p += 3) {
            mean += (0.2126 * rgb[p] + 0.7152 * rgb[p + 1] + 0.0722 * rgb[p + 2]) / 255.0;
            clipped += rgb[p] == 255 || rgb[p + 1] == 255 || rgb[p + 2] == 255;
        }
        mean /= double(rgb.size() / 3);
        clipped /= double(rgb.size() / 3);
        const std::filesystem::path path = dir / (std::string(shots[i].name) + ".png");
        image::writePng(path, int(kW), int(kH), rgb.data());
        std::printf("look: %s (%.2f s into %s, mean %.3f, clipped %.3f) in %.1f s\n", path.string().c_str(),
                    shots[i].local, f.title, mean, clipped, secondsSince(start));
    }
    idle(ctx);
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
    try {
        auto ctx = gfx::Context::create({.validation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the results are checked\n");
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 16u,
                       argc >= 5 ? std::stof(argv[4]) : 1.f);
            return ctx.validationMessageCount() == 0 ? 0 : 1;
        }
        const std::string only = argc >= 2 ? argv[1] : "";
        const auto timed = [&](const char* name, auto&& run) {
            if (!only.empty() && only.find(name) == std::string::npos)
                return;
            const auto t0 = std::chrono::steady_clock::now();
            run();
            std::printf("  [%s %.2f s]\n", name, secondsSince(t0));
        };
        timed("c", [&] { testCards(); });
        timed("t", [&] { testTesserae(ctx); });
        timed("o", [&] { testOptics(); });
        timed("r", [&] { testRotozoomMatch(); });
        timed("s", [&] {
            test::SweepLimits limits;
            limits.maxCameraStep = 12.f; // LENS OF GOD's orbit, descent and pull-back cover kilometres
            const test::SweepStats stats = test::checkSceneSweep(SceneId::Lens, limits);
            CHECK(stats.frames > 2500 && stats.shots == 6);
        });
        std::optional<Shots> shots;
        timed("f", [&] { testFocusSpot(shots ? *shots : shots.emplace(ctx)); });
        timed("d", [&] { testFringes(shots ? *shots : shots.emplace(ctx)); });
        timed("g", [&] { testGodFocus(shots ? *shots : shots.emplace(ctx)); });
        shots.reset();
        timed("p", [&] { test::checkChapterPictures(ctx, SceneId::Lens); });
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("scene_lens: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
