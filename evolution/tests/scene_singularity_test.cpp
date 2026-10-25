// The Singularity scene (step 5, Task 25) and the gravitational lens (shaders/rt/lensing.glsl) on lavapipe,
// with the validation layer on; fails on any message.
//
// (k) the card names 48-step geodesics, which is the scene's lens, and is the scene's own nowCard; the shots'
//     exposures stay flat (the white-out is post::Look's); packLensing's refusals;
// (r) the Einstein ring: a star exactly behind the hole, seen from 20 r_s at 64x64, shows as a ring whose
//     radius is the analytic one (the null geodesic's exact deflection, integrated finely on the CPU) within
//     1 px; the weak-field estimate sqrt(2 r_s / D) is printed beside it;
// (b) capture: under a uniformly bright sky every pixel whose rays all have impact parameter b < 2.55 r_s is
//     black, every one whose rays all have b > 3.0 r_s is not;
// (o) with the lens off (no LensingFrame) and with r_s = 0 the pictures are the same bytes, and the unbent
//     picture differs from the bent one;
// (p) checkChapterPictures and (s) checkSceneSweep (scene_fixture.hpp).
//
// `scene_singularity_test --look <dir> [spp] [detail]` renders the chapter's still on the accent (the ring)
// instead, plus EVO_LOOK_AT=<seconds,...> chapter-local moments.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/lensing.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/mesh_builder.hpp"
#include "modern/scenes/singularity.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include "scene_fixture.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
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

int chapterOf(show::Effect effect) {
    for (int c = 0; c < show::chapterCount(); ++c)
        if (show::sampleTimeline(show::chapterStart(c)).effect == effect)
            return c;
    throw std::runtime_error("no such chapter");
}

show::TimelineFrame at(int chapter, double local) {
    show::TimelineFrame f = show::sampleTimeline(show::chapterStart(chapter) + local);
    f.era = 1.f;
    f.wipe = 1.f;
    f.eraCut = 0.f;
    return f;
}

void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

template <typename E, typename F> bool throws(F&& f) {
    try {
        f();
    } catch (const E&) {
        return true;
    }
    return false;
}

// --- (k) the card
// --------------------------------------------------------------------------------------------

void testCard() {
    const show::TimelineFrame f = at(chapterOf(show::Effect::Singularity), 3.0);
    std::printf("(k) SINGULARITY: \"%s\"\n", f.nowCard);
    CHECK(std::string(f.nowCard).find("48-STEP GEODESICS") != std::string::npos);
    CHECK(modern::kGeodesicSteps == 48u);
    const modern::Singularity scene{modern::SceneDetail{1.f}};
    // The scene's own card is the cue line, its step count the lens's at any detail.
    CHECK(scene.nowCard(show::Effect::Singularity) == f.nowCard);
    CHECK(modern::Singularity{modern::SceneDetail{1.f / 256}}.nowCard(show::Effect::Singularity) == f.nowCard);
    CHECK(scene.nowCard(show::Effect::Singularity).find(std::to_string(modern::singularityLens().steps) + "-STEP") !=
          std::string::npos);
    CHECK(scene.nowCard(show::Effect::Return).empty());
    // No white-out of its own: the shots' exposures are flat (post::Look's arc carries the fall to white).
    const int chapter = chapterOf(show::Effect::Singularity);
    const double duration = at(chapter, 0.0).chapterDuration;
    for (double t = 0.0; t < duration; t += 0.25)
        CHECK(modern::singularityExposureEV(at(chapter, t)) <= 0.5f);
    const modern::SceneFrame frame = scene.frame(f);
    CHECK(frame.lensing && frame.lensing->steps == 48u && frame.lensing->massRadius == 1.f &&
          frame.lensing->influenceRadius == 30.f && frame.lensing->disc);
    CHECK(frame.medium && frame.medium->grid && frame.medium->grid->size == modern::kDiscGrid);
    CHECK(scene.staticScene().capacity.mediumGrid == modern::kDiscGrid);
    CHECK(frame.splats && frame.splats->count == 2 * modern::kSingularityStars);
    modern::LensingFrame bad = modern::singularityLens();
    bad.steps = 0;
    CHECK(throws<std::invalid_argument>([&] { modern::packLensing(bad); }));
    bad = modern::singularityLens();
    bad.influenceRadius = 1.f;
    CHECK(throws<std::invalid_argument>([&] { modern::packLensing(bad); }));
    bad = modern::singularityLens();
    bad.massRadius = std::numeric_limits<float>::quiet_NaN();
    CHECK(throws<std::invalid_argument>([&] { modern::packLensing(bad); }));
    // A disc needs the frame's grid medium.
    modern::SceneFrame noGrid = frame;
    noGrid.medium.reset();
    CHECK(throws<std::invalid_argument>([&] { modern::checkSceneFrame(scene.staticScene(), noGrid); }));
}

