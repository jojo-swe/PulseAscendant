// Emissive splats: points added into the emission guide in fixed point with integer
// atomics, through a test splat shader (shaders/test/splat_grid.comp: a row of points, a hashed cloud, a
// spiral galaxy), with the validation layer and its synchronization validation on; fails on any message.
//   (a) energy: one point's summed energy (sum of radiance times pixel area, at the 1920x1080 reference) is
//       its flux, and agrees at 160x90 and 320x180 within 1%, for a point and for a point of 2 px radius;
//       its pixels match a CPU reference of the projection and the renormalised 5x5 Gaussian; a faint
//       point (flux 0.05) keeps its energy at both sizes (the fixed point rounds in reference units);
//   (b) depth: a point behind the depth guide adds nothing, one in front (or over a miss, -1) adds its
//   energy,
//       one behind the camera nothing; the emission elsewhere stays exactly as it was;
//   (c) order independence: 2^18 points rendered twice give identical bytes, and so does a shader variant
//       that hands the same points to other invocations (SPLAT_PERMUTE_IDS);
//   (d) overflow: N identical points whose exact sum is just below 2^32 are not flagged and resolve to their
//       sum; one more wraps the channel, which is flagged and resolves to 65504, while the other channels
//       are not; a single add of 2^32 units or more is flagged and never added;
//   (e) refusals: checkSplats (capacity, scale, params, shader), record (size, format, scale, count);
//   (f) through ModernStage (a Cornell box with splats, as SceneId::Ship): splats behind the back wall leave
//       the picture byte for byte as without; one point lands on the pixel the tracer images it at; a galaxy
//       in front adds its flux to the HDR picture, also at trace scale 0.5 through the upscaler; a frame
//       after a seek equals a fresh renderer's (window and export); the splat images count in gpuBytes(),
//       measured; a frame over the capacity throws;
//   (g) a picture: a 2^18-star spiral galaxy at 320x180 (build-out/step5/splat_spiral.png).
// `splat_test --image <width> <height> <count> <file.png>` renders only the galaxy picture, at any size.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "modern/scene_gpu.hpp"
#include "modern/splats.hpp"
#include "post/tonemap.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include "rt_test_scenes.hpp"
#include "test_env.hpp"

#include <vk_mem_alloc.h>

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
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace gfx = evo::gfx;
namespace modern = evo::modern;
namespace render = evo::render;
namespace show = evo::show;
using evo::modern::Vec3;

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

const std::filesystem::path kGridShader =
    std::filesystem::path(EVO_TEST_SPIRV_DIR) / "test_splat_grid.comp.spv";
const std::filesystem::path kPermutedShader =
    std::filesystem::path(EVO_TEST_SPIRV_DIR) / "test_splat_grid_permuted.comp.spv";

enum Mode { kRow = 0, kCloud = 1, kSpiral = 2 };

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    float magnitude;
    if (exponent == 0)
        magnitude = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31)
        magnitude =
            mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else
        magnitude = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -magnitude : magnitude;
}

// Waits for the queue between subtests: synchronization validation credits no host wait on the timeline.
void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

modern::Camera testCamera() {
    return {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 60.f};
}

// The splat frame of a row of `count` points from `position`, `step` apart.
modern::SplatFrame rowFrame(std::uint32_t count, Vec3 position, float radius, Vec3 flux, Vec3 step = {}) {
    modern::SplatFrame f;
    f.count = count;
    f.params = {position.x, position.y, position.z, radius, flux.x, flux.y, flux.z, 0,
                step.x,     step.y,     step.z,     0,      0,      0,      0,      float(kRow)};
    return f;
}

modern::SplatFrame cloudFrame(std::uint32_t count, Vec3 centre, float extent, float flux, float radius) {
    modern::SplatFrame f;
    f.count = count;
    f.params = {centre.x, centre.y, centre.z, extent, flux, radius, 0, 0, 0, 0, 0, 0, 0, 0, 0, float(kCloud)};
    return f;
}

modern::SplatFrame spiralFrame(std::uint32_t count, Vec3 centre, float outer, float totalFlux,
                               float turnSpeed, float starRadius, float tilt) {
    modern::SplatFrame f;
    f.count = count;
    f.params = {centre.x, centre.y, centre.z, outer, totalFlux, turnSpeed, starRadius, 0,
                tilt,     0,        0,        0,     0,         0,         0,          float(kSpiral)};
    return f;
}

// --- The CPU reference ------------------------------------------------------------------------------------

struct Projected {
    bool visible = false;
    double x = 0, y = 0, z = 0; // pixel position and view depth
};

Projected project(const modern::Camera& camera, Vec3 p, std::uint32_t width, std::uint32_t height) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const Vec3 v = p - camera.eye;
    Projected out;
    out.z = modern::dot(v, b.forward);
    if (!(out.z > 1e-6))
        return out;
    const double tanHalf = std::tan(camera.fovYDegrees * 3.14159265358979323846 / 360.0);
    const double aspect = double(width) / height;
    const double ndcX = modern::dot(v, b.right) / (out.z * tanHalf * aspect);
    const double ndcY = modern::dot(v, b.up) / (out.z * tanHalf);
    out.x = (ndcX + 1.0) * 0.5 * width;
    out.y = (1.0 - ndcY) * 0.5 * height;
    out.visible = true;
    return out;
}

