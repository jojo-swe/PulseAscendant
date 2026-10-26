// The integrator v2 on lavapipe, with the validation layer on; fails on any message.
// 64x64 unless noted.
//
// (a) path regularisation: in a mirror caustic box (diffuse floor, small lamp, smooth mirror) roughening
// every
//     lobe after the first rough vertex to 0.25 cuts the speckle fraction (pixels over 10x their 3x3 median)
//     at least 5x at 16 spp and moves the floor's mean by under 25 % (the bias, documented); it never touches
//     the first vertex (maxBounces 1: the same bytes with and without), and NEE + MIS against BSDF sampling
//     alone still agree with it on (a Cornell box with a mirror block);
// (b) the clamp after a rough vertex only: camera -> chrome sphere -> a lamp of 500 reads >= 400, and so does
//     camera -> glass slab (two delta events) -> lamp, with clampIndirect 16; diffuse -> lamp contributions
//     above 16 are clamped;
// (c) camera-only emission: an LED of 1000 shows 1000 seen directly and through a mirror, while everything
//     else (a lit desk patch beside it, and every pixel off the LED's mirror image) is byte-identical with
//     the LED at 1000 and at 0; a light whose surface is camera-only is refused;
// (d) dispersion: through a flint prism (Abbe 30) a white slit splits, the red centroid >= 3 px from the
//     blue; a parallel slab keeps white white (rgb ratios within 2 %); a white furnace through dispersive
//     glass stays 1 per channel (energy kept); with Abbe 0 every pixel is grey (r = g = b exactly); the hero
//     wavelength is a pure function of the seed;
// (e) spectralRgb is non-negative with a mean of (1, 1, 1) over 380-780 nm on the CPU and on the GPU (a
//     compute probe), and dispersiveIor gives back the IOR at 587.6 nm and the Abbe number.

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene_gpu.hpp"
#include "rt/spectral.hpp"
#include "rt_test_scenes.hpp"
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
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;
using modern::TraceSettings;
using modern::Vec3;
using test::TestScene;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr std::uint32_t kSize = 64;

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
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

// One RGBA image read back: its bytes and its values as floats.
struct Plane {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> bytes;
    std::vector<float> values;
    float at(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return values[(std::size_t(y) * width + x) * 4 + c];
    }
    double luminance(std::uint32_t x, std::uint32_t y) const {
        return 0.2126 * at(x, y, 0) + 0.7152 * at(x, y, 1) + 0.0722 * at(x, y, 2);
    }
    // The pixel's bytes equal another plane's.
    bool sameBytes(const Plane& o, std::uint32_t x, std::uint32_t y) const {
        const std::size_t stride = bytes.size() / (std::size_t(width) * height);
        const std::size_t i = (std::size_t(y) * width + x) * stride;
        return std::memcmp(&bytes[i], &o.bytes[i], stride) == 0;
    }
};

struct Frame {
    Plane lighting, emission, albedo;
};