// --- The test lens: one hole, nothing else, a configurable sky
// --------------------------------------------------------

constexpr std::uint32_t kSize = 64;
constexpr float kDistance = 20.f, kFov = 60.f;

class LensScene final : public modern::ModernScene {
public:
    LensScene(std::optional<modern::LensingFrame> lens, bool brightSky) : lens_(lens), brightSky_(brightSky) {
        static_.meshes = {modern::quad(1.f, 1.f)};
        static_.materials.resize(1);
        static_.capacity.instances = 1;
        static_.capacity.lights = 1;
        static_.capacity.materials = 1;
    }
    const modern::SceneStatic& staticScene() const override { return static_; }
    Camera camera(const show::TimelineFrame&) const override {
        return {{0.f, 0.f, kDistance}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, kFov};
    }
    modern::SceneFrame frame(const show::TimelineFrame& f) const override {
        modern::SceneFrame frame;
        frame.camera = camera(f);
        frame.lensing = lens_;
        modern::EnvironmentDesc& env = frame.environment;
        env = modern::environmentDefaults(modern::EnvironmentKind::Space);
        env.starDensity = 0.f;
        env.select = 0.f;
        if (brightSky_) {
            env.zenith = env.horizon = env.ground = {1.f, 1.f, 1.f};
        } else {
            env.zenith = env.horizon = env.ground = {0.f, 0.f, 0.f};
            env.discDirection = {0.f, 0.f, -1.f}; // exactly behind the hole
            env.discAngularRadius = 0.3f * 3.14159265f / 180.f;
            env.discRadiance = {1000.f, 1000.f, 1000.f};
        }
        return frame;
    }
    bool samplesRetro() const override { return false; }

private:
    modern::SceneStatic static_;
    std::optional<modern::LensingFrame> lens_;
    bool brightSky_;
};

modern::LensingFrame testLens(float massRadius) {
    modern::LensingFrame lens = modern::singularityLens();
    lens.massRadius = massRadius;
    lens.disc = false;
    return lens;
}

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    float value;
    if (exponent == 0)
        value = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31)
        value = mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else
        value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -value : value;
}

struct Picture {
    std::vector<float> emission; // RGBA per pixel, the emission guide (the sky the camera rays reach)
    std::vector<std::uint8_t> rgb;
};

