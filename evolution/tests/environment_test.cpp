// Scene environments and sky sampling (scene environments) on lavapipe, with the validation layer and its
// synchronization validation on; fails on any message. A GPU probe (shaders/test/environment_probe.comp) runs
// environment.glsl's functions directly; the path tracer runs at 64x64 unless noted.
//
// (0) the C++ side: EnvironmentDesc{} is the NightSky defaults (the moon's direction, no disc, not sampled),
//     every kind's defaults pack to valid uniforms, the disc and dome shares, and the refusals (also through
//     checkSceneFrame, which the path tracer and the scene sweep run); the Studio's frames keep their own
//     NightSky (studioEnvironment, the Studio light rig), never sampled, and its moon emitter stands along that
//     sky's glow direction; RETURN and REBOOT keep that sky with the moon's glow out and the horizon at dawn;
// (a) disc and dome sampling: every sample's pdf equals environmentPdf (1e-4) and its radiance equals
//     environmentLighting; the pdf integrates to 1 over the sphere (importance-sampled Monte Carlo, 1 %);
//     the disc gets discShare of the samples, all inside it;
// (b) a diffuse floor under a Day sky with a sun disc 0.5 degrees across: NEE + BSDF equals the analytic
//     irradiance of the disc plus a quadrature of the dome (environmentLighting through the probe), for an
//     environment share of 0.5 and of 1; the error per pixel at 16 spp is at least 4x lower with NEE; with an
//     8-degree disc, which BSDF sampling alone finds often enough, NEE + BSDF equals BSDF alone (3 %);
// (c) the lighting sky has no stars: the floor's lighting is byte-identical with starBrightness 0 and 1000
//     (environment NEE and escaping rays both see environmentLighting), the probe's environmentLighting too;
//     a primary ray straight at a star sees it;
// (d) flux conservation: the stars over a patch of 16 x 9 star cells at the zenith, rendered at 160x90 and at
//     320x180 (different filter widths), have the same mean radiance (2 %); the probe's quadrature of
//     environmentView over the patch at two footprints agrees too (0.5 %); and off a face's centre, where
//     cells are smaller and stars elongated in uv, the quadrature over 16 x 16 cells equals the flux of their
//     stars from the hash (0.5 %);
// (e) the Studio's mean display luminance at STUDIO NIGHT 40 % and REBOOT 60 % (160x90, spp 4, Export)
//     stays within 5 % of its measured value (first the step 4 night sky's, measured before this task; since
//     the Studio light rig relit and reframed the Studio, that task's).
// The layout of EnvironmentUniforms is material_test's probe. With --look <dir> [spp] it only renders stills
// of the skies to look at.

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/environment.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene_gpu.hpp"
#include "modern/studio.hpp"
#include "post/tonemap_reference.hpp"
#include "render/renderer.hpp"
#include "rt/gpu_types.hpp"
#include "rt/random.hpp"
#include "rt/sky_constants.hpp"
#include "rt_test_scenes.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace evo;
using modern::Camera;
using modern::EnvironmentDesc;
using modern::EnvironmentKind;
using modern::TraceSettings;
using modern::Vec3;
using test::TestScene;

constexpr double kPi = std::numbers::pi;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

template <typename Exception, typename F> bool throws(F&& f) {
    try {
        f();
    } catch (const Exception& error) {
        std::printf("  refused: %s\n", error.what());
        return true;
    }
    return false;
}

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

double luminance(const double rgb[3]) {
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
}

Vec3 fromElevation(double elevationDegrees, double azimuthDegrees) {
    const double e = elevationDegrees * kPi / 180, a = azimuthDegrees * kPi / 180;
    return {float(std::cos(e) * std::sin(a)), float(std::sin(e)), float(std::cos(e) * std::cos(a))};
}

// --- The probe ---------------------------------------------------------------------------------------------

struct Evaluation {
    double view[3], lighting[3], pdf;
};
struct Sample {
    bool ok;
    Vec3 wi;
    double le[3], pdf, pdfEval, lighting[3];
};

// environment.glsl run by a compute shader over a list of inputs: mode 0 evaluates directions (xyz, w the
// footprint), mode 1 draws samples (xy the random numbers).
class Probe {
public:
    explicit Probe(gfx::Context& ctx)
        : ctx_(ctx),
          pipeline_(ctx, EVO_TEST_SPIRV_DIR "/test_environment_probe.comp.spv", kBindings, sizeof(Push)) {}

    std::vector<Evaluation> evaluate(const rt::EnvironmentUniforms& env,
                                     const std::vector<std::array<float, 4>>& in) {
        const std::vector<float> out = run(env, in, 0);
        std::vector<Evaluation> e(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) {
            const float* r = &out[i * kResultFloats];
            for (int c = 0; c < 3; ++c) {
                e[i].view[c] = r[c];
                e[i].lighting[c] = r[3 + c];
            }
            e[i].pdf = r[6];
        }
        return e;
    }

    std::vector<Sample> sample(const rt::EnvironmentUniforms& env,
                               const std::vector<std::array<float, 4>>& in) {
        const std::vector<float> out = run(env, in, 1);
        std::vector<Sample> s(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) {
            const float* r = &out[i * kResultFloats];
            s[i].ok = r[0] != 0.f;
            s[i].wi = {r[1], r[2], r[3]};
            for (int c = 0; c < 3; ++c) {
                s[i].le[c] = r[4 + c];
                s[i].lighting[c] = r[9 + c];
            }
            s[i].pdf = r[7];
            s[i].pdfEval = r[8];
        }
        return s;
    }

private:
    struct Push {
        std::uint32_t mode, count;
    };
    static constexpr std::uint32_t kResultFloats = 12;
    static constexpr std::array<gfx::Binding, 3> kBindings{{{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}}};