// What one point adds to every pixel (one channel per flux component), as splat_common.glsl defines it.
std::vector<double> referenceSplat(const modern::Camera& camera, Vec3 p, float radius, double flux,
                                   std::uint32_t width, std::uint32_t height, int half = 2) {
    std::vector<double> out(std::size_t{width} * height, 0.0);
    const Projected q = project(camera, p, width, height);
    if (!q.visible)
        return out;
    const double tanHalf = std::tan(camera.fovYDegrees * 3.14159265358979323846 / 360.0);
    const double sigma = std::max(radius / (q.z * tanHalf) * 0.5 * height, 0.5);
    const int cx = static_cast<int>(std::floor(q.x)), cy = static_cast<int>(std::floor(q.y));
    const int side = 2 * half + 1;
    double w[25], total = 0;
    for (int j = 0; j < side; ++j)
        for (int i = 0; i < side; ++i) {
            const double dx = cx + i - half + 0.5 - q.x, dy = cy + j - half + 0.5 - q.y;
            w[j * side + i] = std::exp(-(dx * dx + dy * dy) / (2 * sigma * sigma));
            total += w[j * side + i];
        }
    const double scale = flux * double(width) * height / (1920.0 * 1080.0) / total;
    for (int j = 0; j < side; ++j)
        for (int i = 0; i < side; ++i) {
            const int x = cx + i - half, y = cy + j - half;
            if (x >= 0 && y >= 0 && x < int(width) && y < int(height))
                out[std::size_t(y) * width + x] = scale * w[j * side + i];
        }
    return out;
}

// --- The pass harness -------------------------------------------------------------------------------------

struct Result {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint16_t> emissionBits; // RGBA16F
    std::array<std::vector<std::uint32_t>, 3> sums;
    std::vector<std::uint32_t> overflow;

    std::size_t index(std::uint32_t x, std::uint32_t y) const { return std::size_t{y} * width + x; }
    float emission(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return halfToFloat(emissionBits[index(x, y) * 4 + c]);
    }
    // The summed energy of a channel above `base`: sum of radiance times the pixel's share of the 1920x1080
    // reference.
    double energy(std::uint32_t c, float base = 0.f) const {
        double sum = 0;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
                sum += emission(x, y, c) - base;
        return sum * (1920.0 * 1080.0) / (double(width) * height);
    }
    // The pixel with the largest red sum.
    std::size_t brightest() const {
        return static_cast<std::size_t>(std::max_element(sums[0].begin(), sums[0].end()) - sums[0].begin());
    }
};

// A SplatPass on its own ring with a depth guide and an emission guide it clears to given values, and a
// readback buffer that lives as long as the harness.
class Harness {
public:
    Harness(gfx::Context& ctx, std::uint32_t width, std::uint32_t height)
        : width_(width), height_(height), ring_(std::make_unique<gfx::FrameRing>(ctx)) {
        constexpr VkImageUsageFlags usage =
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        depth_ = gfx::Image(ctx, {width, height, VK_FORMAT_R32_SFLOAT, usage, 1, "test depth"});
        emission_ =
            gfx::Image(ctx, {width, height, VK_FORMAT_R16G16B16A16_SFLOAT, usage, 1, "test emission"});
        const VkDeviceSize pixels = VkDeviceSize{width} * height;
        readback_ =
            gfx::Buffer(ctx, {pixels * (8 + 4 * 4), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "readback"});
        pass_ = std::make_unique<modern::SplatPass>(ctx, *ring_, width, height);
    }
    ~Harness() {
        ring_->waitAll();
        pass_.reset();
        ring_.reset();
    }

    modern::SplatPass& pass() { return *pass_; }
    gfx::FrameRing& ring() { return *ring_; }
    gfx::Image& emission() { return emission_; }

    Result run(const std::filesystem::path& shader, const modern::SplatFrame& frame, float depth,
               std::array<float, 4> base = {0, 0, 0, 0}, const modern::Camera& camera = testCamera(),
               double seconds = 0, std::uint32_t footprint = 5) {
        VkCommandBuffer cmd = ring_->begin();
        clear(cmd, depth, base);
        pass_->record(cmd, shader, frame, camera, depth_, emission_, seconds, footprint);
        readback(cmd);
        ring_->wait(ring_->submit());
        return collect();
    }

    // Records one pass with the given images (to check refusals); submits whatever happens.
    void recordWith(const std::filesystem::path& shader, const modern::SplatFrame& frame, gfx::Image& depth,
                    gfx::Image& emission) {
        VkCommandBuffer cmd = ring_->begin();
        try {
            pass_->record(cmd, shader, frame, testCamera(), depth, emission, 0.0);
        } catch (...) {
            ring_->submit();
            throw;
        }
        ring_->submit();
    }