Picture renderLens(gfx::Context& ctx, std::optional<modern::LensingFrame> lens, bool brightSky) {
    render::RenderSettings settings{kSize, kSize};
    settings.hud = false;
    settings.mode = render::RenderMode::Export;
    settings.spp = 4;
    settings.sceneFactory = [lens, brightSky](SceneId id, modern::SceneDetail d) -> std::unique_ptr<modern::ModernScene> {
        if (id == SceneId::Singularity)
            return std::make_unique<LensScene>(lens, brightSky);
        return modern::makeScene(id, d);
    };
    const int chapter = chapterOf(show::Effect::Singularity);
    const show::TimelineFrame f = at(chapter, 5.0);
    gfx::FrameRing ring(ctx);
    gfx::Readback readback(ctx, kSize, kSize);
    Picture out;
    {
        render::Renderer renderer(ctx, ring, settings);
        const double times[] = {f.showSeconds};
        renderer.prepare(times);
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, f);
        readback.record(cmd, renderer.output());
        gfx::Image& emission = renderer.modern().tracer()->gbuffer().emission;
        gfx::Buffer buffer(ctx, {VkDeviceSize(kSize) * kSize * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "emission"});
        emission.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kSize, kSize, 1};
        vkCmdCopyImageToBuffer(cmd, emission.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle(), 1,
                               &region);
        gfx::bufferBarrier2(cmd, buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        ring.wait(ring.submit());
        idle(ctx);
        buffer.invalidate();
        const auto* half = static_cast<const std::uint16_t*>(buffer.mapped());
        out.emission.resize(std::size_t(kSize) * kSize * 4);
        for (std::size_t i = 0; i < out.emission.size(); ++i)
            out.emission[i] = halfToFloat(half[i]);
        out.rgb = readback.rgb();
    }
    idle(ctx);
    return out;
}

// The exact deflection: the null geodesic x'' = -3/2 r_s h^2 x / r^5 (the orbits of Binet's equation) from the
// camera at distance D, leaving at angle theta from the line to the hole, integrated with fine RK4 steps far
// out, plus the remaining weak-field tail. Returns the final direction's component across the axis (0: the
// ray ends parallel to the axis, i.e. it reaches a star exactly behind the hole).
double finalAcross(double theta, double D) {
    double x[3] = {0, 0, D}, v[3] = {std::sin(theta), 0, -std::cos(theta)};
    const double h2 = D * D * std::sin(theta) * std::sin(theta);
    const auto accel = [&](const double* p, double* a) {
        const double r2 = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
        const double s = -1.5 * h2 / (r2 * r2 * std::sqrt(r2));
        for (int i = 0; i < 3; ++i)
            a[i] = s * p[i];
    };
    for (int step = 0; step < 2000000; ++step) {
        const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
        if (r < 1.0)
            return std::numeric_limits<double>::quiet_NaN();
        const double out = (x[0] * v[0] + x[1] * v[1] + x[2] * v[2]);
        if (r > 1e6 && out > 0)
            break;
        const double speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        const double hh = 2e-3 * r / speed;
        double k1v[3], k2v[3], k3v[3], k4v[3], p[3];
        accel(x, k1v);
        for (int i = 0; i < 3; ++i)
            p[i] = x[i] + 0.5 * hh * v[i];
        accel(p, k2v);
        for (int i = 0; i < 3; ++i)
            p[i] = x[i] + 0.5 * hh * (v[i] + 0.5 * hh * k1v[i]);
        accel(p, k3v);
        for (int i = 0; i < 3; ++i)
            p[i] = x[i] + hh * (v[i] + 0.5 * hh * k2v[i]);
        accel(p, k4v);
        for (int i = 0; i < 3; ++i) {
            x[i] += hh * (v[i] + hh / 6.0 * (k1v[i] + k2v[i] + k3v[i]));
            v[i] += hh / 6.0 * (k1v[i] + 2 * k2v[i] + 2 * k3v[i] + k4v[i]);
        }
    }
    // The tail beyond 10^6 r_s: (r_s / b)(1 - x.v / r), towards the axis.
    const double speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    double u[3] = {v[0] / speed, v[1] / speed, v[2] / speed};
    const double r = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    const double along = x[0] * u[0] + x[1] * u[1] + x[2] * u[2];
    const double perp[3] = {x[0] - along * u[0], x[1] - along * u[1], x[2] - along * u[2]};
    const double b = std::sqrt(perp[0] * perp[0] + perp[1] * perp[1] + perp[2] * perp[2]);
    const double tail = 1.0 / b * (1.0 - along / r);
    return u[0] * std::cos(tail) - perp[0] / b * std::sin(tail);
}