    std::vector<float> run(const rt::EnvironmentUniforms& env, const std::vector<std::array<float, 4>>& in,
                           std::uint32_t mode) {
        gfx::Buffer envBuffer(ctx_,
                              {sizeof env, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "probe environment"});
        std::memcpy(envBuffer.mapped(), &env, sizeof env);
        envBuffer.flush();
        gfx::Buffer inBuffer(ctx_, {sizeof(float) * 4 * std::max<std::size_t>(in.size(), 1),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "probe inputs"});
        std::memcpy(inBuffer.mapped(), in.data(), sizeof(float) * 4 * in.size());
        inBuffer.flush();
        gfx::Buffer outBuffer(ctx_, {sizeof(float) * kResultFloats * std::max<std::size_t>(in.size(), 1),
                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "probe results"});
        const std::array<VkDescriptorBufferInfo, 3> infos{{{envBuffer.handle(), 0, VK_WHOLE_SIZE},
                                                           {inBuffer.handle(), 0, VK_WHOLE_SIZE},
                                                           {outBuffer.handle(), 0, VK_WHOLE_SIZE}}};
        {
            gfx::FrameRing ring(ctx_);
            VkCommandBuffer cmd = ring.begin();
            pipeline_.bind(cmd, ring, {}, infos);
            const Push push{mode, static_cast<std::uint32_t>(in.size())};
            pipeline_.push(cmd, &push, sizeof push);
            pipeline_.dispatch(cmd, static_cast<std::uint32_t>(in.size()), 1, 64, 1);
            gfx::bufferBarrier2(cmd, outBuffer.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                VK_ACCESS_2_HOST_READ_BIT);
            ring.wait(ring.submit());
        }
        idle(ctx_);
        outBuffer.invalidate();
        const auto* r = static_cast<const float*>(outBuffer.mapped());
        return std::vector<float>(r, r + kResultFloats * in.size());
    }

    gfx::Context& ctx_;
    gfx::ComputePipeline pipeline_;
};

// The irradiance on an upward floor from environmentLighting over the upper hemisphere: a midpoint
// quadrature in (cos^2 theta, phi), whose cells have equal projected solid angle (E = pi * mean).
std::array<double, 3> domeIrradiance(Probe& probe, const rt::EnvironmentUniforms& env, int n = 512) {
    std::vector<std::array<float, 4>> dirs;
    dirs.reserve(std::size_t(n) * n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const double cos2 = (i + 0.5) / n, phi = 2 * kPi * (j + 0.5) / n;
            const double c = std::sqrt(cos2), s = std::sqrt(1 - cos2);
            dirs.push_back({float(s * std::cos(phi)), float(c), float(s * std::sin(phi)), 0.f});
        }
    const std::vector<Evaluation> e = probe.evaluate(env, dirs);
    std::array<double, 3> sum{0, 0, 0};
    for (const Evaluation& v : e)
        for (int c = 0; c < 3; ++c)
            sum[c] += v.lighting[c];
    for (double& s : sum)
        s *= kPi / double(e.size());
    return sum;
}

// --- The path tracer ---------------------------------------------------------------------------------------

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

// One image read back as floats, 4 channels per pixel, plus its raw bytes.
struct Plane {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> bytes;
    std::vector<float> values;
    float at(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return values[(std::size_t(y) * width + x) * 4 + c];
    }
    double mean(std::uint32_t c) const {
        double sum = 0;
        for (std::size_t i = 0; i < std::size_t(width) * height; ++i)
            sum += values[i * 4 + c];
        return sum / (double(width) * height);
    }
};

struct PendingRead {
    gfx::Buffer buffer;
    VkFormat format;
    std::uint32_t width, height;
};

PendingRead recordRead(gfx::Context& ctx, VkCommandBuffer cmd, gfx::Image& image) {
    const VkFormat format = image.format();
    const std::uint32_t bytesPerPixel = format == VK_FORMAT_R32G32B32A32_SFLOAT ? 16 : 8;
    const VkExtent2D extent = image.extent();
    PendingRead read{gfx::Buffer(ctx, {VkDeviceSize(extent.width) * extent.height * bytesPerPixel,
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "environment test readback"}),
                     format, extent.width, extent.height};
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, read.buffer.handle(), 1,
                           &region);
    gfx::bufferBarrier2(cmd, read.buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                        VK_ACCESS_2_HOST_READ_BIT);
    return read;
}

Plane finishRead(const PendingRead& read) {
    read.buffer.invalidate();
    Plane plane;
    plane.width = read.width;
    plane.height = read.height;
    const auto* bytes = static_cast<const std::uint8_t*>(read.buffer.mapped());
    plane.bytes.assign(bytes, bytes + read.buffer.size());
    const std::size_t floats = std::size_t(read.width) * read.height * 4;
    plane.values.resize(floats);
    if (read.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
        for (std::size_t i = 0; i < floats; ++i) {
            std::uint16_t h;
            std::memcpy(&h, bytes + 2 * i, 2);
            plane.values[i] = halfToFloat(h);
        }
    } else {
        std::memcpy(plane.values.data(), bytes, floats * sizeof(float));
    }
    return plane;
}

struct Frame {
    Plane lighting, emission;
};

// A path tracer for one scene with its own ring (declared after the scene's GPU side, before the tracer).
class Tracer {
public:
    Tracer(gfx::Context& ctx, const TestScene& scene, std::uint32_t width, std::uint32_t height)
        : ctx_(ctx), gpu_(ctx, scene.statics), ring_(ctx),
          tracer_(ctx, ring_, width, height, modern::tracerCapacity(scene.statics.capacity)) {}

    Frame trace(const modern::SceneFrame& frame, const TraceSettings& settings) {
        VkCommandBuffer cmd = ring_.begin();
        tracer_.trace(cmd, gpu_, frame, std::nullopt, settings, nullptr);
        modern::GBuffer& g = tracer_.gbuffer();
        const PendingRead lighting = recordRead(ctx_, cmd, g.lighting);
        const PendingRead emission = recordRead(ctx_, cmd, g.emission);
        ring_.wait(ring_.submit());
        idle(ctx_);
        return {finishRead(lighting), finishRead(emission)};
    }

private:
    gfx::Context& ctx_;
    modern::SceneGpu gpu_;
    gfx::FrameRing ring_;
    modern::PathTracer tracer_;
};

constexpr float kFloorAlbedo = 0.5f;

// A diffuse floor of albedo 0.5 reaching far beyond the picture under an open sky (nothing else in the
// scene), seen from 1 m above: every pixel's lighting is albedo / pi times the sky's irradiance.
TestScene floorScene(const EnvironmentDesc& environment) {
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::lambert({kFloorAlbedo, kFloorAlbedo, kFloorAlbedo})};
    s.frame.instances = {{test::kQuadMesh, 0, test::place({0, 0, 0}, test::kX, -90, {4000, 4000, 1})}};
    s.frame.camera = {{0, 1, 0}, {0, 0, -0.5f}, {0, 1, 0}, 40.f};
    s.frame.environment = environment;
    return s;
}