// A path tracer for one scene with its own frame ring (declared first: the tracer's TLAS waits for it).
class Tracer {
public:
    Tracer(gfx::Context& ctx, const TestScene& scene, std::uint32_t width = kSize,
           std::uint32_t height = kSize)
        : ctx_(ctx), gpu_(ctx, scene.statics), ring_(ctx),
          tracer_(ctx, ring_, width, height, modern::tracerCapacity(scene.statics.capacity)), width_(width),
          height_(height),
          unused_(ctx, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "unused retro"}) {}

    Frame trace(const modern::SceneFrame& frame, const TraceSettings& settings) {
        VkCommandBuffer cmd = ring_.begin();
        tracer_.trace(cmd, gpu_, frame, std::nullopt, settings, &unused_);
        modern::GBuffer& g = tracer_.gbuffer();
        std::array<gfx::Image*, 3> images{&g.lighting, &g.emission, &g.albedo};
        std::vector<gfx::Buffer> reads;
        for (gfx::Image* image : images) {
            const std::uint32_t bpp = image->format() == VK_FORMAT_R32G32B32A32_SFLOAT ? 16 : 8;
            reads.emplace_back(ctx_, gfx::BufferDesc{VkDeviceSize(width_) * height_ * bpp,
                                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                                                     "integrator readback"});
            image->transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                              VK_ACCESS_2_TRANSFER_READ_BIT);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {width_, height_, 1};
            vkCmdCopyImageToBuffer(cmd, image->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   reads.back().handle(), 1, &region);
            gfx::bufferBarrier2(cmd, reads.back().handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                VK_ACCESS_2_HOST_READ_BIT);
        }
        ring_.wait(ring_.submit());
        idle(ctx_);
        std::array<Plane, 3> planes;
        for (std::size_t k = 0; k < 3; ++k) {
            reads[k].invalidate();
            Plane& p = planes[k];
            p.width = width_;
            p.height = height_;
            const auto* bytes = static_cast<const std::uint8_t*>(reads[k].mapped());
            p.bytes.assign(bytes, bytes + reads[k].size());
            p.values.resize(std::size_t(width_) * height_ * 4);
            if (images[k]->format() == VK_FORMAT_R32G32B32A32_SFLOAT) {
                std::memcpy(p.values.data(), bytes, p.values.size() * sizeof(float));
            } else {
                for (std::size_t i = 0; i < p.values.size(); ++i) {
                    std::uint16_t h;
                    std::memcpy(&h, bytes + 2 * i, 2);
                    p.values[i] = halfToFloat(h);
                }
            }
        }
        return {std::move(planes[0]), std::move(planes[1]), std::move(planes[2])};
    }

    modern::PathTracer& pathTracer() { return tracer_; }
    gfx::FrameRing& ring() { return ring_; }
    const modern::SceneGpu& scene() const { return gpu_; }
    gfx::Image& unused() { return unused_; }

private:
    gfx::Context& ctx_;
    modern::SceneGpu gpu_;
    gfx::FrameRing ring_;
    modern::PathTracer tracer_;
    std::uint32_t width_, height_;
    gfx::Image unused_;
};

// Where a world point lands in the picture (continuous pixel coordinates, centres at +0.5).
struct PixelPos {
    double x, y;
};
PixelPos project(const modern::Camera& camera, Vec3 p) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const Vec3 v = p - camera.eye;
    const double z = modern::dot(v, b.forward);
    const double tanHalf = std::tan(test::radians(camera.fovYDegrees) * 0.5);
    const double ndcX = modern::dot(v, b.right) / z / tanHalf;
    const double ndcY = modern::dot(v, b.up) / z / tanHalf;
    return {(ndcX + 1) * 0.5 * kSize, (1 - ndcY) * 0.5 * kSize};
}

std::uint8_t encode(float linear) {
    const float v = std::pow(std::clamp(linear / (1.f + linear), 0.f, 1.f), 1.f / 2.2f); // for looking only
    return static_cast<std::uint8_t>(v * 255.f + 0.5f);
}

// lighting + emission, Reinhard and gamma, to <dir>/integrator_<name>.png (<EVO_TEST_OUT_DIR>/step5 by
// default).
void writePicture(const Frame& f, const std::string& name,
                  const std::filesystem::path& dir = std::filesystem::path(EVO_TEST_OUT_DIR) / "step5") {
    std::filesystem::create_directories(dir);
    const std::uint32_t w = f.lighting.width, h = f.lighting.height;
    std::vector<std::uint8_t> rgb(std::size_t(w) * h * 3);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            for (std::uint32_t c = 0; c < 3; ++c)
                rgb[(std::size_t(y) * w + x) * 3 + c] =
                    encode(f.lighting.at(x, y, c) + f.emission.at(x, y, c));
    image::writePng(dir / ("integrator_" + name + ".png"), int(w), int(h), rgb.data());
}

// --- (a) Regularisation ------------------------------------------------------------------------------------

