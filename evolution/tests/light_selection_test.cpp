// Per-point light selection (Task 3 of step 5) on lavapipe, with the validation layer on; fails on any
// message.
//
// (a) the GPU (shaders/rt/lights.glsl through a compute probe) equals the CPU reference
//     (modern/light_select.hpp) within 1e-4 relative, for four lights (an emitter, a retro screen, a portal
//     and a moon gated by it) at 50 points; a point behind a light gives it no weight, a transmissive point
//     ignores its normal, and the screen's power follows the retro picture's mean (its 1x1 mip level);
// (b) pickLight's choices over 65536 stratified u equal lightSelectPdf within 1 %, its pdf is
//     lightSelectPdf's, the pdfs sum to 1, and a point where no light has weight picks nothing;
// (c) MIS stays unbiased: a Cornell box with three lights of powers 1 : 30 : 900 has the same mean with
//     NEE + MIS as with BSDF sampling alone, and its per-pixel variance is at most 0.6 of step 4's fixed
//     selection weights;
// (d) the Studio: at desk points the DOS screen (nearly black) gets under 5 % of the samples (step 4: 30 %),
//     an all-white picture over 20 %;
// (e) the Studio's moon is only chosen where the segment to it crosses the window.
//
// With --measure <dir> it renders the Studio at 1 spp with per-point and with fixed selection against a
// 256 spp reference and prints the RMSE over the frame and over the lamp-lit desk.

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/environment.hpp"
#include "modern/light_select.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene_gpu.hpp"
#include "modern/studio.hpp"
#include "render/retro_stage.hpp"
#include "rt/bindings.hpp"
#include "rt_test_scenes.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"
#include "text/text_pass.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <numbers>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;
using modern::Vec3;
using test::TestScene;

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

Vec3 load(const float v[3]) {
    return {v[0], v[1], v[2]};
}

Vec3 centreOf(const rt::RectLight& l) {
    return load(l.corner) + (load(l.edgeU) + load(l.edgeV)) * 0.5f;
}

void writeBuffer(gfx::Buffer& b, const void* data, std::size_t bytes) {
    std::memcpy(b.mapped(), data, bytes);
    b.flush();
}

// --- Retro pictures ----------------------------------------------------------------------------------------

struct Rgb8 {
    std::uint8_t r, g, b;
};