    // Records the clears, the pass and a tonemap of the emission into `ldr`, read back into `rgb`.
    std::vector<std::uint8_t> picture(gfx::Context& ctx, const modern::SplatFrame& frame,
                                      const modern::Camera& camera, float exposureEV) {
        evo::post::Tonemap tonemap(ctx, *ring_);
        gfx::Image ldr(ctx, {width_, height_, VK_FORMAT_R8G8B8A8_UNORM,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "test ldr"});
        gfx::Readback rgb(ctx, width_, height_);
        VkCommandBuffer cmd = ring_->begin();
        clear(cmd, -1.f, {0, 0, 0, 1});
        pass_->record(cmd, kGridShader, frame, camera, depth_, emission_, 0.0);
        tonemap.record(cmd, emission_, ldr, exposureEV);
        rgb.record(cmd, ldr);
        ring_->wait(ring_->submit());
        return rgb.rgb();
    }

private:
    void clear(VkCommandBuffer cmd, float depth, std::array<float, 4> base) {
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkClearColorValue value{};
        value.float32[0] = depth;
        depth_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdClearColorImage(cmd, depth_.handle(), VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
        for (int c = 0; c < 4; ++c)
            value.float32[c] = base[c];
        emission_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                             VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdClearColorImage(cmd, emission_.handle(), VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
    }

    void readback(VkCommandBuffer cmd) {
        const VkDeviceSize pixels = VkDeviceSize{width_} * height_;
        gfx::bufferBarrier2(cmd, readback_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        gfx::Image* sources[5] = {&emission_, &pass_->accumulator(0), &pass_->accumulator(1),
                                  &pass_->accumulator(2), &pass_->overflow()};
        VkDeviceSize offset = 0;
        for (gfx::Image* image : sources) {
            image->transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                              VK_ACCESS_2_TRANSFER_READ_BIT);
            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {width_, height_, 1};
            vkCmdCopyImageToBuffer(cmd, image->handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   readback_.handle(), 1, &region);
            offset += pixels * (image == &emission_ ? 8 : 4);
        }
        gfx::bufferBarrier2(cmd, readback_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                            VK_ACCESS_2_HOST_READ_BIT);
    }

    Result collect() {
        readback_.invalidate();
        const std::size_t pixels = std::size_t{width_} * height_;
        const auto* bytes = static_cast<const std::uint8_t*>(readback_.mapped());
        Result r;
        r.width = width_;
        r.height = height_;
        r.emissionBits.resize(pixels * 4);
        std::memcpy(r.emissionBits.data(), bytes, pixels * 8);
        std::size_t offset = pixels * 8;
        for (int c = 0; c < 3; ++c) {
            r.sums[c].resize(pixels);
            std::memcpy(r.sums[c].data(), bytes + offset, pixels * 4);
            offset += pixels * 4;
        }
        r.overflow.resize(pixels);
        std::memcpy(r.overflow.data(), bytes + offset, pixels * 4);
        return r;
    }

    std::uint32_t width_, height_;
    gfx::Image depth_, emission_;
    gfx::Buffer readback_;
    std::unique_ptr<gfx::FrameRing> ring_;
    std::unique_ptr<modern::SplatPass> pass_;
};

// --- (a) ---------------------------------------------------------------------------------------------------

void testEnergyAcrossResolutions(gfx::Context& ctx) {
    const Vec3 flux{1000.f, 500.f, 250.f};
    const Vec3 position{0.31f, -0.17f, -4.f};
    // radius 0: sigma 0.5 px at both sizes; radius 0.1: 1.95 px at 160x90 and 3.9 px at 320x180.
    for (const float radius : {0.f, 0.1f}) {
        std::array<double, 2> energies{};
        int k = 0;
        for (const auto [w, h] :
             {std::array<std::uint32_t, 2>{160, 90}, std::array<std::uint32_t, 2>{320, 180}}) {
            Harness harness(ctx, w, h);
            const Result r = harness.run(kGridShader, rowFrame(1, position, radius, flux), -1.f);
            for (std::uint32_t c = 0; c < 3; ++c) {
                const double f = c == 0 ? flux.x : c == 1 ? flux.y : flux.z;
                const double e = r.energy(c);
                CHECK(std::fabs(e - f) < 0.01 * f);
                // Every pixel against the CPU reference: projection, footprint and scale.
                const std::vector<double> ref = referenceSplat(testCamera(), position, radius, f, w, h);
                for (std::uint32_t y = 0; y < h; ++y)
                    for (std::uint32_t x = 0; x < w; ++x) {
                        const double expected = ref[r.index(x, y)], got = r.emission(x, y, c);
                        CHECK(std::fabs(got - expected) <= 2e-3 * expected + 2.f / 4096.f);
                    }
            }
            energies[k++] = r.energy(0);
            std::printf("(a) radius %.2f at %ux%u: energy %.3f of flux %.0f\n", radius, w, h, r.energy(0),
                        flux.x);
            idle(ctx);
        }
        CHECK(std::fabs(energies[0] - energies[1]) < 0.01 * energies[0]);
    }
    // A faint point (the fixed point rounds in reference units, so its adds round alike at every size):
    // flux 0.05, about 125 steps at its centre at the default scale, whatever the resolution.
    std::array<double, 2> faint{};
    int k = 0;
    for (const auto [w, h] :
         {std::array<std::uint32_t, 2>{160, 90}, std::array<std::uint32_t, 2>{320, 180}}) {
        Harness harness(ctx, w, h);
        faint[k++] =
            harness.run(kGridShader, rowFrame(1, position, 0.f, {0.05f, 0.05f, 0.05f}), -1.f).energy(0);
        idle(ctx);
    }
    CHECK(std::fabs(faint[0] - 0.05) < 0.02 * 0.05 && std::fabs(faint[1] - 0.05) < 0.02 * 0.05);
    std::printf("(a) a faint point of flux 0.05: energy %.5f at 160x90, %.5f at 320x180\n", faint[0],
                faint[1]);
    // The window's 3x3 footprint: the same energy, every pixel as the 3x3 reference, nothing further out.
    for (const float radius : {0.f, 0.1f}) {
        Harness harness(ctx, 160, 90);
        const Result r = harness.run(kGridShader, rowFrame(1, position, radius, flux), -1.f, {0, 0, 0, 0},
                                     testCamera(), 0.0, 3);
        const double e = r.energy(0);
        CHECK(std::fabs(e - flux.x) < 0.01 * flux.x);
        const std::vector<double> ref = referenceSplat(testCamera(), position, radius, flux.x, 160, 90, 1);
        std::uint32_t lit = 0;
        for (std::uint32_t y = 0; y < 90; ++y)
            for (std::uint32_t x = 0; x < 160; ++x) {
                const double expected = ref[r.index(x, y)], got = r.emission(x, y, 0);
                CHECK(std::fabs(got - expected) <= 2e-3 * expected + 2.f / 4096.f);
                lit += got > 0.f;
            }
        CHECK(lit <= 9);
        std::printf("(a) the 3x3 footprint, radius %.2f: energy %.3f of %.0f over %u pixels\n", radius, e,
                    flux.x, lit);
        idle(ctx);
    }
}

// --- (b) ---------------------------------------------------------------------------------------------------

void testDepth(gfx::Context& ctx) {
    Harness harness(ctx, 160, 90);
    const Vec3 flux{800.f, 800.f, 800.f};
    const std::array<float, 4> base{0.25f, 0.5f, 1.f, 1.f};
    const auto unchanged = [&](const Result& r, bool sums = true) {
        for (std::uint32_t y = 0; y < r.height; ++y)
            for (std::uint32_t x = 0; x < r.width; ++x)
                for (std::uint32_t c = 0; c < 4; ++c)
                    if (r.emission(x, y, c) != base[c])
                        return false;
        for (std::uint32_t c = 0; c < 3 && sums; ++c)
            for (const std::uint32_t s : r.sums[c])
                if (s != 0)
                    return false;
        return true;
    };
    const modern::SplatFrame point = rowFrame(1, {0.2f, 0.1f, -2.f}, 0.f, flux); // view depth 2
    const Result behind = harness.run(kGridShader, point, 1.5f, base);
    CHECK(unchanged(behind));
    const Result front = harness.run(kGridShader, point, 3.f, base);
    const Result miss = harness.run(kGridShader, point, -1.f, base);
    for (std::uint32_t c = 0; c < 3; ++c) {
        CHECK(std::fabs(front.energy(c, base[c]) - flux.x) < 0.01 * flux.x);
        CHECK(front.emissionBits == miss.emissionBits);
    }
    // Alpha and the pixels the point misses keep their values.
    std::uint32_t untouched = 0;
    for (std::uint32_t y = 0; y < front.height; ++y)
        for (std::uint32_t x = 0; x < front.width; ++x) {
            CHECK(front.emission(x, y, 3) == 1.f);
            untouched += front.sums[0][front.index(x, y)] == 0 && front.emission(x, y, 0) == base[0];
        }
    CHECK(untouched >= 160 * 90 - 25);
    // Behind the camera.
    CHECK(unchanged(harness.run(kGridShader, rowFrame(1, {0.f, 0.f, 2.f}, 0.f, flux), -1.f, base)));
    // A count of 0 records nothing (the accumulators keep the last frame's sums; the emission is untouched).
    CHECK(unchanged(harness.run(kGridShader, rowFrame(0, {0.2f, 0.1f, -2.f}, 0.f, flux), -1.f, base), false));
    std::printf("(b) a point behind the depth guide adds nothing; in front or over a miss it adds %.2f\n",
                front.energy(0, base[0]));
}

// --- (c) ---------------------------------------------------------------------------------------------------

void testOrderIndependence(gfx::Context& ctx) {
    Harness harness(ctx, 160, 90);
    const modern::SplatFrame cloud = cloudFrame(1u << 18, {0.f, 0.f, -5.f}, 2.5f, 20.f, 0.02f);
    const auto t0 = std::chrono::steady_clock::now();
    const Result first = harness.run(kGridShader, cloud, -1.f);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const Result second = harness.run(kGridShader, cloud, -1.f);
    const Result permuted = harness.run(kPermutedShader, cloud, -1.f);
    CHECK(first.emissionBits == second.emissionBits);
    CHECK(first.sums == second.sums);
    CHECK(first.emissionBits == permuted.emissionBits);
    CHECK(first.sums == permuted.sums);
    std::uint32_t lit = 0;
    for (const std::uint32_t s : first.sums[0])
        lit += s > 0;
    CHECK(lit > 160 * 90 * 9 / 10);
    // A different cloud does differ (the comparison sees the points at all).
    const Result other =
        harness.run(kGridShader, cloudFrame(1u << 18, {0.1f, 0.f, -5.f}, 2.5f, 20.f, 0.02f), -1.f);
    CHECK(other.emissionBits != first.emissionBits);
    std::printf(
        "(c) 2^18 points: identical bytes twice and with permuted ids (%u of %u pixels lit, %.0f ms)\n", lit,
        160u * 90u, ms);
}

// --- (d) ---------------------------------------------------------------------------------------------------

void testOverflow(gfx::Context& ctx) {
    Harness harness(ctx, 160, 90);
    constexpr float kScale = 1048576.f; // 2^20: a channel holds up to 4096 reference units
    constexpr double kArea = 160.0 * 90.0 / (1920.0 * 1080.0);
    const Vec3 position{0.02f, 0.03f, -3.f};
    const Vec3 flux{6.f, 0.06f, 0.f};
    modern::SplatFrame one = rowFrame(1, position, 0.f, flux);
    one.fixedPointScale = kScale;
    const Result single = harness.run(kGridShader, one, -1.f);
    const std::size_t centre = single.brightest();
    const std::uint64_t v = single.sums[0][centre], vGreen = single.sums[1][centre];
    CHECK(v > (1u << 21) && v < (1u << 23));
    const auto n = static_cast<std::uint32_t>((std::uint64_t{1} << 32) / v); // n * v < 2^32 <= (n + 1) * v
    CHECK(n * v < (std::uint64_t{1} << 32) && (n + 1) * v >= (std::uint64_t{1} << 32));
    const std::uint32_t x = static_cast<std::uint32_t>(centre % 160),
                        y = static_cast<std::uint32_t>(centre / 160);

    modern::SplatFrame below = one;
    below.count = n;
    const Result under = harness.run(kGridShader, below, -1.f);
    CHECK(under.sums[0][centre] == n * v);
    for (const std::uint32_t bits : under.overflow)
        CHECK(bits == 0);
    const double expected = double(n * v) / kScale * kArea;
    CHECK(std::fabs(under.emission(x, y, 0) - expected) <= 2e-3 * expected);
    CHECK(under.emission(x, y, 0) < 65504.f);

    modern::SplatFrame above = one;
    above.count = n + 1;
    const Result over = harness.run(kGridShader, above, -1.f);
    CHECK(over.overflow[centre] == 1u); // red only
    CHECK(over.emission(x, y, 0) == 65504.f);
    CHECK(over.sums[1][centre] == (n + 1) * vGreen);
    const double green = double((n + 1) * vGreen) / kScale * kArea;
    CHECK(std::fabs(over.emission(x, y, 1) - green) <= 2e-3 * green);
    std::uint32_t flagged = 0;
    for (const std::uint32_t bits : over.overflow)
        flagged += bits != 0;
    CHECK(flagged == 1);

    // One add of 2^32 units or more: flagged, never added.
    modern::SplatFrame huge = rowFrame(1, position, 0.f, {1e9f, 0.f, 0.f});
    huge.fixedPointScale = kScale;
    const Result big = harness.run(kGridShader, huge, -1.f);
    CHECK((big.overflow[centre] & 1u) == 1u);
    CHECK(big.sums[0][centre] == 0);
    CHECK(big.emission(x, y, 0) == 65504.f);
    std::printf("(d) %u adds of %llu units: sum %llu unflagged (%.2f); one more: flagged, 65504\n", n,
                static_cast<unsigned long long>(v), static_cast<unsigned long long>(n * v), expected);
}

// --- (e) ---------------------------------------------------------------------------------------------------

void testRefusals(gfx::Context& ctx) {
    // checkSplats: the CPU validation the tracer and the scene sweep run.
    modern::checkSplats(std::nullopt, 0, {});
    modern::checkSplats(rowFrame(10, {}, 0, {1, 1, 1}), 10, kGridShader);
    CHECK(throws<std::length_error>(
        [&] { modern::checkSplats(rowFrame(11, {}, 0, {1, 1, 1}), 10, kGridShader); }));
    CHECK(throws<std::invalid_argument>([&] { modern::checkSplats(rowFrame(1, {}, 0, {1, 1, 1}), 10, {}); }));
    modern::SplatFrame bad = rowFrame(1, {}, 0, {1, 1, 1});
    bad.fixedPointScale = 0.f;
    CHECK(throws<std::invalid_argument>([&] { modern::checkSplats(bad, 10, kGridShader); }));
    bad.fixedPointScale = std::numeric_limits<float>::infinity();
    CHECK(throws<std::invalid_argument>([&] { modern::checkSplats(bad, 10, kGridShader); }));
    bad = rowFrame(1, {}, 0, {1, 1, 1});
    bad.params[5] = std::numeric_limits<float>::quiet_NaN();
    CHECK(throws<std::invalid_argument>([&] { modern::checkSplats(bad, 10, kGridShader); }));
    bad = rowFrame(1, {}, 0, {1, 1, 1});
    bad.windowFootprint = 4;
    CHECK(throws<std::invalid_argument>([&] { modern::checkSplats(bad, 10, kGridShader); }));

    // checkSceneFrame runs it on the scene's capacity and shader.
    evo::test::TestScene scene = evo::test::cornellBox();
    scene.frame.splats = rowFrame(5, {}, 0, {1, 1, 1});
    CHECK(throws<std::invalid_argument>([&] { modern::checkSceneFrame(scene.statics, scene.frame); }));
    scene.statics.splatShader = kGridShader;
    CHECK(throws<std::length_error>([&] { modern::checkSceneFrame(scene.statics, scene.frame); }));
    scene.statics.capacity.splats = 5;
    modern::checkSceneFrame(scene.statics, scene.frame);

    // record(): sizes, formats, scale, count; a shader that does not load.
    Harness harness(ctx, 32, 18);
    gfx::Image depth(ctx, {32, 18, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, 1, "depth"});
    gfx::Image emission(ctx,
                        {32, 18, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, 1, "emission"});
    gfx::Image small(ctx, {16, 18, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, 1, "small"});
    gfx::Image wrong(ctx, {32, 18, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, 1, "wrong"});
    const modern::SplatFrame ok = rowFrame(1, {0, 0, -2}, 0, {1, 1, 1});
    CHECK(throws<std::invalid_argument>([&] { harness.recordWith(kGridShader, ok, small, emission); }));
    CHECK(throws<std::invalid_argument>([&] { harness.recordWith(kGridShader, ok, depth, wrong); }));
    CHECK(throws<std::invalid_argument>([&] { harness.recordWith(kGridShader, ok, emission, emission); }));
    modern::SplatFrame zero = ok;
    zero.fixedPointScale = 0.f;
    CHECK(throws<std::invalid_argument>([&] { harness.recordWith(kGridShader, zero, depth, emission); }));
    modern::SplatFrame many = ok;
    many.count = std::numeric_limits<std::uint32_t>::max();
    CHECK(throws<std::length_error>([&] { harness.recordWith(kGridShader, many, depth, emission); }));
    CHECK(throws<std::runtime_error>(
        [&] { harness.recordWith("/nonexistent/splat.spv", ok, depth, emission); }));
    harness.ring().waitAll();
    CHECK(throws<std::invalid_argument>([&] { modern::SplatPass(ctx, harness.ring(), 0, 18); }));
    // One pipeline per shader path: the grid shader, loaded once however often it ran.
    harness.recordWith(kGridShader, ok, depth, emission);
    harness.pass().prepare(kGridShader);
    CHECK(harness.pass().pipelineCount() == 1);
    harness.ring().waitAll();
    std::printf("(e) refusals: capacity, scale, params, shader, sizes, formats, count\n");
}

// --- (f) ---------------------------------------------------------------------------------------------------

constexpr std::uint32_t kWidth = 160, kHeight = 90;
constexpr int kFlyby = 1;
constexpr std::uint32_t kCapacity = 1u << 16;

double chapterTime(int chapter, double fraction) {
    const double start = show::chapterStart(chapter);
    return start + fraction * (show::chapterStart(chapter + 1) - start);
}

show::TimelineFrame forced2026(double seconds) {
    show::TimelineFrame frame = show::sampleTimeline(seconds);
    frame.era = 1.f;
    frame.wipe = 1.f;
    frame.eraCut = 0.f;
    return frame;
}

// The Cornell box with the splats the test sets (none by default), as SceneId::Ship.
class SplatScene final : public modern::ModernScene {
public:
    explicit SplatScene(const std::optional<modern::SplatFrame>* splats) : splats_(splats) {
        scene_.statics.splatShader = kGridShader;
        scene_.statics.capacity.splats = kCapacity;
    }
    const modern::SceneStatic& staticScene() const override { return scene_.statics; }
    modern::SceneFrame frame(const show::TimelineFrame&) const override {
        modern::SceneFrame f = scene_.frame;
        f.splats = *splats_;
        return f;
    }
    modern::Camera camera(const show::TimelineFrame&) const override { return scene_.frame.camera; }
    bool samplesRetro() const override { return false; }

private:
    evo::test::TestScene scene_ = evo::test::cornellBox();
    const std::optional<modern::SplatFrame>* splats_;
};

struct Frame {
    std::vector<std::uint8_t> rgb;
    std::vector<std::uint16_t> hdr; // RGBA16F at the trace size (= the output size here)
    float hdrAt(std::size_t pixel, std::uint32_t c) const { return halfToFloat(hdr[pixel * 4 + c]); }
};

class StageHarness {
public:
    StageHarness(gfx::Context& ctx, render::RenderMode mode, const std::optional<modern::SplatFrame>* splats,
                 float traceScale = 1.f)
        : readback_(ctx, kWidth, kHeight), hdrBuffer_(ctx, {VkDeviceSize{kWidth} * kHeight * 8,
                                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "hdr"}),
          ring_(ctx), renderer_(ctx, ring_, settings(mode, splats, traceScale)) {}

    Frame render(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        readback_.record(cmd, renderer_.output());
        // The previous frame's copy into the buffer finished before the host read it; order the GPU writes
        // too.
        gfx::bufferBarrier2(cmd, hdrBuffer_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        gfx::Image& hdr = renderer_.modern().hdr();
        hdr.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                       VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kWidth, kHeight, 1};
        vkCmdCopyImageToBuffer(cmd, hdr.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, hdrBuffer_.handle(),
                               1, &region);
        gfx::bufferBarrier2(cmd, hdrBuffer_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                            VK_ACCESS_2_HOST_READ_BIT);
        ring_.wait(ring_.submit());
        hdrBuffer_.invalidate();
        Frame out;
        out.rgb = readback_.rgb();
        const auto* bits = static_cast<const std::uint16_t*>(hdrBuffer_.mapped());
        out.hdr.assign(bits, bits + std::size_t{kWidth} * kHeight * 4);
        return out;
    }
    render::Renderer& renderer() { return renderer_; }

private:
    static render::RenderSettings
    settings(render::RenderMode mode, const std::optional<modern::SplatFrame>* splats, float traceScale) {
        render::RenderSettings s{kWidth, kHeight};
        s.traceScale = traceScale;
        s.hud = false;
        s.mode = mode;
        s.spp = 2;
        s.sceneFactory = [splats](modern::SceneId id,
                                  modern::SceneDetail detail) -> std::unique_ptr<modern::ModernScene> {
            if (id == modern::SceneId::Ship)
                return std::make_unique<SplatScene>(splats);
            return modern::makeScene(id, detail);
        };
        s.sceneMap = [](show::Effect effect) {
            return effect == show::Effect::Flyby ? modern::SceneId::Ship : modern::sceneFor(effect);
        };
        return s;
    }

    gfx::Readback readback_;
    gfx::Buffer hdrBuffer_;
    gfx::FrameRing ring_;
    render::Renderer renderer_; // last: its destructor waits for the ring
};

VkDeviceSize allocatedBytes(gfx::Context& ctx) {
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(ctx.allocator(), &stats);
    return stats.total.statistics.allocationBytes;
}

void testThroughModernStage(gfx::Context& ctx) {
    const show::TimelineFrame at = forced2026(chapterTime(kFlyby, 0.4));
    const show::TimelineFrame later = forced2026(chapterTime(kFlyby, 0.6));
    const modern::Camera camera = evo::test::cornellBox().frame.camera; // eye (0, 1, 0.95), looking at -z
    std::optional<modern::SplatFrame> splats;

    // Memory: the splat images come with the first splat scene and count in gpuBytes(), as allocated.
    Frame plain;
    {
        StageHarness h(ctx, render::RenderMode::Export, &splats);
        render::ModernStage& stage = h.renderer().modern();
        const VkDeviceSize stageImages = stage.hdr().bytes() + stage.image().bytes();
        const VkDeviceSize before = allocatedBytes(ctx);
        stage.prepare(modern::SceneId::Ship);
        CHECK(allocatedBytes(ctx) - before == stage.gpuBytes() - stageImages);
        CHECK(stage.imageBytes() >= stageImages + VkDeviceSize{kWidth} * kHeight * 4 * 4);
        plain = h.render(at);
    }
    idle(ctx);

    // Behind the back wall (z = -1, view depth 1.95): the picture is byte for byte the one without splats.
    splats = cloudFrame(kCapacity, {0.f, 1.f, -1.6f}, 0.5f, 200.f, 0.01f);
    {
        StageHarness h(ctx, render::RenderMode::Export, &splats);
        const Frame hidden = h.render(at);
        CHECK(hidden.rgb == plain.rgb);
        CHECK(hidden.hdr == plain.hdr);
    }
    idle(ctx);

    // One point in front of the back wall lands on the pixel the CPU projection (the tracer's camera) gives.
    const Vec3 point{0.3f, 1.25f, -0.9f};
    splats = rowFrame(1, point, 0.f, {5000.f, 5000.f, 5000.f});
    {
        StageHarness h(ctx, render::RenderMode::Export, &splats);
        const Frame one = h.render(at);
        std::size_t best = 0;
        float bestGain = -1.f;
        for (std::size_t p = 0; p < std::size_t{kWidth} * kHeight; ++p) {
            const float gain = one.hdrAt(p, 0) - plain.hdrAt(p, 0);
            if (gain > bestGain)
                bestGain = gain, best = p;
        }
        const Projected q = project(camera, point, kWidth, kHeight);
        CHECK(q.visible);
        CHECK(best == std::size_t(std::floor(q.y)) * kWidth + std::size_t(std::floor(q.x)));
        std::printf("(f) one point lands on pixel (%zu, %zu), the projection's (%.2f, %.2f)\n", best % kWidth,
                    best / kWidth, q.x, q.y);
    }
    idle(ctx);

    // A galaxy in front of the back wall, clear of the boxes: the HDR picture gains its flux.
    const float totalFlux = 3000.f;
    splats = spiralFrame(kCapacity, {0.f, 1.65f, -0.8f}, 0.28f, totalFlux, 0.5f, 0.f, 0.f);
    Frame galaxy;
    {
        StageHarness h(ctx, render::RenderMode::Export, &splats);
        galaxy = h.render(at);
        CHECK(galaxy.rgb != plain.rgb);
        double gained = 0;
        for (std::size_t p = 0; p < std::size_t{kWidth} * kHeight; ++p)
            for (std::uint32_t c = 0; c < 3; ++c)
                gained += galaxy.hdrAt(p, c) - plain.hdrAt(p, c);
        // The stage must neither lose nor double the stars: against the same splats run through a pass of
        // its own over an empty depth guide (every star is in front of the back wall and above the boxes).
        Harness pass(ctx, kWidth, kHeight);
        const Result alone = pass.run(kGridShader, *splats, -1.f, {0, 0, 0, 0}, camera, at.localSeconds);
        const double expected = (alone.energy(0) + alone.energy(1) + alone.energy(2)) * double(kWidth) *
                                kHeight / (1920.0 * 1080.0);
        CHECK(std::fabs(gained - expected) < 0.02 * expected);
        std::printf("(f) a galaxy in front: the HDR picture gains %.2f, the splats alone %.2f\n", gained,
                    expected);
    }
    idle(ctx);

    // Below trace scale 1 the splats go in at the trace size (80x45) and the upscaler takes them to the
    // output: the picture's gain stays within 5 % of the full-size one (the clamped Catmull-Rom upsample is
    // not exactly energy-preserving at single-pixel stars). The sums are in reference units, so the faint
    // stars' adds round the same at 80x45 as at 160x90.
    {
        std::optional<modern::SplatFrame> none;
        Frame plainHalf, galaxyHalf;
        {
            StageHarness h(ctx, render::RenderMode::Export, &none, 0.5f);
            plainHalf = h.render(at);
        }
        idle(ctx);
        {
            StageHarness h(ctx, render::RenderMode::Export, &splats, 0.5f);
            CHECK(h.renderer().modern().traceExtent().width == kWidth / 2);
            galaxyHalf = h.render(at);
        }
        idle(ctx);
        double full = 0, half = 0;
        for (std::size_t p = 0; p < std::size_t{kWidth} * kHeight; ++p)
            for (std::uint32_t c = 0; c < 3; ++c) {
                full += galaxy.hdrAt(p, c) - plain.hdrAt(p, c);
                half += galaxyHalf.hdrAt(p, c) - plainHalf.hdrAt(p, c);
            }
        CHECK(std::fabs(half - full) < 0.05 * full);
        std::printf("(f) at trace scale 0.5 the HDR picture gains %.2f (%.2f at scale 1)\n", half, full);
    }

    // Seeks: the moving galaxy at `later` after `at` equals a fresh renderer's `later`, in both modes.
    for (const render::RenderMode mode : {render::RenderMode::Export, render::RenderMode::Interactive}) {
        Frame seeked, fresh;
        {
            StageHarness h(ctx, mode, &splats);
            const Frame first = h.render(at);
            seeked = h.render(later);
            CHECK(first.rgb != seeked.rgb); // the galaxy turned
        }
        idle(ctx);
        {
            StageHarness h(ctx, mode, &splats);
            fresh = h.render(later);
        }
        idle(ctx);
        CHECK(seeked.rgb == fresh.rgb);
        CHECK(seeked.hdr == fresh.hdr);
    }
    std::printf("(f) after a seek the frame equals a fresh renderer's, in export and in the window\n");

    // Over the capacity: the frame is refused (by the tracer's checkSceneFrame) before anything records.
    evo::test::TestScene scene = evo::test::cornellBox();
    scene.statics.splatShader = kGridShader;
    scene.statics.capacity.splats = kCapacity;
    scene.frame.splats = cloudFrame(kCapacity + 1, {}, 1.f, 1.f, 0.f);
    CHECK(throws<std::length_error>([&] { modern::checkSceneFrame(scene.statics, scene.frame); }));
}

// --- (g) ---------------------------------------------------------------------------------------------------

void writeGalaxy(gfx::Context& ctx, std::uint32_t width, std::uint32_t height, std::uint32_t count,
                 const std::filesystem::path& file) {
    Harness harness(ctx, width, height);
    const modern::Camera camera{{0.f, 1.4f, 4.2f}, {0.f, -0.1f, 0.f}, {0, 1, 0}, 50.f};
    const modern::SplatFrame frame = spiralFrame(count, {0.f, 0.f, 0.f}, 2.f, 2.5e5f, 0.f, 0.f, -1.35f);
    const std::vector<std::uint8_t> rgb = harness.picture(ctx, frame, camera, 0.f);
    std::filesystem::create_directories(file.parent_path());
    evo::image::writePng(file, static_cast<int>(width), static_cast<int>(height), rgb.data());
    std::size_t lit = 0;
    for (std::size_t i = 0; i < rgb.size(); i += 3)
        lit += rgb[i] + rgb[i + 1] + rgb[i + 2] > 30;
    CHECK(lit > rgb.size() / 3 / 50);
    std::printf("(g) %u stars at %ux%u: %s (%zu pixels lit)\n", count, width, height, file.string().c_str(),
                lit);
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the results are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("SKIP sync validation: the layer's synchronization validation is unavailable\n");
            if (argc == 6 && std::string(argv[1]) == "--image") {
                writeGalaxy(ctx, static_cast<std::uint32_t>(std::stoul(argv[2])),
                            static_cast<std::uint32_t>(std::stoul(argv[3])),
                            static_cast<std::uint32_t>(std::stoul(argv[4])), argv[5]);
                idle(ctx);
            } else {
                testEnergyAcrossResolutions(ctx);
                idle(ctx);
                testDepth(ctx);
                idle(ctx);
                testOrderIndependence(ctx);
                idle(ctx);
                testOverflow(ctx);
                idle(ctx);
                testRefusals(ctx);
                idle(ctx);
                testThroughModernStage(ctx);
                idle(ctx);
                writeGalaxy(ctx, 320, 180, 1u << 18,
                            std::filesystem::path(EVO_TEST_OUT_DIR) / "step5" / "splat_spiral.png");
                idle(ctx);
            }
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("splat_test: %d checks passed in %.1f s\n", checksPassed, seconds);
    return 0;
}