// The share of pixels brighter than 10x the median of their 3x3 neighbourhood, among the interior pixels
// whose median is at least `floor` (the lit part of the picture: in its near-black corners any indirect path
// is 10x the median).
double speckleFraction(const Plane& p, double floor) {
    std::size_t speckles = 0, counted = 0;
    for (std::uint32_t y = 1; y + 1 < kSize; ++y)
        for (std::uint32_t x = 1; x + 1 < kSize; ++x) {
            std::array<double, 9> n{};
            for (int k = 0; k < 9; ++k)
                n[k] = p.luminance(x + k % 3 - 1, y + k / 3 - 1);
            std::nth_element(n.begin(), n.begin() + 4, n.end());
            if (!(n[4] >= floor))
                continue;
            ++counted;
            speckles += p.luminance(x, y) > 10 * n[4] ? 1 : 0;
        }
    return counted ? double(speckles) / double(counted) : 0.0;
}

// The mean lighting over the pixels whose primary hit is the floor (albedo 0.8).
double floorMean(const Frame& f) {
    double sum = 0;
    std::size_t count = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x)
            if (std::abs(f.albedo.at(x, y, 1) - 0.8f) < 1e-3f) {
                sum += f.lighting.luminance(x, y);
                ++count;
            }
    return count ? sum / double(count) : 0.0;
}

double meanLuminance(const Plane& p) {
    double sum = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x)
            sum += p.luminance(x, y);
    return sum / double(kSize * kSize);
}

void testRegularisation(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    {
        const TestScene scene = test::causticBox();
        Tracer tracer(ctx, scene);
        // The clamp is off: this is about regularisation alone.
        TraceSettings plain{.spp = 16, .maxBounces = 4, .frameSeed = 3, .clampIndirect = 1e30f};
        plain.regularizeRoughness = 0.f;
        TraceSettings regular = plain;
        regular.regularizeRoughness = 0.25f;
        const Frame a = tracer.trace(scene.frame, plain);
        const Frame b = tracer.trace(scene.frame, regular);
        writePicture(a, "caustic_box_plain");
        writePicture(b, "caustic_box_regularised");
        const double lit = 0.1 * meanLuminance(a.lighting);
        const double speckleA = speckleFraction(a.lighting, lit), speckleB = speckleFraction(b.lighting, lit);
        // The means at 64 spp (the unregularised caustic is too noisy at 16 for a mean).
        plain.spp = regular.spp = 64;
        const double meanA = floorMean(tracer.trace(scene.frame, plain));
        const double meanB = floorMean(tracer.trace(scene.frame, regular));
        std::printf(
            "(a) caustic box at 16 spp: speckles %.3f %% plain, %.3f %% regularised (%.1fx); floor mean "
            "%.4f -> %.4f (%+.1f %%)\n",
            100 * speckleA, 100 * speckleB, speckleA / std::max(speckleB, 1e-9), meanA, meanB,
            100 * (meanB / meanA - 1));
        CHECK(speckleA > 0.003);
        CHECK(speckleA >= 5 * speckleB);
        CHECK(meanA > 0.05 && std::abs(meanB / meanA - 1) < 0.25);

        // The first vertex is never regularised: with one bounce nothing changes, byte for byte.
        plain.spp = regular.spp = 4;
        plain.maxBounces = regular.maxBounces = 1;
        const Frame one = tracer.trace(scene.frame, plain);
        const Frame oneRegular = tracer.trace(scene.frame, regular);
        CHECK(one.lighting.bytes == oneRegular.lighting.bytes);
        CHECK(meanLuminance(one.lighting) > 0.05);
    }
    {
        // MIS with regularisation: a Cornell box whose block is a smooth mirror (regularised after the first
        // rough vertex), NEE + MIS against BSDF sampling alone, clamp off.
        TestScene scene = test::cornellBox();
        scene.statics.materials[4] = test::metal({0.9f, 0.9f, 0.9f}, 0.f);
        Tracer tracer(ctx, scene);
        TraceSettings settings{.spp = 64, .maxBounces = 4, .frameSeed = 11, .clampIndirect = 1e30f};
        settings.regularizeRoughness = 0.25f;
        double withNee = 0, without = 0;
        for (const std::uint32_t seed : {11u, 12u}) {
            settings.frameSeed = seed;
            settings.nee = true;
            withNee += 0.5 * meanLuminance(tracer.trace(scene.frame, settings).lighting);
            settings.nee = false;
            without += 0.5 * meanLuminance(tracer.trace(scene.frame, settings).lighting);
        }
        std::printf("(a) mirror Cornell box, regularised: mean %.4f with NEE, %.4f without (%+.2f %%)\n",
                    withNee, without, 100 * (withNee / without - 1));
        CHECK(withNee > 0.05 && std::abs(withNee / without - 1) < 0.03);
    }
    std::printf("(a) done (%.1f s)\n", secondsSince(start));
}