// An empty sky (one quad far below, out of view) seen by `camera`.
TestScene skyScene(const EnvironmentDesc& environment, const Camera& camera) {
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::lambert({0, 0, 0})};
    s.frame.instances = {{test::kQuadMesh, 0, test::place({0, -500, 0}, test::kX, -90, {1, 1, 1})}};
    s.frame.camera = camera;
    s.frame.environment = environment;
    return s;
}

// --- (0) ---------------------------------------------------------------------------------------------------

bool sameWords(const rt::EnvironmentUniforms& a, const rt::EnvironmentUniforms& b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

void testDescriptions() {
    // The default environment is the NightSky defaults: the moon's direction, no disc, never sampled.
    const EnvironmentDesc night = modern::environmentDefaults(EnvironmentKind::NightSky);
    CHECK(sameWords(modern::packEnvironment(EnvironmentDesc{}), modern::packEnvironment(night)));
    const rt::EnvironmentUniforms n = modern::packEnvironment(night);
    CHECK(n.kind == std::uint32_t(EnvironmentKind::NightSky));
    const Vec3 moon =
        modern::normalize({rt::kMoonDirection[0], rt::kMoonDirection[1], rt::kMoonDirection[2]});
    const float moonWords[3] = {moon.x, moon.y, moon.z};
    for (int c = 0; c < 3; ++c) {
        CHECK(std::abs(n.discDirection[c] - moonWords[c]) < 1e-6f);
        CHECK(std::abs(n.glowDirection[c] - moonWords[c]) < 1e-6f);
    }
    CHECK(night.discAngularRadius == 0.f && night.select == 0.f);
    CHECK(n.discShare == 0.f && n.domeShare == 1.f && n.discCosRadius == 1.f);
    CHECK(n.intensity == 1.f && n.starBrightness > 0.f && n.townDensity > 0.f);

    for (const EnvironmentKind kind :
         {EnvironmentKind::NightSky, EnvironmentKind::Space, EnvironmentKind::Dusk, EnvironmentKind::Day}) {
        const EnvironmentDesc d = modern::environmentDefaults(kind);
        const rt::EnvironmentUniforms u = modern::packEnvironment(d);
        CHECK(u.kind == std::uint32_t(kind) && d.kind == kind);
        CHECK(u.discShare >= 0.f && u.domeShare >= 0.f && u.discShare + u.domeShare <= 1.f + 1e-6f);
        CHECK(std::abs(u.discCosRadius - float(std::cos(double(d.discAngularRadius)))) < 1e-7f);
        CHECK(d.select >= 0.f && d.select <= 1.f);
        const float dirLength =
            std::sqrt(u.discDirection[0] * u.discDirection[0] + u.discDirection[1] * u.discDirection[1] +
                      u.discDirection[2] * u.discDirection[2]);
        CHECK(std::abs(dirLength - 1.f) < 1e-5f);
    }
    // Day and Dusk have a sun, sampled; Day sees no stars and no town.
    for (const EnvironmentKind kind : {EnvironmentKind::Dusk, EnvironmentKind::Day}) {
        const EnvironmentDesc d = modern::environmentDefaults(kind);
        const rt::EnvironmentUniforms u = modern::packEnvironment(d);
        CHECK(d.discAngularRadius > 0.f && d.select > 0.f);
        CHECK(u.discShare > 0.f && u.domeShare > 0.f && std::abs(u.discShare + u.domeShare - 1.f) < 1e-6f);
    }
    CHECK(modern::environmentDefaults(EnvironmentKind::Day).starBrightness == 0.f);
    CHECK(modern::environmentDefaults(EnvironmentKind::Day).townDensity == 0.f);

    // A brighter disc gets a larger share; the direction is normalised.
    EnvironmentDesc day = modern::environmentDefaults(EnvironmentKind::Day);
    const float share = modern::packEnvironment(day).discShare;
    day.discRadiance = day.discRadiance * 4.f;
    day.discDirection = day.discDirection * 3.f;
    const rt::EnvironmentUniforms brighter = modern::packEnvironment(day);
    CHECK(brighter.discShare > share);
    CHECK(std::abs(brighter.discDirection[1] - modern::normalize(day.discDirection).y) < 1e-6f);
    // A black sky has nothing to sample.
    EnvironmentDesc black = modern::environmentDefaults(EnvironmentKind::Space);
    black.zenith = black.horizon = black.ground = {};
    black.glow = 0;
    black.discAngularRadius = 0;
    const rt::EnvironmentUniforms none = modern::packEnvironment(black);
    CHECK(none.discShare == 0.f && none.domeShare == 0.f);

    auto refused = [](auto change) {
        EnvironmentDesc d = modern::environmentDefaults(EnvironmentKind::Day);
        change(d);
        return throws<std::invalid_argument>([&] { modern::packEnvironment(d); });
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(refused([](EnvironmentDesc& d) { d.select = -0.1f; }));
    CHECK(refused([](EnvironmentDesc& d) { d.select = 1.5f; }));
    CHECK(refused([&](EnvironmentDesc& d) { d.select = nan; }));
    CHECK(refused([](EnvironmentDesc& d) { d.discAngularRadius = -0.01f; }));
    CHECK(refused([](EnvironmentDesc& d) { d.discAngularRadius = 1.6f; }));
    CHECK(refused([](EnvironmentDesc& d) { d.discDirection = {}; }));
    CHECK(refused([&](EnvironmentDesc& d) { d.discDirection.x = nan; }));
    CHECK(refused([](EnvironmentDesc& d) { d.discRadiance = {-1, 1, 1}; }));
    CHECK(refused([](EnvironmentDesc& d) { d.zenith = {0, -0.1f, 0}; }));
    CHECK(refused([&](EnvironmentDesc& d) { d.intensity = nan; }));
    CHECK(refused([](EnvironmentDesc& d) { d.intensity = -1; }));
    CHECK(refused([](EnvironmentDesc& d) { d.starDensity = -1; }));
    CHECK(refused([](EnvironmentDesc& d) { d.glow = -1; }));
    CHECK(refused([](EnvironmentDesc& d) { d.kind = static_cast<EnvironmentKind>(7); }));
    // checkSceneFrame (the tracer's and the sweep's validation) refuses it too.
    TestScene s = floorScene(modern::environmentDefaults(EnvironmentKind::Day));
    modern::checkSceneFrame(s.statics, s.frame);
    s.frame.environment.select = 2;
    CHECK(throws<std::invalid_argument>([&] { modern::checkSceneFrame(s.statics, s.frame); }));
    // The Studio keeps its own NightSky, never sampled (its window portal samples the sky), and stands its
    // moon emitter along that sky's disc and glow direction. RETURN and REBOOT are the same sky at dawn:
    // the moon's glow is out and the horizon has lifted off the night colour.
    const rt::EnvironmentUniforms studioSky = modern::packEnvironment(modern::studioEnvironment());
    CHECK(studioSky.kind == std::uint32_t(EnvironmentKind::NightSky));
    const EnvironmentDesc nightSky = modern::studioEnvironment();
    const std::unique_ptr<modern::ModernScene> studio = modern::makeScene(modern::SceneId::Studio);
    for (const int chapter : {9, 10, 11, 19, 20})
        for (const double fraction : {0.1, 0.5, 0.9}) {
            const modern::SceneFrame f = studio->frame(show::sampleTimeline(
                show::chapterStart(chapter) +
                fraction * (show::chapterStart(chapter + 1) - show::chapterStart(chapter))));
            CHECK(f.environment.kind == EnvironmentKind::NightSky);
            CHECK(f.environment.select == 0.f);
            const bool dawn = chapter >= 19;
            if (!dawn)
                CHECK(sameWords(modern::packEnvironment(f.environment), studioSky));
            else {
                CHECK(f.environment.glow == 0.f);
                CHECK(f.environment.horizon.x > nightSky.horizon.x && f.environment.horizon.y > nightSky.horizon.y);
                CHECK(f.environment.discDirection.x == nightSky.discDirection.x &&
                      f.environment.discDirection.y == nightSky.discDirection.y &&
                      f.environment.discDirection.z == nightSky.discDirection.z);
            }
        }
    const Vec3 studioMoon = modern::normalize(modern::Studio::kMoonDirection);
    CHECK(std::abs(studioMoon.x - studioSky.discDirection[0]) < 1e-6f &&
          std::abs(studioMoon.y - studioSky.discDirection[1]) < 1e-6f &&
          std::abs(studioMoon.z - studioSky.discDirection[2]) < 1e-6f &&
          std::abs(studioMoon.x - studioSky.glowDirection[0]) < 1e-6f);
    std::printf("(0) defaults, shares and refusals; the Studio's NightSky\n");
}

// --- (a) ---------------------------------------------------------------------------------------------------

void testSampling(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    Probe probe(ctx);
    EnvironmentDesc d = modern::environmentDefaults(EnvironmentKind::Day);
    d.discDirection = fromElevation(35, 20);
    for (const float radiusDegrees : {0.25f, 3.f}) {
        d.discAngularRadius = radiusDegrees * float(kPi) / 180.f;
        const rt::EnvironmentUniforms env = modern::packEnvironment(d);
        constexpr int kN = 256;
        std::vector<std::array<float, 4>> us;
        for (int i = 0; i < kN; ++i)
            for (int j = 0; j < kN; ++j)
                us.push_back({(i + 0.5f) / kN, (j + 0.37f) / kN, 0, 0});
        const std::vector<Sample> samples = probe.sample(env, us);
        double worstPdf = 0, worstLe = 0;
        std::size_t inDisc = 0;
        const Vec3 axis = modern::normalize(d.discDirection);
        for (const Sample& s : samples) {
            CHECK(s.ok);
            CHECK(std::abs(modern::length(s.wi) - 1.f) < 1e-4f);
            worstPdf = std::max(worstPdf, std::abs(s.pdf - s.pdfEval) / s.pdfEval);
            for (int c = 0; c < 3; ++c)
                worstLe =
                    std::max(worstLe, std::abs(s.le[c] - s.lighting[c]) / std::max(s.lighting[c], 1e-6));
            inDisc += modern::dot(s.wi, axis) >= env.discCosRadius ? 1 : 0;
            CHECK(s.wi.y > 0.f || modern::dot(s.wi, axis) >= env.discCosRadius); // the dome: upper hemisphere
        }
        // The first discShare of u.x goes to the disc; the dome's cosine samples land in it rarely.
        const double discFraction = double(inDisc) / samples.size();
        std::printf(
            "    disc %.2f deg: worst pdf error %.2e, worst radiance error %.2e, %.4f of the samples in "
            "the disc (share %.4f)\n",
            radiusDegrees, worstPdf, worstLe, discFraction, env.discShare);
        CHECK(worstPdf < 1e-4);
        CHECK(worstLe < 1e-4);
        CHECK(discFraction > env.discShare - 1.0 / kN && discFraction < env.discShare + 1.0 / kN + 0.01);

        // The pdf integrates to 1: E_q[pdf / q] with q = half uniform on the sphere, half uniform in a cone
        // of twice the disc's radius about its axis (stratified, deterministic).
        const double cosCone = std::cos(2.0 * radiusDegrees * kPi / 180);
        const Vec3 t = modern::normalize(modern::cross(axis, Vec3{0, 1, 0}));
        const Vec3 b = modern::cross(axis, t);
        std::vector<std::array<float, 4>> dirs;
        std::vector<double> q;
        constexpr int kM = 200;
        for (int i = 0; i < kM; ++i)
            for (int j = 0; j < kM; ++j) {
                const double u1 = (i + 0.5) / kM, u2 = (j + 0.5) / kM;
                const double phi = 2 * kPi * u2;
                // uniform on the sphere
                const double z = 1 - 2 * u1, r = std::sqrt(std::max(0.0, 1 - z * z));
                dirs.push_back({float(r * std::cos(phi)), float(z), float(r * std::sin(phi)), 0.f});
                // uniform in the cone
                const double cz = 1 - u1 * (1 - cosCone), cr = std::sqrt(std::max(0.0, 1 - cz * cz));
                const Vec3 w =
                    axis * float(cz) + t * float(cr * std::cos(phi)) + b * float(cr * std::sin(phi));
                dirs.push_back({w.x, w.y, w.z, 0.f});
            }
        const double coneDensity = 1 / (2 * kPi * (1 - cosCone)), sphereDensity = 1 / (4 * kPi);
        const std::vector<Evaluation> e = probe.evaluate(env, dirs);
        double integral = 0;
        for (std::size_t i = 0; i < dirs.size(); ++i) {
            const Vec3 w{dirs[i][0], dirs[i][1], dirs[i][2]};
            const double inCone = modern::dot(modern::normalize(w), axis) >= cosCone ? coneDensity : 0;
            integral += e[i].pdf / (0.5 * sphereDensity + 0.5 * inCone);
        }
        integral /= double(dirs.size());
        std::printf("    disc %.2f deg: the pdf integrates to %.5f\n", radiusDegrees, integral);
        CHECK(std::abs(integral - 1) < 0.01);
    }
    idle(ctx);
    std::printf("(a) disc and dome sampling (%.1f s)\n", secondsSince(start));
}

// --- (b) ---------------------------------------------------------------------------------------------------

// The floor's expected lighting per channel: albedo / pi * (the disc's irradiance + the dome's).
std::array<double, 3> expectedFloor(Probe& probe, const EnvironmentDesc& d) {
    const rt::EnvironmentUniforms env = modern::packEnvironment(d);
    rt::EnvironmentUniforms dome = env;
    dome.discRadiance[0] = dome.discRadiance[1] = dome.discRadiance[2] = 0;
    const std::array<double, 3> eDome = domeIrradiance(probe, dome);
    const double sin2 = std::pow(std::sin(double(d.discAngularRadius)), 2);
    const double cosSun = modern::normalize(d.discDirection).y;
    const double radiance[3] = {d.discRadiance.x, d.discRadiance.y, d.discRadiance.z};
    std::array<double, 3> e{};
    for (int c = 0; c < 3; ++c)
        e[c] = kFloorAlbedo / kPi * (d.intensity * radiance[c] * kPi * sin2 * cosSun + eDome[c]);
    return e;
}

// The mean squared error per pixel against the expected luminance.
double pixelError(const Plane& p, double expected) {
    double sum = 0;
    for (std::uint32_t y = 0; y < p.height; ++y)
        for (std::uint32_t x = 0; x < p.width; ++x) {
            const double l = 0.2126 * p.at(x, y, 0) + 0.7152 * p.at(x, y, 1) + 0.0722 * p.at(x, y, 2);
            sum += (l - expected) * (l - expected);
        }
    return sum / (double(p.width) * p.height);
}

void testSunAndDome(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    Probe probe(ctx);
    constexpr std::uint32_t kSize = 64;
    const TraceSettings direct{.spp = 32, .maxBounces = 1, .frameSeed = 7, .clampIndirect = 1e30f};

    EnvironmentDesc sun = modern::environmentDefaults(EnvironmentKind::Day);
    sun.discDirection = fromElevation(50, -30);
    sun.discAngularRadius = 0.25f * float(kPi) / 180.f; // 0.5 degrees across
    sun.discRadiance = sun.discRadiance * 4.f;          // the default's power in a quarter of its solid angle
    const std::array<double, 3> want = expectedFloor(probe, sun);
    const double wantLum = luminance(want.data());
    {
        rt::EnvironmentUniforms dome = modern::packEnvironment(sun);
        dome.discRadiance[0] = dome.discRadiance[1] = dome.discRadiance[2] = 0;
        const double domeLum = luminance(domeIrradiance(probe, dome).data()) * kFloorAlbedo / kPi;
        std::printf("    0.5-degree sun: expected floor %.4f, of which the sun %.0f %%\n", wantLum,
                    100 * (1 - domeLum / wantLum));
        CHECK(domeLum < 0.5 * wantLum); // the sun dominates: the case the sampling exists for
    }
    for (const float select : {0.5f, 1.f}) {
        sun.select = select;
        const TestScene scene = floorScene(sun);
        Tracer tracer(ctx, scene, kSize, kSize);
        const Frame f = tracer.trace(scene.frame, direct);
        for (int c = 0; c < 3; ++c) {
            const double got = f.lighting.mean(std::uint32_t(c));
            std::printf("    select %.1f, channel %d: %.5f, analytic + quadrature %.5f (%.2f %%)\n", select,
                        c, got, want[c], 100 * (got / want[c] - 1));
            CHECK(std::abs(got / want[c] - 1) < 0.02);
        }
    }
    // At 16 spp the error per pixel is far lower with NEE (the BSDF alone rarely finds the sun).
    {
        sun.select = 0.5f;
        const TestScene scene = floorScene(sun);
        Tracer tracer(ctx, scene, kSize, kSize);
        TraceSettings s = direct;
        s.spp = 16;
        const double withNee = pixelError(tracer.trace(scene.frame, s).lighting, wantLum);
        s.nee = false;
        const double without = pixelError(tracer.trace(scene.frame, s).lighting, wantLum);
        std::printf("    16 spp: squared error per pixel %.3e with NEE, %.3e without (%.0fx)\n", withNee,
                    without, without / withNee);
        CHECK(without > 4 * withNee);
    }
    // An 8-degree disc: BSDF sampling alone converges, and NEE + MIS agrees with it.
    {
        EnvironmentDesc wide = sun;
        wide.discAngularRadius = 8.f * float(kPi) / 180.f;
        wide.discRadiance = sun.discRadiance * 0.001f;
        const std::array<double, 3> wideWant = expectedFloor(probe, wide);
        const TestScene scene = floorScene(wide);
        Tracer tracer(ctx, scene, kSize, kSize);
        TraceSettings s = direct;
        const double withNee = tracer.trace(scene.frame, s).lighting.mean(1);
        s.nee = false;
        s.spp = 128;
        const double without = tracer.trace(scene.frame, s).lighting.mean(1);
        std::printf("    8-degree disc, green: %.5f with NEE, %.5f BSDF alone (%.2f %%), analytic %.5f\n",
                    withNee, without, 100 * (withNee / without - 1), wideWant[1]);
        CHECK(std::abs(withNee / without - 1) < 0.03);
        CHECK(std::abs(withNee / wideWant[1] - 1) < 0.02);
    }
    idle(ctx);
    std::printf("(b) a floor under a sun and a dome (%.1f s)\n", secondsSince(start));
}

// --- (c) ---------------------------------------------------------------------------------------------------

void testLightingHasNoStars(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    Probe probe(ctx);
    EnvironmentDesc night = modern::environmentDefaults(EnvironmentKind::NightSky);
    night.select = 1; // the dome sampled, and every escaping ray: both see environmentLighting
    EnvironmentDesc noStars = night, bright = night;
    noStars.starBrightness = 0;
    bright.starBrightness = 1000;

    // The probe: environmentLighting is the same function either way; environmentView is not.
    std::vector<std::array<float, 4>> dirs;
    for (int i = 0; i < 128; ++i)
        for (int j = 0; j < 128; ++j) {
            const double y = 0.05 + 0.95 * (i + 0.5) / 128, phi = 2 * kPi * (j + 0.5) / 128;
            const double r = std::sqrt(1 - y * y);
            dirs.push_back({float(r * std::cos(phi)), float(y), float(r * std::sin(phi)), 0.01f});
        }
    const std::vector<Evaluation> a = probe.evaluate(modern::packEnvironment(noStars), dirs);
    const std::vector<Evaluation> b = probe.evaluate(modern::packEnvironment(bright), dirs);
    std::size_t viewDiffers = 0;
    for (std::size_t i = 0; i < dirs.size(); ++i) {
        for (int c = 0; c < 3; ++c)
            CHECK(a[i].lighting[c] == b[i].lighting[c]);
        viewDiffers += a[i].view[1] != b[i].view[1] ? 1 : 0;
    }
    CHECK(viewDiffers > 0);

    // The floor's lighting: byte-identical.
    {
        const TraceSettings settings{.spp = 4, .maxBounces = 2, .frameSeed = 3};
        const TestScene sceneA = floorScene(noStars), sceneB = floorScene(bright);
        Tracer tracerA(ctx, sceneA, 64, 64), tracerB(ctx, sceneB, 64, 64);
        const Frame fa = tracerA.trace(sceneA.frame, settings), fb = tracerB.trace(sceneB.frame, settings);
        CHECK(fa.lighting.mean(1) > 0);
        CHECK(fa.lighting.bytes == fb.lighting.bytes);
    }

    // A star: the zenith's star cells seen with a footprint wide enough to catch each star at its cell's
    // centre, then the brightest cell searched finely.
    constexpr int kCells = 64;      // about 11 stars at the default density
    const float cell = 2.f / 384.f; // star cells per cube face side: 384
    std::vector<std::array<float, 4>> centres;
    for (int i = 0; i < kCells; ++i)
        for (int j = 0; j < kCells; ++j)
            centres.push_back({(i - kCells / 2 + 0.5f) * cell, 1.f, (j - kCells / 2 + 0.5f) * cell, 1.f});
    const std::vector<Evaluation> coarse = probe.evaluate(modern::packEnvironment(night), centres);
    std::size_t best = 0;
    for (std::size_t i = 0; i < coarse.size(); ++i)
        if (coarse[i].view[1] - coarse[i].lighting[1] > coarse[best].view[1] - coarse[best].lighting[1])
            best = i;
    CHECK(coarse[best].view[1] > coarse[best].lighting[1] * 1.01);
    std::vector<std::array<float, 4>> fine;
    constexpr int kFine = 64;
    for (int i = 0; i < kFine; ++i)
        for (int j = 0; j < kFine; ++j)
            fine.push_back({centres[best][0] + ((i + 0.5f) / kFine - 0.5f) * cell, 1.f,
                            centres[best][2] + ((j + 0.5f) / kFine - 0.5f) * cell, 0.f});
    const std::vector<Evaluation> fineEval = probe.evaluate(modern::packEnvironment(night), fine);
    std::size_t peak = 0;
    for (std::size_t i = 0; i < fineEval.size(); ++i)
        if (fineEval[i].view[1] > fineEval[peak].view[1])
            peak = i;
    const Vec3 star = modern::normalize({fine[peak][0], fine[peak][1], fine[peak][2]});
    // A filtered point, not a lit cell: three times the footprint spreads its flux over nine times the solid
    // angle; a third of a cell away it is gone.
    {
        const std::vector<Evaluation> e =
            probe.evaluate(modern::packEnvironment(night), {{star.x, star.y, star.z, 4e-4f},
                                                            {star.x, star.y, star.z, 1.2e-3f},
                                                            {star.x + cell / 3, star.y, star.z, 0.f},
                                                            {star.x, star.y, star.z, 0.f}});
        const double narrow = e[0].view[1] - e[0].lighting[1], wide = e[1].view[1] - e[1].lighting[1];
        const double aside = e[2].view[1] - e[2].lighting[1], sharp = e[3].view[1] - e[3].lighting[1];
        std::printf(
            "    a star at footprint 4e-4: %.3f, at 1.2e-3: %.3f (%.1fx); a third of a cell aside %.2e\n",
            narrow, wide, narrow / wide, aside);
        CHECK(narrow > 6 * wide);
        CHECK(aside < 1e-3 * sharp);
    }

    // A primary ray straight at it: a camera 1 degree high on the star, 16 x 16 pixels.
    const Camera camera{{0, 0, 0}, star, {0, 0, 1}, 1.f};
    for (const bool lit : {true, false}) {
        EnvironmentDesc d = night;
        d.starBrightness = lit ? 1.f : 0.f;
        const TestScene scene = skyScene(d, camera);
        Tracer tracer(ctx, scene, 16, 16);
        const Frame f = tracer.trace(scene.frame, {.spp = 8, .maxBounces = 1, .frameSeed = 11});
        double centre = 0;
        for (std::uint32_t y = 7; y <= 8; ++y)
            for (std::uint32_t x = 7; x <= 8; ++x)
                centre += f.emission.at(x, y, 1) / 4;
        const double corner = f.emission.at(0, 0, 1);
        std::printf("    star %s: centre %.4f, corner %.4f\n", lit ? "on" : "off", centre, corner);
        if (lit)
            CHECK(centre > 3 * corner);
        else
            CHECK(std::abs(centre / corner - 1) < 0.01);
    }
    idle(ctx);
    std::printf("(c) the lighting sky has no stars; the view sees them (%.1f s)\n", secondsSince(start));
}

// --- (d) ---------------------------------------------------------------------------------------------------

void testStarFlux(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    // Stars only, one in every cell.
    EnvironmentDesc stars = modern::environmentDefaults(EnvironmentKind::NightSky);
    stars.zenith = stars.horizon = stars.ground = {};
    stars.glow = 0;
    stars.townDensity = 0;
    stars.starDensity = 1000;
    const float cell = 2.f / 384.f;
    // Looking straight up at the +y face, whose uv is (x, z) / y: picture x along +x, up along +z. The
    // picture spans 16 cells across, from a cell edge, and 9 high, from a cell centre's -4.5 to +4.5: no
    // star sits within a quarter cell of its border.
    const float tanHalf = 4.5f * cell;
    const Camera camera{{0, 0, 0}, {0, 1, 0.5f * cell}, {0, 0, 1}, float(2 * std::atan(tanHalf) * 180 / kPi)};
    double means[2];
    int k = 0;
    for (const auto [w, h] : {std::pair{160u, 90u}, std::pair{320u, 180u}}) {
        const TestScene scene = skyScene(stars, camera);
        Tracer tracer(ctx, scene, w, h);
        const Frame f = tracer.trace(scene.frame, {.spp = 8, .maxBounces = 1, .frameSeed = 5});
        means[k++] = f.emission.mean(1);
    }
    std::printf("    16 x 9 cells of stars: mean %.5f at 160x90, %.5f at 320x180 (%.2f %%)\n", means[0],
                means[1], 100 * (means[0] / means[1] - 1));
    CHECK(means[1] > 0);
    CHECK(std::abs(means[0] / means[1] - 1) < 0.02);

    // The probe: environmentView integrated over the same patch (a 4x supersampled grid of the 320x180
    // picture's pixels) at a footprint of 1 and of 2 of those pixels.
    Probe probe(ctx);
    const rt::EnvironmentUniforms env = modern::packEnvironment(stars);
    double integral[2];
    for (int f = 0; f < 2; ++f) {
        const float pixel = 2 * tanHalf / 180.f;
        std::vector<std::array<float, 4>> dirs;
        for (int i = 0; i < 4 * 320; ++i)
            for (int j = 0; j < 4 * 180; ++j)
                dirs.push_back({-16 * tanHalf / 9 + (i + 0.5f) * pixel / 4, 1.f,
                                0.5f * cell - tanHalf + (j + 0.5f) * pixel / 4, pixel * float(1 + f)});
        double sum = 0;
        for (const Evaluation& e : probe.evaluate(env, dirs))
            sum += e.view[1];
        integral[f] = sum / double(dirs.size());
    }
    std::printf("    the probe over the patch: %.5f at a footprint of 1 pixel, %.5f at 2 (%.2f %%)\n",
                integral[0], integral[1], 100 * (integral[1] / integral[0] - 1));
    CHECK(integral[0] > 0);
    CHECK(std::abs(integral[1] / integral[0] - 1) < 0.005);
    CHECK(std::abs(means[1] / integral[0] - 1) < 0.03);

    // Away from a face's centre (u, v about 0.6, 0.5 on +y, where cells are smaller and stars elongated in
    // uv): the green radiance integrated over 16 x 16 cells, sum of view * du dv / (1 + u^2 + v^2)^1.5,
    // equals the flux of the 256 stars in them, brightness * green tint * a cell's mean solid angle, from the
    // shader's hash ported here.
    {
        constexpr std::uint32_t kFirstX = 300, kFirstY = 280, kCellsAcross = 16, kSteps = 16;
        const double kMeanCellSolidAngle = 4 * kPi / (6.0 * 384 * 384);
        double flux = 0;
        for (std::uint32_t ky = kFirstY; ky < kFirstY + kCellsAcross; ++ky)
            for (std::uint32_t kx = kFirstX; kx < kFirstX + kCellsAcross; ++kx) {
                const std::uint32_t h = rt::pcgHash(kx + 384u * (ky + 384u * 2u) + 0x51A7u); // face 2: +y
                const double u = double((h >> 16) & 0xFFu) / 255.0,
                             warmth = double((h >> 24) & 0xFFu) / 255.0;
                flux += (0.1 + 2.1 * u * u * u * u) * (0.85 + 0.05 * warmth) * kMeanCellSolidAngle;
            }
        for (const float footprint : {0.f, 4e-4f}) {
            std::vector<std::array<float, 4>> dirs;
            std::vector<double> solidAngle;
            const float step = cell / kSteps;
            for (std::uint32_t j = 0; j < kCellsAcross * kSteps; ++j)
                for (std::uint32_t i = 0; i < kCellsAcross * kSteps; ++i) {
                    const float u = -1 + kFirstX * cell + (i + 0.5f) * step;
                    const float v = -1 + kFirstY * cell + (j + 0.5f) * step;
                    dirs.push_back({u, 1.f, v, footprint});
                    solidAngle.push_back(double(step) * step / std::pow(1.0 + u * u + v * v, 1.5));
                }
            const std::vector<Evaluation> e = probe.evaluate(env, dirs);
            double sum = 0;
            for (std::size_t i = 0; i < e.size(); ++i)
                sum += e[i].view[1] * solidAngle[i];
            std::printf("    16 x 16 cells off the face's centre, footprint %.0e: %.4e, the stars' flux %.4e "
                        "(%.2f %%)\n",
                        double(footprint), sum, flux, 100 * (sum / flux - 1));
            CHECK(std::abs(sum / flux - 1) < 0.005);
        }
    }
    idle(ctx);
    std::printf("(d) the stars' flux does not depend on the resolution (%.1f s)\n", secondsSince(start));
}

// --- (e) ---------------------------------------------------------------------------------------------------

show::TimelineFrame forced2026(double seconds) {
    show::TimelineFrame frame = show::sampleTimeline(seconds);
    frame.era = 1.f;
    frame.wipe = 1.f;
    frame.eraCut = 0.f;
    return frame;
}

double chapterTime(int chapter, double fraction) {
    const double start = show::chapterStart(chapter);
    return start + fraction * (show::chapterStart(chapter + 1) - start);
}

// The Studio under its sky, measured with this test's renderer: step 4's before scene environments (c722ab1) was 0.10121
// and 0.15723; the Studio light rig relit the Studio (STUDIO NIGHT: the lamp off, the CRT the key light, a low
// wide dolly; REBOOT: a dark modern room, the CRT's light rig and exposure).
// Re-measured on the merge of the Studio light rig with the NEE shadow-ray fix (d91fb09 and later), which lit REBOOT more.
// Re-measured again with the Studio's takes (STUDIO NIGHT's dolly back to the night wide, REBOOT's close-up)
// and the per-act grade (Act II's lifted blacks): 0.07627 and 0.13525, then 0.20728 and 0.23459.
// Re-measured after the Studio polish (the night sky, the town glow and the per-shot exposure): the night
// wide is darker and REBOOT's close-up sits lower.
constexpr double kStudioNightBaseline = 0.11457, kRebootBaseline = 0.17955;
constexpr int kStudioNight = 11, kReboot = 20;

void testStudio(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    render::RenderSettings settings{160, 90};
    settings.hud = false;
    settings.mode = render::RenderMode::Export;
    settings.spp = 4;
    settings.detail = modern::SceneDetail{1.f / 256};
    gfx::Readback readback(ctx, 160, 90);
    gfx::FrameRing ring(ctx);
    render::Renderer renderer(ctx, ring, settings);
    for (const auto [name, chapter, fraction, baseline] :
         {std::tuple{"STUDIO NIGHT 40 %", kStudioNight, 0.4, kStudioNightBaseline},
          std::tuple{"REBOOT 60 %", kReboot, 0.6, kRebootBaseline}}) {
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, forced2026(chapterTime(chapter, fraction)));
        readback.record(cmd, renderer.output());
        ring.wait(ring.submit());
        const std::vector<std::uint8_t> rgb = readback.rgb();
        double mean = 0;
        for (std::size_t i = 0; i < rgb.size(); i += 3)
            mean += (0.2126 * rgb[i] + 0.7152 * rgb[i + 1] + 0.0722 * rgb[i + 2]) / 255.0;
        mean /= double(rgb.size() / 3);
        std::printf("    %s: mean display luminance %.5f, measured %.5f (%.2f %%)\n", name, mean, baseline,
                    100 * (mean / baseline - 1));
        CHECK(std::abs(mean / baseline - 1) < 0.05);
    }
    idle(ctx);
    std::printf("(e) the Studio under its NightSky (%.1f s)\n", secondsSince(start));
}

// --- --look ------------------------------------------------------------------------------------------------

// lighting + emission times `exposure`, Reinhard and sRGB (for looking only), to <dir>/<name>.png.
void writeLook(const Frame& f, double exposure, const std::filesystem::path& dir, const std::string& name) {
    std::vector<std::uint8_t> rgb(std::size_t(f.lighting.width) * f.lighting.height * 3);
    for (std::uint32_t y = 0; y < f.lighting.height; ++y)
        for (std::uint32_t x = 0; x < f.lighting.width; ++x)
            for (std::uint32_t c = 0; c < 3; ++c) {
                const double v = exposure * (f.lighting.at(x, y, c) + f.emission.at(x, y, c));
                const float encoded = post::srgbOetf(float(v / (1 + v)));
                rgb[(std::size_t(y) * f.lighting.width + x) * 3 + c] =
                    std::uint8_t(std::clamp(encoded, 0.f, 1.f) * 255.f + 0.5f);
            }
    const std::filesystem::path path = dir / (name + ".png");
    image::writePng(path, int(f.lighting.width), int(f.lighting.height), rgb.data());
    std::printf("  wrote %s\n", path.string().c_str());
}

// Stills of the skies: the NightSky's town and stars at the Studio's scale (640x360 over 37 degrees) and at a
// 4K pixel's (a 640x360 crop of a 3840x2160 picture), and each kind over a floor with a chrome and a white
// sphere.
void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp) {
    std::filesystem::create_directories(dir);
    const TraceSettings settings{.spp = spp, .maxBounces = 4, .frameSeed = 1};
    const EnvironmentDesc night = modern::environmentDefaults(EnvironmentKind::NightSky);
    for (const auto& [name, fov] : {std::pair{"night_640", 37.f}, std::pair{"night_4k_crop", 37.f / 6}}) {
        for (const auto& [part, target] :
             {std::pair{"town", Vec3{0.3f, -0.012f, 1}}, std::pair{"stars", Vec3{0.3f, 0.8f, 1}}}) {
            const TestScene scene = skyScene(night, {{0, 0, 0}, target, {0, 1, 0}, fov});
            Tracer tracer(ctx, scene, 640, 360);
            writeLook(tracer.trace(scene.frame, settings), 25, dir, std::string(name) + "_" + part);
        }
    }
    for (const EnvironmentKind kind :
         {EnvironmentKind::NightSky, EnvironmentKind::Space, EnvironmentKind::Dusk, EnvironmentKind::Day}) {
        EnvironmentDesc d = modern::environmentDefaults(kind);
        d.select = kind == EnvironmentKind::NightSky || kind == EnvironmentKind::Space ? 1.f : d.select;
        TestScene scene = floorScene(d);
        scene.statics.materials.push_back(test::metal({0.95f, 0.95f, 0.95f}, 0.05f));
        scene.statics.materials.push_back(test::lambert({0.8f, 0.8f, 0.8f}));
        scene.frame.instances.push_back(
            {test::kSphereMesh, 1, test::place({-0.6f, 0.5f, -2}, test::kY, 0, {0.5f, 0.5f, 0.5f})});
        scene.frame.instances.push_back(
            {test::kSphereMesh, 2, test::place({0.6f, 0.5f, -2.3f}, test::kY, 0, {0.5f, 0.5f, 0.5f})});
        const Vec3 sun = modern::normalize(d.discDirection);
        // Facing the sun's azimuth so that it, the glow and the horizon are in the picture.
        const Vec3 forward = modern::normalize(Vec3{sun.x, 0, sun.z});
        scene.frame.camera = {{0, 1.2f, 0}, Vec3{0, 0.9f, 0} + forward * 3.f, {0, 1, 0}, 60.f};
        for (auto& instance : scene.frame.instances)
            if (instance.mesh == test::kSphereMesh)
                instance.transform =
                    test::place(Vec3{0, 0.5f, 0} + forward * 3.f +
                                    modern::normalize(modern::cross(forward, Vec3{0, 1, 0})) *
                                        (instance.material == 1 ? -0.7f : 0.7f),
                                test::kY, 0, {0.5f, 0.5f, 0.5f});
        Tracer tracer(ctx, scene, 640, 360);
        const double exposure = kind == EnvironmentKind::Day ? 1 : kind == EnvironmentKind::Dusk ? 2 : 25;
        const char* names[] = {"kind_night", "kind_space", "kind_dusk", "kind_day"};
        writeLook(tracer.trace(scene.frame, settings), exposure, dir, names[std::uint32_t(kind)]);
    }
}

} // namespace

// environment_test [cases]: runs the cases named by their letters ("0abcde", the default: all).
// environment_test --look <dir> [spp]: only writes the stills of renderLook.
int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
    const bool look = argc >= 3 && std::string(argv[1]) == "--look";
    const std::string cases = look ? "" : argc >= 2 ? argv[1] : "0abcde";
    auto runs = [&](char c) { return cases.find(c) != std::string::npos; };
    try {
        if (runs('0'))
            testDescriptions();
        auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the results are checked\n");
        else if (!ctx.syncValidationEnabled())
            std::printf("warning: synchronization validation unavailable\n");
        if (look)
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 16u);
        if (runs('a'))
            testSampling(ctx);
        if (runs('b'))
            testSunAndDome(ctx);
        if (runs('c'))
            testLightingHasNoStars(ctx);
        if (runs('d'))
            testStarFlux(ctx);
        if (runs('e'))
            testStudio(ctx);
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("environment_test: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