double analyticRingAngle(double D) {
    double lo = 0.2, hi = 0.7; // across: negative (crossed over) below the ring, positive above
    CHECK(finalAcross(lo, D) < 0 && finalAcross(hi, D) > 0);
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        (finalAcross(mid, D) < 0 ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

double luminance(const std::vector<float>& e, std::size_t i) {
    return 0.2126 * e[4 * i] + 0.7152 * e[4 * i + 1] + 0.0722 * e[4 * i + 2];
}

void testRing(gfx::Context& ctx) {
    const Picture p = renderLens(ctx, testLens(1.f), false);
    const double tanHalf = std::tan(kFov * 3.14159265358979 / 360.0);
    double peak = 0;
    for (std::size_t i = 0; i < std::size_t(kSize) * kSize; ++i)
        peak = std::max(peak, luminance(p.emission, i));
    double weight = 0, sum = 0;
    std::uint32_t lit = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const double dx = x + 0.5 - kSize / 2.0, dy = y + 0.5 - kSize / 2.0;
            const double r = std::sqrt(dx * dx + dy * dy);
            const double l = luminance(p.emission, std::size_t(y) * kSize + x);
            if (r < 10.0 || r > 31.0 || l < 0.02 * peak)
                continue; // only the primary ring (the photon rings hug the shadow, ~7 px)
            weight += l;
            sum += l * r;
            ++lit;
        }
    const double measured = weight > 0 ? sum / weight : 0;
    const double theta = analyticRingAngle(kDistance);
    const double expected = std::tan(theta) / tanHalf * (kSize / 2.0);
    const double weak = std::tan(std::sqrt(2.0 / kDistance)) / tanHalf * (kSize / 2.0);
    std::printf("(r) Einstein ring from %.0f r_s at %ux%u: measured radius %.2f px over %u pixels; the exact "
                "geodesic %.2f px (%.4f rad), the weak-field sqrt(2 r_s / D) %.2f px\n",
                kDistance, kSize, kSize, measured, lit, expected, theta, weak);
    CHECK(lit >= 60);
    CHECK(std::abs(measured - expected) <= 1.0);
    // The ring is round: its pixels' radii spread little about the mean.
    double spread = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const double dx = x + 0.5 - kSize / 2.0, dy = y + 0.5 - kSize / 2.0;
            const double r = std::sqrt(dx * dx + dy * dy);
            const double l = luminance(p.emission, std::size_t(y) * kSize + x);
            if (r >= 10.0 && r <= 31.0 && l >= 0.02 * peak)
                spread += l * (r - measured) * (r - measured);
        }
    std::printf("(r) the ring's radial spread %.2f px\n", std::sqrt(spread / weight));
    CHECK(std::sqrt(spread / weight) < 1.5);
}

// --- (b) capture
// ---------------------------------------------------------------------------------------------

void testCapture(gfx::Context& ctx) {
    const Picture p = renderLens(ctx, testLens(1.f), true);
    const double tanHalf = std::tan(kFov * 3.14159265358979 / 360.0);
    std::uint32_t inside = 0, insideBlack = 0, outside = 0, outsideLit = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            double bMin = 1e9, bMax = 0;
            for (const double cy : {0.0, 1.0})
                for (const double cx : {0.0, 1.0}) {
                    const double sx = (2.0 * (x + cx) / kSize - 1.0) * tanHalf;
                    const double sy = (1.0 - 2.0 * (y + cy) / kSize) * tanHalf;
                    const double sinTheta = std::sqrt(sx * sx + sy * sy) / std::sqrt(1.0 + sx * sx + sy * sy);
                    const double b = kDistance * sinTheta;
                    bMin = std::min(bMin, b);
                    bMax = std::max(bMax, b);
                }
            const double l = luminance(p.emission, std::size_t(y) * kSize + x);
            if (bMax < 2.55) {
                ++inside;
                insideBlack += l == 0.0;
            } else if (bMin > 3.0) {
                ++outside;
                outsideLit += l > 0.0;
            }
        }
    std::printf("(b) capture: %u pixels with b < 2.55 r_s, %u black; %u with b > 3.0 r_s, %u lit\n", inside,
                insideBlack, outside, outsideLit);
    CHECK(inside >= 30 && insideBlack == inside);
    CHECK(outside >= 3000 && outsideLit == outside);
}