// --- (b) The clamp
// ------------------------------------------------------------------------------------------

// The mean luminance of the lighting over the 4x4 pixels around the picture's centre.
double centreLighting(const Frame& f) {
    double sum = 0;
    for (std::uint32_t y = kSize / 2 - 2; y < kSize / 2 + 2; ++y)
        for (std::uint32_t x = kSize / 2 - 2; x < kSize / 2 + 2; ++x)
            sum += f.lighting.luminance(x, y);
    return sum / 16;
}

void testClamp(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    const TraceSettings defaults{.spp = 8, .frameSeed = 2};
    CHECK(defaults.clampIndirect == 16.f && defaults.regularizeRoughness == 0.25f);
    CHECK(modern::SceneFrame{}.clampIndirect == 16.f);
    double chrome = 0, slab = 0;
    {
        const TestScene scene = test::chromeScene(500);
        Tracer tracer(ctx, scene);
        chrome = centreLighting(tracer.trace(scene.frame, defaults));
    }
    {
        const TestScene scene = test::slabLampScene(500);
        Tracer tracer(ctx, scene);
        slab = centreLighting(tracer.trace(scene.frame, defaults));
    }
    std::printf("(b) a lamp of 500 after delta events: %.1f off chrome, %.1f through a glass slab\n", chrome,
                slab);
    CHECK(chrome >= 400 && chrome <= 500);
    CHECK(slab >= 400 && slab <= 500);

    // Diffuse -> lamp: BSDF sampling alone, one bounce, so each sample is one contribution of 0.8 * 500 or 0.
    TestScene scene = test::analyticLightScene(test::lambert({0.8f, 0.8f, 0.8f}));
    scene.statics.materials[2] = test::emitter({500, 500, 500});
    for (float& e : scene.frame.lights[0].emission)
        e = 500;
    Tracer tracer(ctx, scene);
    TraceSettings settings{.spp = 8, .maxBounces = 1, .frameSeed = 4, .nee = false};
    const Frame clamped = tracer.trace(scene.frame, settings);
    settings.clampIndirect = 1e30f;
    const Frame open = tracer.trace(scene.frame, settings);
    double brightestClamped = 0, brightestOpen = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            brightestClamped = std::max(brightestClamped, clamped.lighting.luminance(x, y));
            brightestOpen = std::max(brightestOpen, open.lighting.luminance(x, y));
        }
    std::printf(
        "(b) diffuse -> lamp of 500, BSDF only: brightest pixel %.2f clamped at 16, %.2f open (%.1f s)\n",
        brightestClamped, brightestOpen, secondsSince(start));
    CHECK(brightestOpen > 16);
    CHECK(brightestClamped > 1 && brightestClamped <= 16.001);
}

// --- (c) Camera-only emission ------------------------------------------------------------------------------