// A retro picture as the retro stage makes it: 320 x 200 R8G8B8A8_SRGB with every mip level. // setup
gfx::Image makeRetro(gfx::Context& ctx, const std::function<Rgb8(std::uint32_t x, std::uint32_t y)>& pixel) {
    gfx::Image image(
        ctx, {320, 200, VK_FORMAT_R8G8B8A8_SRGB,
              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
              gfx::mipLevelsFor(320, 200), "test retro picture"});
    gfx::Buffer staging(ctx, {320 * 200 * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "test retro staging"});
    auto* bytes = static_cast<std::uint8_t*>(staging.mapped());
    for (std::uint32_t y = 0; y < 200; ++y)
        for (std::uint32_t x = 0; x < 320; ++x) {
            const std::size_t i = std::size_t(y) * 320 + x;
            const Rgb8 c = pixel(x, y);
            bytes[4 * i] = c.r;
            bytes[4 * i + 1] = c.g;
            bytes[4 * i + 2] = c.b;
            bytes[4 * i + 3] = 255;
        }
    staging.flush();
    ctx.immediate([&](VkCommandBuffer cmd) { // setup
        image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {320, 200, 1};
        vkCmdCopyBufferToImage(cmd, staging.handle(), image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        image.generateMips(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    return image;
}

double srgbToLinear(double v) {
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

// --- The probe ---------------------------------------------------------------------------------------------

struct ProbePoint {
    float p[3];
    std::uint32_t transmissive;
    float n[3];
    float pad;
};
static_assert(sizeof(ProbePoint) == 32);

constexpr std::uint32_t kMaxLights = 8, kStride = 4 * kMaxLights;

struct PointResult {
    std::array<float, kMaxLights> importance{}, pdf{}, power{}, portalLuminance{};
};

// shaders/test/light_select_probe.comp: lights.glsl's selection with the path tracer's bindings.
class Probe {
public:
    Probe(gfx::Context& ctx, gfx::FrameRing& ring)
        : ctx_(ctx), ring_(ring),
          pipeline_(ctx, EVO_TEST_SPIRV_DIR "/test_light_select_probe.comp.spv", kBindings, 16) {}

    // The frame's lights at every point (mode 0).
    std::vector<PointResult> evaluate(std::span<const rt::RectLight> lights,
                                      std::span<const ProbePoint> points, gfx::Image& retro,
                                      bool fixed = false) {
        const std::vector<float> out = run(0, lights, points, retro, 1, fixed, points.size() * kStride);
        std::vector<PointResult> results(points.size());
        for (std::size_t k = 0; k < points.size(); ++k)
            for (std::size_t i = 0; i < lights.size(); ++i) {
                results[k].importance[i] = out[k * kStride + 2 * i];
                results[k].pdf[i] = out[k * kStride + 2 * i + 1];
                results[k].power[i] = out[k * kStride + 2 * kMaxLights + i];
                results[k].portalLuminance[i] = out[k * kStride + 3 * kMaxLights + i];
            }
        return results;
    }

    // pickLight((j + 0.5) / pickCount) at every point (mode 1): (index or -1, pdf) per pick, point-major.
    std::vector<float> pick(std::span<const rt::RectLight> lights, std::span<const ProbePoint> points,
                            gfx::Image& retro, std::uint32_t pickCount) {
        return run(1, lights, points, retro, pickCount, false, points.size() * pickCount * 2);
    }

private:
    // The tree's nodes (Task 12: lightImportance reads the root of a Tree light; these frames have none).
    static constexpr std::array<gfx::Binding, 7> kBindings = {
        gfx::Binding{EVO_BINDING_FRAME, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        gfx::Binding{EVO_BINDING_TREE_NODES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        gfx::Binding{EVO_BINDING_LIGHTS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        gfx::Binding{EVO_BINDING_RETRO_SCREEN, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
        gfx::Binding{EVO_BINDING_ENVIRONMENT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        gfx::Binding{27, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        gfx::Binding{28, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};

    std::vector<float> run(std::uint32_t mode, std::span<const rt::RectLight> lights,
                           std::span<const ProbePoint> points, gfx::Image& retro, std::uint32_t pickCount,
                           bool fixed, std::size_t floats) {
        if (lights.size() > kMaxLights)
            throw std::logic_error("Probe: too many lights");
        constexpr VkBufferUsageFlags kStorage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        rt::FrameUniforms u{};
        u.lightCount = static_cast<std::uint32_t>(lights.size());
        u.flags = rt::kTraceFlagNee | (fixed ? rt::kTraceFlagFixedLightSelection : 0u);
        u.sliceCount = 1;
        gfx::Buffer frame(ctx_, {sizeof u, kStorage, true, "probe frame"});
        writeBuffer(frame, &u, sizeof u);
        gfx::Buffer lightBuffer(ctx_, {std::max<std::size_t>(lights.size(), 1) * sizeof(rt::RectLight),
                                       kStorage, true, "probe lights"});
        if (!lights.empty())
            writeBuffer(lightBuffer, lights.data(), lights.size() * sizeof(rt::RectLight));
        // The Studio's environment (the default NightSky, Task 2): a portal's power is its lighting radiance.
        const rt::EnvironmentUniforms sky = modern::packEnvironment(modern::EnvironmentDesc{});
        gfx::Buffer environment(ctx_, {sizeof sky, kStorage, true, "probe environment"});
        writeBuffer(environment, &sky, sizeof sky);
        gfx::Buffer pointBuffer(ctx_, {points.size() * sizeof(ProbePoint), kStorage, true, "probe points"});
        writeBuffer(pointBuffer, points.data(), points.size() * sizeof(ProbePoint));
        gfx::Buffer output(ctx_, {floats * sizeof(float), kStorage, true, "probe results"});
        const rt::LightTreeNode noTree{};
        gfx::Buffer treeNodes(ctx_, {sizeof noTree, kStorage, true, "probe tree nodes"});
        writeBuffer(treeNodes, &noTree, sizeof noTree);

        VkCommandBuffer cmd = ring_.begin();
        retro.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        const auto whole = [](const gfx::Buffer& b) {
            return VkDescriptorBufferInfo{b.handle(), 0, VK_WHOLE_SIZE};
        };
        const std::array<gfx::DescriptorWrite, 7> writes = {
            gfx::DescriptorWrite::buffer(EVO_BINDING_FRAME, whole(frame)),
            gfx::DescriptorWrite::buffer(EVO_BINDING_TREE_NODES, whole(treeNodes)),
            gfx::DescriptorWrite::buffer(EVO_BINDING_LIGHTS, whole(lightBuffer)),
            gfx::DescriptorWrite::image(
                EVO_BINDING_RETRO_SCREEN,
                {ctx_.trilinearSampler(), retro.sampledView(), VK_IMAGE_LAYOUT_GENERAL}),
            gfx::DescriptorWrite::buffer(EVO_BINDING_ENVIRONMENT, whole(environment)),
            gfx::DescriptorWrite::buffer(27, whole(pointBuffer)),
            gfx::DescriptorWrite::buffer(28, whole(output))};
        pipeline_.bind(cmd, ring_, writes);
        const std::array<std::uint32_t, 4> push = {mode, static_cast<std::uint32_t>(points.size()), pickCount,
                                                   0};
        pipeline_.push(cmd, push.data(), sizeof push);
        const std::uint32_t invocations =
            static_cast<std::uint32_t>(mode == 0 ? points.size() : points.size() * pickCount);
        pipeline_.dispatch(cmd, invocations, 1, 64, 1);
        gfx::bufferBarrier2(cmd, output.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                            VK_ACCESS_2_HOST_READ_BIT);
        ring_.wait(ring_.submit());
        output.invalidate();
        std::vector<float> values(floats);
        std::memcpy(values.data(), output.mapped(), floats * sizeof(float));
        return values;
    }

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    gfx::ComputePipeline pipeline_;
};

ProbePoint probePoint(Vec3 p, Vec3 n, bool transmissive) {
    const Vec3 u = modern::normalize(n);
    return {{p.x, p.y, p.z}, transmissive ? 1u : 0u, {u.x, u.y, u.z}, 0.f};
}

// --- (a), (b): four lights around a small room ----------------------------------------------------------

// An emitter in the ceiling (y = 1, facing down), a retro screen on the back wall (z = -1, facing +z), a
// window portal in the right wall (x = 2, facing -x) and a moon outside it (x = 6, facing -x) gated by it.
std::vector<rt::RectLight> probeLights() {
    using test::place;
    return {modern::rectLightForQuad(place({0, 1, 0}, test::kX, 90, {1, 1, 1}), 1, 1, rt::LightKind::Emitter,
                                     {3, 2, 1}, 1.f),
            modern::rectLightForQuad(place({-0.3f, 0.4f, -1}, test::kY, 0, {0.8f, 0.6f, 1}), 1, 1,
                                     rt::LightKind::RetroScreen, {1.5f, 1.5f, 1.5f}, 0.7f),
            modern::rectLightForQuad(place({2, 0.3f, 0.2f}, test::kY, -90, {1, 1, 1}), 1, 1,
                                     rt::LightKind::Portal, {}, 1.f),
            [] {
                rt::RectLight moon =
                    modern::rectLightForQuad(place({6, 0.9f, 0.4f}, test::kY, -90, {0.3f, 0.3f, 1}), 1, 1,
                                             rt::LightKind::Emitter, {50, 50, 50}, 1.f);
                moon.gate = 2;
                return moon;
            }()};
}

// 50 points in the room: random positions and normals, a quarter transmissive, then a few on purpose.
std::vector<ProbePoint> probePoints() {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> x(-1.f, 1.6f), y(-0.5f, 0.9f), z(-0.8f, 1.f), s(-1.f, 1.f);
    std::vector<ProbePoint> points;
    while (points.size() < 44) {
        const Vec3 n{s(rng), s(rng), s(rng)};
        if (modern::length(n) < 0.2f)
            continue;
        points.push_back(probePoint({x(rng), y(rng), z(rng)}, n, points.size() % 4 == 3));
    }
    points.push_back(
        probePoint({0.1f, 1.3f, 0.1f}, {0, 1, 0}, false));             // 44: above (behind) the ceiling light
    points.push_back(probePoint({0.1f, 1.3f, 0.1f}, {0, 1, 0}, true)); // 45: the same, transmissive
    points.push_back(probePoint({0.f, 0.f, 0.f}, {0, 1, 0}, false));   // 46: facing the ceiling light
    points.push_back(probePoint({0.f, 0.f, 0.f}, {0, -1, 0}, false));  // 47: facing away: only the walls'
    points.push_back(probePoint({0.f, 0.f, 0.f}, {0, -1, 0}, true));   // 48: facing away but transmissive
    points.push_back(probePoint({1.f, 0.3f, -1.3f}, {0, 1, 0}, false)); // 49: behind the screen
    return points;
}

void testProbeAgainstReference(gfx::Context& ctx, gfx::FrameRing& ring, Probe& probe) {
    const auto start = std::chrono::steady_clock::now();
    const std::vector<rt::RectLight> lights = probeLights();
    const std::vector<ProbePoint> points = probePoints();
    // A picture with a known mean: the left half red, the right half blue, linear 1 each.
    gfx::Image picture = makeRetro(
        ctx, [](std::uint32_t x, std::uint32_t) { return x < 160 ? Rgb8{255, 0, 0} : Rgb8{0, 0, 255}; });
    const std::vector<PointResult> gpu = probe.evaluate(lights, points, picture);

    // The screen's power is emissionScale x the picture's mean luminance x its area (the 1x1 mip level).
    const rt::RectLight& screen = lights[1];
    const float screenLuminance = gpu[0].power[1] / (screen.emission[0] * screen.area);
    const double expectedMean = 0.5 * (0.2126 + 0.0722);
    std::printf("(a) screen: 1x1 mip luminance %.5f (picture mean %.5f)\n", screenLuminance, expectedMean);
    CHECK(std::abs(screenLuminance / expectedMean - 1) < 0.01);
    std::array<float, kMaxLights> portalLuminance{};
    for (std::size_t i = 0; i < lights.size(); ++i)
        portalLuminance[i] = gpu[0].portalLuminance[i];
    std::printf("    portal: environment luminance straight out %.5f\n", portalLuminance[2]);
    CHECK(portalLuminance[2] > 0.f && std::isfinite(portalLuminance[2]));
    CHECK(std::abs(gpu[0].power[2] - portalLuminance[2] * lights[2].area) <= 1e-6f * gpu[0].power[2]);
    CHECK(std::abs(gpu[0].power[0] - (0.2126f * 3 + 0.7152f * 2 + 0.0722f * 1) * lights[0].area) < 1e-5f);

    double worst = 0;
    int gatedOpen = 0, gatedShut = 0;
    for (std::size_t k = 0; k < points.size(); ++k) {
        const ProbePoint& q = points[k];
        const Vec3 p = load(q.p), n = load(q.n);
        const std::vector<float> want =
            modern::lightSelectPdfsReference(lights, p, n, q.transmissive != 0, screenLuminance,
                                             std::span<const float>(portalLuminance.data(), lights.size()));
        float sum = 0, total = 0;
        for (std::size_t i = 0; i < lights.size(); ++i) {
            const float ungated = modern::lightImportanceReference(lights[i], p, n, q.transmissive != 0,
                                                                   screenLuminance, portalLuminance[i]);
            float importance = ungated;
            if (lights[i].gate != rt::kNoLight &&
                !modern::segmentCrossesPortal(lights[lights[i].gate], p, centreOf(lights[i])))
                importance = 0.f;
            total += importance;
            const float got = gpu[k].importance[i];
            CHECK((got == 0.f) == (importance == 0.f));
            const double error = importance > 0 ? std::abs(got / importance - 1) : 0;
            const double pdfError = want[i] > 0 ? std::abs(gpu[k].pdf[i] / want[i] - 1) : gpu[k].pdf[i];
            worst = std::max({worst, error, pdfError});
            sum += gpu[k].pdf[i];
            if (lights[i].gate != rt::kNoLight && ungated > 0)
                ++(importance > 0 ? gatedOpen : gatedShut);
        }
        CHECK(total > 0 ? std::abs(sum - 1) < 1e-5f : sum == 0.f);
    }
    std::printf("    %zu points x %zu lights: worst relative difference %.2e; moon gated open at %d points, "
                "shut at %d (%.1f s)\n",
                points.size(), lights.size(), worst, gatedOpen, gatedShut, secondsSince(start));
    CHECK(worst < 1e-4);
    CHECK(gatedOpen > 0 && gatedShut > 0);
    // The points placed on purpose.
    CHECK(gpu[44].importance[0] == 0.f && gpu[45].importance[0] == 0.f); // behind the ceiling light
    CHECK(gpu[46].importance[0] > 0.f);
    CHECK(gpu[47].importance[0] == 0.f); // facing away from it
    CHECK(gpu[48].importance[0] > 0.f);  // transmissive: the normal plays no part
    CHECK(gpu[49].importance[1] == 0.f && gpu[49].importance[0] > 0.f); // behind the screen
    // Step 4's fixed weights on request: selectWeight / sum everywhere.
    const std::vector<PointResult> fixed = probe.evaluate(lights, points, picture, true);
    for (const PointResult& r : fixed)
        CHECK(std::abs(r.pdf[1] - 0.7f / 3.7f) < 1e-6f && r.importance[1] == 0.7f);
    (void)ring;
}

void testPickFrequencies(gfx::Context& ctx, Probe& probe) {
    const auto start = std::chrono::steady_clock::now();
    constexpr std::uint32_t kPicks = 65536;
    const std::vector<rt::RectLight> lights = probeLights();
    const std::vector<ProbePoint> all = probePoints();
    // Points where three or four lights have weight, one with a single light, one with none (above the
    // ceiling light, behind the screen and behind the window, where the moon's gate is shut).
    std::vector<ProbePoint> points = {all[0],  all[3],  all[10],
                                      all[46], all[48], probePoint({3, 1.5f, -1.5f}, {0, 1, 0}, false)};
    gfx::Image picture = makeRetro(ctx, [](std::uint32_t, std::uint32_t) { return Rgb8{200, 180, 160}; });
    const std::vector<PointResult> pdfs = probe.evaluate(lights, points, picture);
    const std::vector<float> picks = probe.pick(lights, points, picture, kPicks);
    double worst = 0;
    for (std::size_t k = 0; k < points.size(); ++k) {
        std::array<std::uint32_t, kMaxLights> counts{};
        std::uint32_t none = 0;
        for (std::uint32_t j = 0; j < kPicks; ++j) {
            const float index = picks[2 * (k * kPicks + j)], pdf = picks[2 * (k * kPicks + j) + 1];
            if (index < 0) {
                ++none;
                continue;
            }
            const auto i = static_cast<std::size_t>(index);
            CHECK(i < lights.size() && pdf > 0.f);
            CHECK(std::abs(pdf - pdfs[k].pdf[i]) <= 1e-6f * pdfs[k].pdf[i]); // lightSelectPdf's
            ++counts[i];
        }
        float sum = 0;
        for (std::size_t i = 0; i < lights.size(); ++i) {
            const double frequency = double(counts[i]) / kPicks, pdf = pdfs[k].pdf[i];
            sum += pdfs[k].pdf[i];
            CHECK(std::abs(frequency - pdf) <= 0.01 * pdf + 2.0 / kPicks);
            if (pdf > 0)
                worst = std::max(worst, std::abs(frequency / pdf - 1));
        }
        if (k + 1 < points.size()) {
            CHECK(none == 0 && std::abs(sum - 1) < 1e-5f);
        } else {
            CHECK(none == kPicks && sum == 0.f); // no light has weight there: pickLight returns false
        }
    }
    std::printf("(b) %zu points x %u picks: frequencies within %.3f %% of the pdf (%.1f s)\n", points.size(),
                kPicks, 100 * worst, secondsSince(start));
}

// --- (c): MIS with per-point selection --------------------------------------------------------------------

// The lighting image (RGBA32F) of one trace.
struct Lighting {
    std::uint32_t width = 0, height = 0;
    std::vector<float> rgba;
    double luminance(std::size_t pixel) const {
        return 0.2126 * rgba[4 * pixel] + 0.7152 * rgba[4 * pixel + 1] + 0.0722 * rgba[4 * pixel + 2];
    }
    std::size_t pixels() const { return std::size_t(width) * height; }
    double meanLuminance() const {
        double sum = 0;
        for (std::size_t i = 0; i < pixels(); ++i)
            sum += luminance(i);
        return sum / double(pixels());
    }
};

// A path tracer for one scene with its own frame ring (the scene's GPU side and the ring first: the tracer's
// TLAS waits for the ring's frames when it goes).
class Tracer {
public:
    Tracer(gfx::Context& ctx, const modern::SceneStatic& statics, std::uint32_t width, std::uint32_t height)
        : ctx_(ctx), gpu_(ctx, statics), ring_(ctx),
          tracer_(ctx, ring_, width, height, modern::tracerCapacity(statics.capacity)), width_(width),
          height_(height) {}

    Lighting trace(const modern::SceneFrame& frame, const modern::TraceSettings& settings,
                   gfx::Image* retro) {
        gfx::Buffer read(ctx_, {VkDeviceSize(width_) * height_ * 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                                "light selection test readback"});
        VkCommandBuffer cmd = ring_.begin();
        tracer_.trace(cmd, gpu_, frame, std::nullopt, settings, retro);
        gfx::Image& image = tracer_.gbuffer().lighting;
        image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width_, height_, 1};
        vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, read.handle(), 1,
                               &region);
        gfx::bufferBarrier2(cmd, read.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        ring_.wait(ring_.submit());
        read.invalidate();
        Lighting l{width_, height_, std::vector<float>(std::size_t(width_) * height_ * 4)};
        std::memcpy(l.rgba.data(), read.mapped(), l.rgba.size() * sizeof(float));
        return l;
    }

private:
    gfx::Context& ctx_;
    modern::SceneGpu gpu_;
    gfx::FrameRing ring_;
    modern::PathTracer tracer_;
    std::uint32_t width_, height_;
};

// The per-pixel variance of one picture, from two independent ones: E[(a - b)^2] / 2, on luminance.
double pixelVariance(const Lighting& a, const Lighting& b) {
    double sum = 0;
    for (std::size_t i = 0; i < a.pixels(); ++i) {
        const double d = a.luminance(i) - b.luminance(i);
        sum += d * d;
    }
    return 0.5 * sum / double(a.pixels());
}

// The Cornell box (rt_test_scenes.hpp) with three 0.5 x 0.5 ceiling lights instead of one, of powers
// 1 : 30 : 900 (emissions kThreeLightEmission), all selectWeight 1: step 4's fixed selection gives each a
// third of the samples wherever it is. The tall metal block is lowered to 0.6 m: with its top 0.8 m under a
// ceiling light, step 4's integrator gives the ceiling above it 10-20 % less two-bounce light with NEE than
// with BSDF sampling alone (about -1.3 % over test::cornellBox's frame at high spp, with one light, so the
// same with fixed and per-point selection: NEE's shadow rays hit the light they aimed at, since fixed, see
// path_tracer_test's close light). This test is about selection, so it keeps clear of that.
constexpr std::array<float, 3> kThreeLightEmission = {0.0125f, 0.375f, 11.25f};

TestScene threeLightCornell() {
    TestScene s = test::cornellBox();
    s.frame.instances.pop_back(); // the one light
    s.frame.instances[6].transform = test::place({-0.35f, 0.3f, -0.3f}, test::kY, 20, {0.55f, 0.6f, 0.55f});
    s.frame.lights.clear();
    const std::array<Vec3, 3> at = {Vec3{-0.55f, 1.995f, -0.5f}, Vec3{0.55f, 1.995f, -0.5f},
                                    Vec3{0.f, 1.995f, 0.5f}};
    for (std::uint32_t i = 0; i < 3; ++i) {
        const float e = kThreeLightEmission[i];
        const std::uint32_t material = static_cast<std::uint32_t>(s.statics.materials.size());
        s.statics.materials.push_back(test::emitter({e, e, e}));
        const rt::Mat34 t = test::place(at[i], test::kX, 90, {0.5f, 0.5f, 1});
        s.frame.instances.push_back({test::kQuadMesh, material, t, i});
        s.frame.lights.push_back(modern::rectLightForQuad(t, 1, 1, rt::LightKind::Emitter, {e, e, e}, 1.f));
    }
    return s;
}

void testCornellThreeLights(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    constexpr std::uint32_t kSize = 64;
    const TestScene scene = threeLightCornell();
    gfx::Image unused(ctx, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "unused retro"});
    Tracer tracer(ctx, scene.statics, kSize, kSize);
    // The luminance clamp after the first bounce is a deliberate bias: off.
    modern::TraceSettings settings{.spp = 32, .maxBounces = 4, .frameSeed = 41, .clampIndirect = 1e30f};
    double mean[2] = {0, 0}, variance[2] = {0, 0};
    for (const bool fixed : {false, true}) {
        settings.fixedLightSelection = fixed;
        settings.frameSeed = 41;
        const Lighting a = tracer.trace(scene.frame, settings, &unused);
        settings.frameSeed = 42;
        const Lighting b = tracer.trace(scene.frame, settings, &unused);
        mean[fixed] = 0.5 * (a.meanLuminance() + b.meanLuminance());
        variance[fixed] = pixelVariance(a, b);
    }
    settings.fixedLightSelection = false;
    settings.nee = false;
    settings.spp = 256; // BSDF sampling alone finds the small lights rarely
    settings.frameSeed = 43;
    const double bsdf = tracer.trace(scene.frame, settings, &unused).meanLuminance();
    std::printf(
        "(c) Cornell box, lights 1 : 30 : 900: mean %.5f per point, %.5f fixed, %.5f BSDF only (%.2f %%, "
        "%.2f %%); pixel variance per point %.3g, fixed %.3g (ratio %.3f) (%.1f s)\n",
        mean[0], mean[1], bsdf, 100 * (mean[0] / bsdf - 1), 100 * (mean[1] / bsdf - 1), variance[0],
        variance[1], variance[0] / variance[1], secondsSince(start));
    CHECK(mean[0] > 0.02);
    CHECK(std::abs(mean[0] / bsdf - 1) < 0.03);
    CHECK(std::abs(mean[1] / bsdf - 1) < 0.03);
    CHECK(std::abs(mean[0] / mean[1] - 1) < 0.01); // both NEE + MIS: far less noisy than BSDF sampling
    CHECK(variance[0] <= 0.6 * variance[1]);
}

// --- (d), (e): the Studio
// -----------------------------------------------------------------------------------

constexpr int kStudioNight = 11;
constexpr float kDeskTop = 0.75f; // studio.cpp's desk top

show::TimelineFrame chapterAt(int chapter, double fraction) {
    const double start = show::chapterStart(chapter);
    return show::sampleTimeline(start + fraction * (show::chapterStart(chapter + 1) - start));
}

// The retro stage's picture of a frame (the DOS prompt in STUDIO NIGHT), with its mip levels.
class RetroPicture {
public:
    explicit RetroPicture(gfx::Context& ctx) : ring_(ctx), text_(ctx, ring_), retro_(ctx, ring_, text_) {}
    gfx::Image& render(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        retro_.render(cmd, frame);
        ring_.wait(ring_.submit());
        return retro_.image();
    }
    gfx::FrameRing& ring() { return ring_; }

private:
    gfx::FrameRing ring_;
    text::TextPass text_;
    render::RetroStage retro_;
};

void testStudioScreen(gfx::Context& ctx, Probe& probe, gfx::Image& dos) {
    const auto start = std::chrono::steady_clock::now();
    const modern::Studio studio;
    const std::vector<rt::RectLight> lights = studio.frame(chapterAt(kStudioNight, 0.2)).lights;
    gfx::Image white = makeRetro(ctx, [](std::uint32_t, std::uint32_t) { return Rgb8{255, 255, 255}; });
    // On the desk: in front of the keyboard, right of it, and in the lamp's pool left of the monitor.
    const std::vector<ProbePoint> desk = {probePoint({0.f, kDeskTop + 5e-4f, -0.03f}, {0, 1, 0}, false),
                                          probePoint({0.22f, kDeskTop + 5e-4f, -0.1f}, {0, 1, 0}, false),
                                          probePoint({-0.3f, kDeskTop + 5e-4f, -0.2f}, {0, 1, 0}, false)};
    const std::vector<PointResult> dark = probe.evaluate(lights, desk, dos);
    const std::vector<PointResult> lit = probe.evaluate(lights, desk, white);
    const std::vector<PointResult> fixed = probe.evaluate(lights, desk, dos, true);
    const auto screen = modern::Studio::kScreenLight;
    const float dosLuminance = dark[0].power[screen] / (lights[screen].emission[0] * lights[screen].area);
    std::printf("(d) Studio desk, screen's selection probability (DOS mip luminance %.4f):\n", dosLuminance);
    for (std::size_t k = 0; k < desk.size(); ++k) {
        std::printf(
            "    point %zu: DOS %.2f %%, white %.2f %%, step 4 fixed %.2f %% (lamp %.2f / %.2f %%, "
            "moon %.2f %%, window %.3f %%)\n",
            k, 100 * dark[k].pdf[screen], 100 * lit[k].pdf[screen], 100 * fixed[k].pdf[screen],
            100 * dark[k].pdf[modern::Studio::kLampLight], 100 * lit[k].pdf[modern::Studio::kLampLight],
            100 * dark[k].pdf[modern::Studio::kMoonLight], 100 * dark[k].pdf[modern::Studio::kWindowLight]);
        CHECK(dark[k].pdf[screen] > 0.f && dark[k].pdf[screen] < 0.05f);
    }
    CHECK(lit[0].pdf[screen] > 0.2f);
    // Step 4's fixed selection is selectWeight / sum (a quarter now that the moon is a fourth light).
    float weightSum = 0.f;
    for (const rt::RectLight& l : lights)
        weightSum += std::max(l.selectWeight, 0.f);
    CHECK(std::abs(fixed[0].pdf[screen] - std::max(lights[screen].selectWeight, 0.f) / weightSum) < 1e-6f);
    (void)start;
}

void testStudioMoon(Probe& probe, gfx::Image& dos) {
    const auto start = std::chrono::steady_clock::now();
    const modern::Studio studio;
    const std::vector<rt::RectLight> lights = studio.frame(chapterAt(kStudioNight, 0.2)).lights;
    const rt::RectLight& moon = lights[modern::Studio::kMoonLight];
    const rt::RectLight& window = lights[modern::Studio::kWindowLight];
    CHECK(moon.gate == modern::Studio::kWindowLight);
    // A grid over the room at the floor, the desk and above it, facing up (the moon is above the horizon).
    std::vector<ProbePoint> points;
    for (float y : {0.01f, kDeskTop + 5e-4f, 1.4f})
        for (float x = -1.9f; x <= 1.9f; x += 0.2f)
            for (float z = -0.9f; z <= 2.8f; z += 0.25f)
                points.push_back(probePoint({x, y, z}, {0, 1, 0}, false));
    const std::vector<PointResult> r = probe.evaluate(lights, points, dos);
    int open = 0, shut = 0;
    for (std::size_t k = 0; k < points.size(); ++k) {
        const bool crosses = modern::segmentCrossesPortal(window, load(points[k].p), centreOf(moon));
        const float pdf = r[k].pdf[modern::Studio::kMoonLight];
        CHECK(crosses || pdf == 0.f); // never chosen where the window does not show it
        CHECK(!crosses || pdf > 0.f); // chosen where it does (every grid point faces up, in front of it)
        ++(crosses ? open : shut);
    }
    std::printf(
        "(e) Studio moon: chosen at the %d grid points whose segment to it crosses the window, never at "
        "the other %d (%.1f s)\n",
        open, shut, secondsSince(start));
    CHECK(open > 10 && shut > 10);
}

// --- --measure: the noise in the Studio -------------------------------------------------------------------

struct PixelPos {
    double x, y;
};

PixelPos project(const modern::Camera& camera, std::uint32_t width, std::uint32_t height, Vec3 p) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const Vec3 v = p - camera.eye;
    const double z = modern::dot(v, b.forward);
    const double tanHalf = std::tan(camera.fovYDegrees * std::numbers::pi / 360.0);
    const double ndcX = modern::dot(v, b.right) / z / (tanHalf * double(width) / height);
    const double ndcY = modern::dot(v, b.up) / z / tanHalf;
    return {(ndcX + 1) * 0.5 * width, (1 - ndcY) * 0.5 * height};
}

// RMSE of 1 spp traces against a 256 spp reference (the raw lighting image, no denoiser), per-point and fixed
// selection, over the whole frame and over a patch of the lamp's pool on the desk; relative to the
// reference's mean there.
void measureStudio(gfx::Context& ctx, const std::filesystem::path& dir) {
    constexpr std::uint32_t kW = 320, kH = 180, kRuns = 16;
    const modern::Studio studio;
    RetroPicture retro(ctx);
    struct Shot {
        const char* name;
        int chapter;
        double fraction;
    };
    for (const Shot& shot :
         {Shot{"night_wide", kStudioNight, 0.08}, Shot{"return_mid", 19, 0.5}, Shot{"finale_end", 9, 0.92}}) {
        const show::TimelineFrame t = chapterAt(shot.chapter, shot.fraction);
        gfx::Image& picture = retro.render(t);
        const modern::SceneFrame frame = studio.frame(t);
        Tracer tracer(ctx, studio.staticScene(), kW, kH);
        modern::TraceSettings settings{.spp = 256, .maxBounces = 4, .frameSeed = 7, .clampIndirect = 2.f};
        const Lighting reference = tracer.trace(frame, settings, &picture);
        const PixelPos aim = project(frame.camera, kW, kH, {-0.12f, kDeskTop, -0.16f}); // the lamp's aim
        auto inPatch = [&](std::size_t i) {
            const double x = double(i % kW) + 0.5, y = double(i / kW) + 0.5;
            return std::abs(x - aim.x) < 0.08 * kW && std::abs(y - aim.y) < 0.08 * kH;
        };
        double refMean[2] = {0, 0};
        std::size_t patchPixels = 0;
        for (std::size_t i = 0; i < reference.pixels(); ++i) {
            refMean[0] += reference.luminance(i);
            if (inPatch(i)) {
                refMean[1] += reference.luminance(i);
                ++patchPixels;
            }
        }
        refMean[0] /= double(reference.pixels());
        refMean[1] /= double(std::max<std::size_t>(patchPixels, 1));
        std::printf("%s: reference mean %.4f, lamp patch (%zu px around %.0f, %.0f) %.4f\n", shot.name,
                    refMean[0], patchPixels, aim.x, aim.y, refMean[1]);
        for (const bool fixed : {true, false}) {
            settings.fixedLightSelection = fixed;
            settings.spp = 1;
            double mse[2] = {0, 0};
            for (std::uint32_t run = 0; run < kRuns; ++run) {
                settings.frameSeed = 1000 + run;
                const Lighting l = tracer.trace(frame, settings, &picture);
                for (std::size_t i = 0; i < l.pixels(); ++i) {
                    const double d = l.luminance(i) - reference.luminance(i);
                    mse[0] += d * d;
                    if (inPatch(i))
                        mse[1] += d * d;
                }
                if (run == 0) {
                    std::vector<std::uint8_t> rgb(std::size_t(kW) * kH * 3);
                    for (std::size_t i = 0; i < l.pixels(); ++i)
                        for (int c = 0; c < 3; ++c) {
                            const double v = l.rgba[4 * i + c] * 8.0;
                            rgb[3 * i + c] =
                                std::uint8_t(std::clamp(std::pow(v / (1 + v), 1 / 2.2), 0.0, 1.0) * 255);
                        }
                    std::filesystem::create_directories(dir);
                    image::writePng(
                        dir / (std::string(shot.name) + (fixed ? "_fixed" : "_perpoint") + "_1spp.png"),
                        int(kW), int(kH), rgb.data());
                }
            }
            const double rmse = std::sqrt(mse[0] / (double(kRuns) * reference.pixels()));
            const double patch = std::sqrt(mse[1] / (double(kRuns) * std::max<std::size_t>(patchPixels, 1)));
            std::printf("  %-9s 1 spp: RMSE frame %.5f (%.1f %% of the mean), lamp patch %.5f (%.1f %%)\n",
                        fixed ? "fixed" : "per point", rmse, 100 * rmse / refMean[0], patch,
                        100 * patch / refMean[1]);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
    try {
        auto ctx = gfx::Context::create({.validation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the values are checked\n");
        if (argc >= 3 && std::string(argv[1]) == "--measure") {
            measureStudio(ctx, argv[2]);
            CHECK(ctx.validationMessageCount() == 0);
            return 0;
        }
        {
            RetroPicture retro(ctx);
            gfx::Image& dos = retro.render(chapterAt(kStudioNight, 0.2));
            Probe probe(ctx, retro.ring());
            testProbeAgainstReference(ctx, retro.ring(), probe);
            testPickFrequencies(ctx, probe);
            testStudioScreen(ctx, probe, dos);
            testStudioMoon(probe, dos);
            retro.ring().waitAll();
        }
        testCornellThreeLights(ctx);
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("light_selection_test: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