// --- (o) the lens off
// -----------------------------------------------------------------------------------------

void testLensOff(gfx::Context& ctx) {
    const Picture off = renderLens(ctx, std::nullopt, false);
    const Picture zero = renderLens(ctx, testLens(0.f), false);
    const Picture bent = renderLens(ctx, testLens(1.f), false);
    CHECK(off.rgb == zero.rgb);
    CHECK(off.emission == zero.emission);
    CHECK(off.rgb != bent.rgb);
    // Unbent, the star is a small disc in the middle of the picture.
    double weight = 0, sum = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const double l = luminance(off.emission, std::size_t(y) * kSize + x);
            weight += l;
            sum += l * std::hypot(x + 0.5 - kSize / 2.0, y + 0.5 - kSize / 2.0);
        }
    std::printf("(o) lens off and r_s = 0: identical bytes; unbent the star sits %.2f px from the centre\n",
                weight > 0 ? sum / weight : -1.0);
    CHECK(weight > 0 && sum / weight < 1.5);
}

// --- Look
// ---------------------------------------------------------------------------------------------------

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
    const int chapter = chapterOf(show::Effect::Singularity);
    std::vector<double> locals = {at(chapter, 0).accentSeconds + 0.4};
    if (const char* list = std::getenv("EVO_LOOK_AT")) {
        locals.clear();
        std::string s(list);
        std::size_t pos = 0;
        while (pos < s.size()) {
            const std::size_t comma = std::min(s.find(',', pos), s.size());
            locals.push_back(std::stod(s.substr(pos, comma - pos)));
            pos = comma + 1;
        }
    }
    std::vector<double> times;
    for (const double l : locals)
        times.push_back(show::chapterStart(chapter) + l);
    renderer.prepare(times);
    std::filesystem::create_directories(dir);
    for (std::size_t i = 0; i < locals.size(); ++i) {
        const auto start = std::chrono::steady_clock::now();
        show::TimelineFrame f = show::sampleTimeline(times[i]);
        f.era = 1.f;
        f.wipe = 1.f;
        f.eraCut = 0.f;
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, f);
        readback.record(cmd, renderer.output());
        ring.wait(ring.submit());
        const std::vector<std::uint8_t> rgb = readback.rgb();
        double mean = 0;
        for (std::size_t p = 0; p < rgb.size(); p += 3)
            mean += (0.2126 * rgb[p] + 0.7152 * rgb[p + 1] + 0.0722 * rgb[p + 2]) / 255.0;
        mean /= double(rgb.size() / 3);
        char name[64];
        std::snprintf(name, sizeof name, "singularity_%.2f.png", locals[i]);
        const std::filesystem::path path = dir / name;
        image::writePng(path, int(kW), int(kH), rgb.data());
        std::printf("look: %s (mean %.3f) in %.1f s\n", path.string().c_str(), mean, secondsSince(start));
    }
    idle(ctx);
}


} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
    try {
        auto ctx = gfx::Context::create({.validation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the results are checked\n");
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 16u,
                       argc >= 5 ? std::stof(argv[4]) : 1.f);
            return ctx.validationMessageCount() == 0 ? 0 : 1;
        }
        const auto timed = [&](const char* name, auto&& run) {
            const auto t0 = std::chrono::steady_clock::now();
            run();
            std::printf("  [%s %.2f s]\n", name, secondsSince(t0));
        };
        timed("k", [&] { testCard(); });
        timed("r", [&] { testRing(ctx); });
        timed("b", [&] { testCapture(ctx); });
        timed("o", [&] { testLensOff(ctx); });
        timed("s", [&] {
            test::SweepLimits limits;
            limits.maxCameraStep = 1.f;
            const test::SweepStats stats = test::checkSceneSweep(SceneId::Singularity, limits);
            CHECK(stats.frames > 1400);
        });
        timed("p", [&] { test::checkChapterPictures(ctx, SceneId::Singularity); });
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("scene_singularity: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