void testCameraOnly(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    const TestScene on = test::ledScene(1000);
    modern::SceneFrame off = on.frame;
    off.materials = test::ledScene(0).statics.materials;
    const TraceSettings settings{.spp = 8, .frameSeed = 9};
    Frame lit, dark;
    {
        Tracer tracer(ctx, on);
        lit = tracer.trace(on.frame, settings);
        dark = tracer.trace(off, settings);
    }
    writePicture(lit, "led");
    const modern::Camera& camera = on.frame.camera;
    // Directly: the primary hit's emission.
    const PixelPos direct = project(camera, test::kLedCentre);
    CHECK(lit.emission.at(std::uint32_t(direct.x), std::uint32_t(direct.y), 1) == 1000.f);
    // Through the mirror floor: its image below the floor, after one delta event (unclamped).
    const Vec3 image{test::kLedCentre.x, -test::kLedCentre.y, test::kLedCentre.z};
    const PixelPos mirrored = project(camera, image);
    const float throughMirror = lit.lighting.at(std::uint32_t(mirrored.x), std::uint32_t(mirrored.y), 1);
    std::printf("(c) LED of 1000: %.1f directly, %.1f in the mirror\n",
                lit.emission.at(std::uint32_t(direct.x), std::uint32_t(direct.y), 1), throughMirror);
    CHECK(throughMirror > 990.f && throughMirror < 1001.f);

    // The lit desk patch beside it: the same bytes with the LED at 1000 and at 0.
    const PixelPos desk = project(camera, test::kDeskFront);
    CHECK(meanLuminance(lit.lighting) > 0);
    double deskLight = 0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const auto x = std::uint32_t(desk.x + dx), y = std::uint32_t(desk.y + dy);
            CHECK(lit.lighting.sameBytes(dark.lighting, x, y));
            deskLight += lit.lighting.luminance(x, y) / 25;
        }
    CHECK(deskLight > 0.01);
    // And every pixel off the LED's mirror image (its corners' bounding box, 2 px wider).
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (const float sx : {-0.5f, 0.5f})
        for (const float sy : {-0.5f, 0.5f}) {
            const PixelPos c = project(camera, image + Vec3{sx * test::kLedSize, sy * test::kLedSize, 0});
            x0 = std::min(x0, c.x), x1 = std::max(x1, c.x), y0 = std::min(y0, c.y), y1 = std::max(y1, c.y);
        }
    std::size_t differing = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const bool inImage = x + 2 >= x0 && x <= x1 + 2 && y + 2 >= y0 && y <= y1 + 2;
            if (!inImage)
                CHECK(lit.lighting.sameBytes(dark.lighting, x, y));
            differing += lit.lighting.sameBytes(dark.lighting, x, y) ? 0 : 1;
        }
    CHECK(differing > 0);

    // A light whose surface's emission is camera-only would be sampled by NEE: refused.
    TestScene asLight = test::ledScene(1000);
    asLight.frame.instances[2].light = 1;
    asLight.frame.lights.push_back(modern::rectLightForQuad(asLight.frame.instances[2].transform, 1, 1,
                                                            rt::LightKind::Emitter, {1000, 1000, 1000}, 1.f));
    bool refused = false;
    try {
        modern::checkSceneFrame(asLight.statics, asLight.frame);
    } catch (const std::invalid_argument& error) {
        std::printf("  refused: %s\n", error.what());
        refused = true;
    }
    CHECK(refused);
    std::printf("(c) %zu pixels differ, all in the LED's mirror image (%.1f s)\n", differing,
                secondsSince(start));
}

// --- (d) Dispersion ----------------------------------------------------------------------------------------

// The x centroid of a channel of the lighting over columns x0 and on.
double centroidX(const Plane& p, std::uint32_t c, std::uint32_t x0) {
    double sum = 0, weighted = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = x0; x < kSize; ++x) {
            sum += p.at(x, y, c);
            weighted += p.at(x, y, c) * (x + 0.5);
        }
    return weighted / sum;
}

std::array<double, 3> meanRgb(const Plane& p, std::uint32_t x0, std::uint32_t x1, std::uint32_t y0,
                              std::uint32_t y1) {
    std::array<double, 3> m{};
    for (std::uint32_t y = y0; y < y1; ++y)
        for (std::uint32_t x = x0; x < x1; ++x)
            for (std::uint32_t c = 0; c < 3; ++c)
                m[c] += p.at(x, y, c) / (double(x1 - x0) * (y1 - y0));
    return m;
}

bool allGrey(const Plane& p) {
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x)
            if (p.at(x, y, 0) != p.at(x, y, 1) || p.at(x, y, 1) != p.at(x, y, 2))
                return false;
    return true;
}

void testDispersion(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    // One tracer per scene: Abbe 0 comes as the frame's own material table (SceneFrame::materials).
    const TraceSettings settings{.spp = 16, .maxBounces = 4, .frameSeed = 5};
    {
        const TestScene prism = test::prismScene(30);
        modern::SceneFrame plain = prism.frame;
        plain.materials = test::prismScene(0).statics.materials;
        Tracer tracer(ctx, prism);
        const Frame split = tracer.trace(prism.frame, settings);
        const Frame again = tracer.trace(prism.frame, settings);
        TraceSettings reseeded = settings;
        reseeded.frameSeed = 6;
        const Frame other = tracer.trace(prism.frame, reseeded);
        const Frame grey = tracer.trace(plain, settings);
        writePicture(split, "prism_abbe30");
        writePicture(grey, "prism_abbe0");
        // The slit's image through the two faces; a second one at the left edge (rays that also reflect off
        // the prism's base) is left out.
        constexpr std::uint32_t kFrom = 12;
        const double red = centroidX(split.lighting, 0, kFrom), green = centroidX(split.lighting, 1, kFrom),
                     blue = centroidX(split.lighting, 2, kFrom);
        std::printf(
            "(d) prism, Abbe 30: slit centroids red %.2f, green %.2f, blue %.2f px; Abbe 0: %.2f px\n", red,
            green, blue, centroidX(grey.lighting, 1, kFrom));
        CHECK(std::abs(red - blue) >= 3);
        CHECK((red - green) * (green - blue) > 0); // the spectrum in order
        CHECK(meanLuminance(grey.lighting) > 0.1);
        CHECK(allGrey(grey.lighting));
        CHECK(!allGrey(split.lighting));
        CHECK(split.lighting.bytes ==
              again.lighting.bytes); // the hero wavelength: a pure function of the seed
        CHECK(split.lighting.bytes != other.lighting.bytes);
    }
    {
        const TestScene slab = test::flintSlabScene(30);
        Tracer tracer(ctx, slab);
        TraceSettings more = settings;
        more.spp = 64;
        const Frame f = tracer.trace(slab.frame, more);
        writePicture(f, "flint_slab");
        const std::array<double, 3> m = meanRgb(f.lighting, 0, kSize, 0, kSize);
        std::printf("(d) parallel flint slab: rgb %.4f %.4f %.4f\n", m[0], m[1], m[2]);
        CHECK(m[1] > 0.5);
        CHECK(std::abs(m[0] / m[1] - 1) < 0.02 && std::abs(m[2] / m[1] - 1) < 0.02);
    }
    const TestScene furnace = test::dispersiveFurnace(30);
    Tracer tracer(ctx, furnace);
    for (const float abbe : {30.f, 0.f}) {
        modern::SceneFrame frame = furnace.frame;
        frame.materials = test::dispersiveFurnace(abbe).statics.materials;
        TraceSettings deep = settings;
        deep.spp = 32;
        deep.maxBounces = 8;
        const Frame f = tracer.trace(frame, deep);
        const std::array<double, 3> m = meanRgb(f.lighting, 0, kSize, 0, kSize);
        std::printf("(d) white furnace through flint glass, Abbe %.0f: rgb %.4f %.4f %.4f\n", abbe, m[0],
                    m[1], m[2]);
        for (const double v : m)
            CHECK(std::abs(v - 1) < 0.015);
    }
    std::printf("(d) done (%.1f s)\n", secondsSince(start));
}

// --- (e) spectralRgb ---------------------------------------------------------------------------------------

class SpectralProbe {
public:
    explicit SpectralProbe(gfx::Context& ctx)
        : ctx_(ctx),
          pipeline_(ctx, std::filesystem::path(EVO_TEST_SPIRV_DIR) / "test_spectral_probe.comp.spv",
                    kBindings, sizeof(Push)) {}

    // spectralRgb(lambda) and dispersiveIor(ior, abbe, lambda) per wavelength.
    std::vector<std::array<float, 4>> run(const std::vector<float>& lambdas, float ior, float abbe) {
        const auto n = static_cast<std::uint32_t>(lambdas.size());
        gfx::Buffer in(ctx_,
                       {sizeof(float) * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "spectral inputs"});
        std::memcpy(in.mapped(), lambdas.data(), sizeof(float) * n);
        in.flush();
        gfx::Buffer out(
            ctx_, {sizeof(float) * 4 * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "spectral results"});
        const std::array<VkDescriptorBufferInfo, 2> infos{
            {{in.handle(), 0, VK_WHOLE_SIZE}, {out.handle(), 0, VK_WHOLE_SIZE}}};
        {
            gfx::FrameRing ring(ctx_);
            VkCommandBuffer cmd = ring.begin();
            pipeline_.bind(cmd, ring, {}, infos);
            const Push push{n, ior, abbe};
            pipeline_.push(cmd, &push, sizeof push);
            pipeline_.dispatch(cmd, n, 1, 64, 1);
            gfx::bufferBarrier2(cmd, out.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                VK_ACCESS_2_HOST_READ_BIT);
            ring.wait(ring.submit());
        }
        idle(ctx_);
        out.invalidate();
        std::vector<std::array<float, 4>> r(n);
        std::memcpy(r.data(), out.mapped(), sizeof(float) * 4 * n);
        return r;
    }

private:
    struct Push {
        std::uint32_t count;
        float ior, abbe;
    };
    static constexpr std::array<gfx::Binding, 2> kBindings{
        {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}}};
    gfx::Context& ctx_;
    gfx::ComputePipeline pipeline_;
};

void testSpectral(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    // The CPU: a midpoint rule over 380-780 nm.
    constexpr int kSteps = 100000;
    std::array<double, 3> mean{};
    float least = 1e30f;
    for (int i = 0; i < kSteps; ++i) {
        const float lambda = rt::kSpectralMinNm + rt::kSpectralRangeNm * (float(i) + 0.5f) / kSteps;
        const std::array<float, 3> c = rt::spectralRgb(lambda);
        for (int k = 0; k < 3; ++k) {
            mean[k] += c[k] / double(kSteps);
            least = std::min(least, c[k]);
        }
    }
    std::printf("(e) spectralRgb mean on the CPU %.6f %.6f %.6f, least %.3g\n", mean[0], mean[1], mean[2],
                least);
    for (const double m : mean)
        CHECK(std::abs(m - 1) < 1e-3);
    CHECK(least >= 0.f);
    // Red at long wavelengths, blue at short, green between.
    const auto r650 = rt::spectralRgb(650), g540 = rt::spectralRgb(540), b450 = rt::spectralRgb(450);
    CHECK(r650[0] > 4 * r650[2] && r650[0] > r650[1]);
    CHECK(g540[1] > g540[0] && g540[1] > g540[2]);
    CHECK(b450[2] > 4 * b450[0] && b450[2] > b450[1]);

    // Cauchy fitted to the IOR at the d line and the Abbe number.
    const float nd = rt::dispersiveIor(1.6f, 30.f, 587.6f), nF = rt::dispersiveIor(1.6f, 30.f, 486.1f),
                nC = rt::dispersiveIor(1.6f, 30.f, 656.3f);
    CHECK(std::abs(nd - 1.6f) < 1e-5f);
    CHECK(std::abs((nd - 1) / (nF - nC) - 30.f) < 0.01f);
    CHECK(rt::dispersiveIor(1.6f, 0.f, 400.f) == 1.6f); // Abbe 0: no dispersion

    // The GPU: the same function at 4000 wavelengths.
    SpectralProbe probe(ctx);
    constexpr int kGpuSteps = 4000;
    std::vector<float> lambdas(kGpuSteps);
    for (int i = 0; i < kGpuSteps; ++i)
        lambdas[i] = rt::kSpectralMinNm + rt::kSpectralRangeNm * (float(i) + 0.5f) / kGpuSteps;
    const std::vector<std::array<float, 4>> gpu = probe.run(lambdas, 1.6f, 30.f);
    std::array<double, 3> gpuMean{};
    double worst = 0;
    for (int i = 0; i < kGpuSteps; ++i) {
        const std::array<float, 3> cpu = rt::spectralRgb(lambdas[i]);
        for (int k = 0; k < 3; ++k) {
            gpuMean[k] += gpu[i][k] / double(kGpuSteps);
            worst = std::max(worst, double(std::abs(gpu[i][k] - cpu[k])));
        }
        worst = std::max(worst, double(std::abs(gpu[i][3] - rt::dispersiveIor(1.6f, 30.f, lambdas[i]))));
    }
    std::printf(
        "(e) spectralRgb mean on the GPU %.6f %.6f %.6f; worst difference from the CPU %.2g (%.1f s)\n",
        gpuMean[0], gpuMean[1], gpuMean[2], worst, secondsSince(start));
    for (const double m : gpuMean)
        CHECK(std::abs(m - 1) < 1e-3);
    CHECK(worst < 1e-4);
}

// --- --look ------------------------------------------------------------------------------------------------

// A still to look at: the look scene of path_tracer_test (an open Cornell box, checker floor, chrome, coated
// wood) with its glass sphere made of dispersive flint (IOR 1.6, Abbe 20) and a flint prism standing on the
// floor, at 320x180.
void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp) {
    TestScene scene = test::lookScene();
    rt::Material flint = test::glass(1.6f, 0);
    flint.abbe = 20;
    scene.statics.materials.push_back(flint);
    const auto flintIndex = static_cast<std::uint32_t>(scene.statics.materials.size() - 1);
    for (modern::SceneInstance& in : scene.frame.instances)
        if (in.mesh == test::kSphereMesh && scene.statics.materials[in.material].transmission > 0.5f)
            in.material = flintIndex;
    scene.statics.meshes.push_back(test::prismMesh(0.5f));
    scene.frame.instances.push_back({static_cast<std::uint32_t>(scene.statics.meshes.size() - 1), flintIndex,
                                     test::place({0.05f, 0.25f, 0.6f}, test::kY, 35, {0.45f, 1, 0.45f})});
    Tracer tracer(ctx, scene, 320, 180);
    TraceSettings settings{.spp = 4, .maxBounces = 6, .frameSeed = 1};
    Frame sum;
    const std::uint32_t passes = std::max(1u, spp / 4);
    for (std::uint32_t i = 0; i < passes; ++i) {
        settings.frameSeed = 1 + i;
        Frame f = tracer.trace(scene.frame, settings);
        if (i == 0) {
            sum = std::move(f);
            continue;
        }
        for (std::size_t k = 0; k < sum.lighting.values.size(); ++k) {
            sum.lighting.values[k] += f.lighting.values[k];
            sum.emission.values[k] += f.emission.values[k];
        }
    }
    for (std::size_t k = 0; k < sum.lighting.values.size(); ++k) {
        sum.lighting.values[k] /= float(passes);
        sum.emission.values[k] /= float(passes);
    }
    writePicture(sum, "look_flint", dir);
    std::printf("wrote %s (%u spp)\n", (dir / "integrator_look_flint.png").string().c_str(), passes * 4);
}

} // namespace

// integrator_test --look <dir> [spp]: only renders a still of dispersive glass to look at.
// integrator_test [a|b|c|d|e ...]: only those parts (all by default).
int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    const std::string parts = argc > 1 ? [&] {
        std::string p;
        for (int i = 1; i < argc; ++i)
            p += argv[i];
        return p;
    }()
                                       : std::string("abcde");
    auto wanted = [&](char part) { return parts.find(part) != std::string::npos; };
    evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
    try {
        auto ctx = gfx::Context::create({.validation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the values are checked\n");
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 256);
            CHECK(ctx.validationMessageCount() == 0);
            return 0;
        }
        if (wanted('e'))
            testSpectral(ctx);
        if (wanted('b'))
            testClamp(ctx);
        if (wanted('c'))
            testCameraOnly(ctx);
        if (wanted('d'))
            testDispersion(ctx);
        if (wanted('a'))
            testRegularisation(ctx);
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("integrator_test: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
