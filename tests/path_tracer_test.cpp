// The path tracer on lavapipe, with the validation layer and synchronization validation on; fails on any
// message. 64x64 unless noted.
//
// (a) an emitter quad: its emission lands in the emission image exactly and nothing in lighting, in one
//     dispatch and in two (the running mean across dispatches);
// (b) MIS is unbiased: a Cornell box with and without next-event estimation has the same mean, and NEE is
//     less noisy; plus analytic cases: a diffuse floor under one rectangular light, and under two selected
//     1 : 3, and under a light close above it (shadow rays must not hit the light they aim at), equals
//     Lambert's polygon formula with and without NEE; a rough metal and a smooth-coated floor
//     agree with and without NEE; a mirror floor (delta: no NEE, no MIS) gives the same bytes either way; a
//     panel lit through a window portal, and through two stacked portals, equals a quadrature of the sky with
//     and without NEE; a retro screen of two colours lights the floor the same with and without NEE, channel
//     by channel, each colour on its own side;
// (c) a furnace: inside a closed 0.5-albedo sphere emitting 1 the lighting is the geometric series 1, with
//     Russian roulette off and on;
// (d) a frame is a pure function of its seed (all six images), and another seed changes it;
// (e) the stress scene stays finite in every value of every image;
// (f) spp 8 records exactly 2 trace dispatches (spp 1: 1, spp 5: 2);
// (g) the guides: depth, normal and motion of a plane, for a static and a moving camera;
// (h) the retro screen as a textured light: its picture in the emission image (decoded from sRGB once) and
//     its colour on the floor in front of it through NEE;
// (i) the hit payload (the frozen ABI): every closest-hit (one per material class) writes
//     materialFlags, abbe and medium from its material, primitive = gl_PrimitiveID within its mesh and
//     prevPosition = position (until Task 9), seen through a probe raygen (shaders/test/payload_probe.rgen)
//     in place of pathtrace.rgen, which also sees TraceSettings::seconds as FrameUniforms::seconds;
// and the seed formula, frameSeedFor, and the refusals (including lights that disagree with the instance
// that is their surface, and a second trace in one frame).
//
// With --look <dir> [spp] it only renders a larger scene (glass, chrome, checker floor, coated wood) to look
// at.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/path_tracer.hpp"
#include "post/tonemap_reference.hpp"
#include "rt/random.hpp"
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
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;
using modern::Camera;
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

// Waits for the queue. Synchronization validation in layer 1.3.275 does not credit host waits on the timeline
// semaphore, so memory a later test reuses (VMA) would raise false hazards; a queue idle is credited.
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

// One image read back: raw bytes and its values as floats, `channels` per pixel.
struct Plane {
    std::uint32_t width = 0, height = 0, channels = 0;
    std::vector<std::uint8_t> bytes;
    std::vector<float> values;
    float at(std::uint32_t x, std::uint32_t y, std::uint32_t c = 0) const {
        return values[(std::size_t(y) * width + x) * channels + c];
    }
    float luminance(std::uint32_t x, std::uint32_t y) const {
        return 0.2126f * at(x, y, 0) + 0.7152f * at(x, y, 1) + 0.0722f * at(x, y, 2);
    }
    double meanLuminance() const {
        double sum = 0;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
                sum += luminance(x, y);
        return sum / (double(width) * height);
    }
};

struct Frame {
    Plane lighting, emission, albedo, normal, depth, motion;
    std::array<const Plane*, 6> all() const {
        return {&lighting, &emission, &albedo, &normal, &depth, &motion};
    }
};

// Copies an image into a host buffer within the frame being recorded.
struct PendingRead {
    gfx::Buffer buffer;
    VkFormat format;
    std::uint32_t width, height;
};

PendingRead recordRead(gfx::Context& ctx, VkCommandBuffer cmd, gfx::Image& image) {
    const VkFormat format = image.format();
    const std::uint32_t bytesPerPixel = format == VK_FORMAT_R32G32B32A32_SFLOAT   ? 16
                                        : format == VK_FORMAT_R16G16B16A16_SFLOAT ? 8
                                        : format == VK_FORMAT_R32_SFLOAT          ? 4
                                                                                  : 0;
    if (bytesPerPixel == 0)
        throw std::logic_error("recordRead: unexpected format");
    const VkExtent2D extent = image.extent();
    PendingRead read{gfx::Buffer(ctx, {VkDeviceSize(extent.width) * extent.height * bytesPerPixel,
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "path tracer test readback"}),
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
    const std::size_t pixels = std::size_t(read.width) * read.height;
    if (read.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
        plane.channels = 4;
        plane.values.resize(pixels * 4);
        for (std::size_t i = 0; i < pixels * 4; ++i) {
            std::uint16_t h;
            std::memcpy(&h, bytes + 2 * i, 2);
            plane.values[i] = halfToFloat(h);
        }
    } else {
        plane.channels = read.format == VK_FORMAT_R32_SFLOAT ? 1 : 4;
        plane.values.resize(pixels * plane.channels);
        std::memcpy(plane.values.data(), bytes, plane.values.size() * sizeof(float));
    }
    return plane;
}

// A path tracer for one scene with its own frame ring (the scene's GPU side and the ring are declared first:
// the tracer's TLAS waits for the ring's frames when it goes, before the BLASes and the ring themselves).
class Tracer {
public:
    Tracer(gfx::Context& ctx, const TestScene& scene, std::uint32_t width, std::uint32_t height)
        : ctx_(ctx), gpu_(ctx, scene.statics), ring_(ctx),
          tracer_(ctx, ring_, width, height, modern::tracerCapacity(scene.statics.capacity)) {}

    // afterTrace runs between the trace and the readback, in the same frame.
    Frame trace(const modern::SceneFrame& frame, const TraceSettings& settings, gfx::Image& retro,
                const std::optional<Camera>& previous = std::nullopt,
                const std::function<void(VkCommandBuffer)>& afterTrace = {}) {
        VkCommandBuffer cmd = ring_.begin();
        tracer_.trace(cmd, gpu_, frame, previous, settings, &retro);
        if (afterTrace)
            afterTrace(cmd);
        modern::GBuffer& g = tracer_.gbuffer();
        std::vector<PendingRead> reads;
        for (gfx::Image* image : {&g.lighting, &g.emission, &g.albedo, &g.normal, &g.depth, &g.motion})
            reads.push_back(recordRead(ctx_, cmd, *image));
        ring_.wait(ring_.submit());
        idle(ctx_);
        return {finishRead(reads[0]), finishRead(reads[1]), finishRead(reads[2]),
                finishRead(reads[3]), finishRead(reads[4]), finishRead(reads[5])};
    }

    modern::PathTracer& pathTracer() { return tracer_; }
    const modern::SceneGpu& scene() const { return gpu_; }
    bool samplesRetro() const { return gpu_.samplesRetro(); }
    gfx::FrameRing& ring() { return ring_; }

private:
    gfx::Context& ctx_;
    modern::SceneGpu gpu_;
    gfx::FrameRing ring_;
    modern::PathTracer tracer_;
};

struct Rgb8 {
    std::uint8_t r, g, b;
};

// A retro picture as the retro stage makes it: 320 x 200 R8G8B8A8_SRGB with every mip level; pixel(x, y)
// gives each texel's sRGB bytes, row 0 at the top.
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
    ctx.immediate([&](VkCommandBuffer cmd) {
        image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {320, 200, 1};
        vkCmdCopyBufferToImage(cmd, staging.handle(), image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        image.generateMips(cmd, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    return image;
}

// A retro picture of one colour.
gfx::Image makeRetro(gfx::Context& ctx, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return makeRetro(ctx, [&](std::uint32_t, std::uint32_t) { return Rgb8{r, g, b}; });
}

// An image the tracer must not touch (scenes that do not sample the retro screen).
gfx::Image unusedImage(gfx::Context& ctx) {
    return gfx::Image(ctx, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "unused retro"});
}

// Where a world point lands in the picture, in continuous pixel coordinates (centres at +0.5).
struct PixelPos {
    double x, y;
};
PixelPos project(const Camera& camera, std::uint32_t width, std::uint32_t height, Vec3 p) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const Vec3 v = p - camera.eye;
    const double z = modern::dot(v, b.forward);
    const double tanHalf = std::tan(test::radians(camera.fovYDegrees) * 0.5);
    const double aspect = double(width) / height;
    const double ndcX = modern::dot(v, b.right) / z / (tanHalf * aspect);
    const double ndcY = modern::dot(v, b.up) / z / tanHalf;
    return {(ndcX + 1) * 0.5 * width, (1 - ndcY) * 0.5 * height};
}

// The camera ray through a pixel centre (pinhole).
Vec3 pixelDirection(const Camera& camera, std::uint32_t width, std::uint32_t height, double px, double py) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const float tanHalf = std::tan(test::radians(camera.fovYDegrees) * 0.5f);
    const float aspect = float(width) / float(height);
    const float sx = (2.f * float(px) / float(width) - 1.f) * tanHalf * aspect;
    const float sy = (1.f - 2.f * float(py) / float(height)) * tanHalf;
    return modern::normalize(b.forward + b.right * sx + b.up * sy);
}

std::uint8_t encode(float linear) {
    const float v = evo::post::srgbOetf(linear / (1.f + linear)); // Reinhard, for looking only
    return static_cast<std::uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
}

// lighting + emission, tone mapped, to <EVO_TEST_OUT_DIR>/step4/<name>.png (or into `dir`).
void writePicture(const Frame& f, const std::string& name, const std::filesystem::path& dir) {
    std::vector<std::uint8_t> rgb(std::size_t(f.lighting.width) * f.lighting.height * 3);
    for (std::uint32_t y = 0; y < f.lighting.height; ++y)
        for (std::uint32_t x = 0; x < f.lighting.width; ++x)
            for (std::uint32_t c = 0; c < 3; ++c)
                rgb[(std::size_t(y) * f.lighting.width + x) * 3 + c] =
                    encode(f.lighting.at(x, y, c) + f.emission.at(x, y, c));
    const std::filesystem::path path = dir / (name + ".png");
    image::writePng(path, int(f.lighting.width), int(f.lighting.height), rgb.data());
    std::printf("  wrote %s\n", path.string().c_str());
}

const std::filesystem::path kOutDir = std::filesystem::path(EVO_TEST_OUT_DIR) / "step4";

bool allFinite(const Plane& p) {
    return std::all_of(p.values.begin(), p.values.end(), [](float v) { return std::isfinite(v); });
}

// --- (a)
// -----------------------------------------------------------------------------------------------------

void testEmitterQuad(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    const TestScene scene = test::emitterQuadScene();
    gfx::Image unused = unusedImage(ctx);
    // spp 8 takes two dispatches: the second averages its samples into the first's (half precision).
    for (const std::uint32_t spp : {4u, 8u}) {
        Frame f;
        {
            Tracer tracer(ctx, scene, 64, 64);
            f = tracer.trace(scene.frame, {.spp = spp, .frameSeed = 1}, unused);
            CHECK(tracer.pathTracer().lastStats().traceDispatches == spp / 4);
        }
        // The quad covers |ndc| < 0.5 / (2 tan 20) = 0.687 of the picture: pixels 11 to 52.
        int inside = 0;
        for (std::uint32_t y = 14; y < 50; ++y)
            for (std::uint32_t x = 14; x < 50; ++x) {
                for (std::uint32_t c = 0; c < 3; ++c) {
                    CHECK(std::abs(f.emission.at(x, y, c) - 2.f) <= 1e-6f);
                    CHECK(f.lighting.at(x, y, c) == 0.f);
                    CHECK(f.albedo.at(x, y, c) == 0.f); // the emitter's black base
                }
                ++inside;
            }
        CHECK(inside == 36 * 36);
        // Outside the quad the camera sees the sky: emission, not lighting, and depth -1.
        CHECK(f.depth.at(1, 1) == -1.f);
        CHECK(f.emission.luminance(1, 1) > 0.f && f.emission.luminance(1, 1) < 1.f);
        CHECK(f.albedo.at(1, 1, 0) == 1.f);
        CHECK(f.depth.at(32, 32) > 1.99f && f.depth.at(32, 32) < 2.01f);
        idle(ctx);
    }
    std::printf("(a) emitter quad at spp 4 and 8: emission 2 inside, lighting 0 (%.1f s)\n",
                secondsSince(start));
}

// --- (b)
// -----------------------------------------------------------------------------------------------------

double meanAbsDifference(const Plane& a, const Plane& b) {
    double sum = 0;
    for (std::uint32_t y = 0; y < a.height; ++y)
        for (std::uint32_t x = 0; x < a.width; ++x)
            sum += std::abs(a.luminance(x, y) - b.luminance(x, y));
    return sum / (double(a.width) * a.height);
}

void testCornellMis(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    const TestScene scene = test::cornellBox();
    gfx::Image unused = unusedImage(ctx);
    Tracer tracer(ctx, scene, 64, 64);
    // The luminance clamp after the first bounce is a deliberate bias; it is off here.
    TraceSettings settings{.spp = 64, .maxBounces = 4, .frameSeed = 11, .clampIndirect = 1e30f};
    const Frame neeA = tracer.trace(scene.frame, settings, unused);
    settings.frameSeed = 12;
    const Frame neeB = tracer.trace(scene.frame, settings, unused);
    settings.nee = false;
    const Frame bsdfB = tracer.trace(scene.frame, settings, unused);
    settings.frameSeed = 11;
    const Frame bsdfA = tracer.trace(scene.frame, settings, unused);
    writePicture(neeA, "path_tracer_cornell_nee", kOutDir);
    writePicture(bsdfA, "path_tracer_cornell_bsdf_only", kOutDir);

    const double withNee = 0.5 * (neeA.lighting.meanLuminance() + neeB.lighting.meanLuminance());
    const double without = 0.5 * (bsdfA.lighting.meanLuminance() + bsdfB.lighting.meanLuminance());
    const double spreadNee = meanAbsDifference(neeA.lighting, neeB.lighting);
    const double spreadBsdf = meanAbsDifference(bsdfA.lighting, bsdfB.lighting);
    std::printf(
        "(b) Cornell box mean %.4f with NEE, %.4f without (%.2f %%); seed spread %.4f vs %.4f (%.1f s)\n",
        withNee, without, 100 * (withNee / without - 1), spreadNee, spreadBsdf, secondsSince(start));
    CHECK(withNee > 0.05);
    CHECK(std::abs(withNee / without - 1) < 0.03);
    CHECK(spreadNee < spreadBsdf);
    for (const Frame* f : {&neeA, &neeB, &bsdfA, &bsdfB})
        for (const Plane* p : f->all())
            CHECK(allFinite(*p));
}

// Irradiance at p (normal n) from a polygon of unit radiance (Lambert's formula).
double polygonIrradiance(Vec3 p, Vec3 n, const std::array<Vec3, 4>& corners) {
    double sx = 0, sy = 0, sz = 0;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const Vec3 a = modern::normalize(corners[i] - p);
        const Vec3 b = modern::normalize(corners[(i + 1) % corners.size()] - p);
        const double theta = std::acos(std::clamp(double(modern::dot(a, b)), -1.0, 1.0));
        const Vec3 c = modern::normalize(modern::cross(a, b));
        sx += theta * c.x;
        sy += theta * c.y;
        sz += theta * c.z;
    }
    return 0.5 * std::abs(sx * n.x + sy * n.y + sz * n.z);
}

// The floor (y = 0) of analyticLightScene or twoLightScene seen through each pixel centre, lit directly
// (maxBounces 1) by every light: Lambert's formula for a diffuse floor of albedo `albedo`, green channel.
std::vector<double> analyticDirect(const TestScene& scene, std::uint32_t size, double albedo) {
    std::vector<double> expected(std::size_t(size) * size, 0.0);
    const Camera& camera = scene.frame.camera;
    for (const rt::RectLight& l : scene.frame.lights) {
        const Vec3 corner{l.corner[0], l.corner[1], l.corner[2]};
        const Vec3 u{l.edgeU[0], l.edgeU[1], l.edgeU[2]};
        const Vec3 v{l.edgeV[0], l.edgeV[1], l.edgeV[2]};
        const std::array<Vec3, 4> corners = {corner, corner + u, corner + u + v, corner + v};
        for (std::uint32_t y = 0; y < size; ++y)
            for (std::uint32_t x = 0; x < size; ++x) {
                const Vec3 d = pixelDirection(camera, size, size, x + 0.5, y + 0.5);
                const float t = -camera.eye.y / d.y;
                const Vec3 p = camera.eye + d * t;
                expected[std::size_t(y) * size + x] +=
                    albedo / std::numbers::pi * polygonIrradiance(p, {0, 1, 0}, corners) * l.emission[1];
            }
    }
    return expected;
}

// The relative error of a picture's channel against expected values, over the whole picture and its worst
// block of block x block pixels.
struct Agreement {
    double got = 0, want = 0, worstBlock = 0;
    double error() const { return got / want - 1; }
};
Agreement compareBlocks(const Plane& plane, std::uint32_t channel, const std::vector<double>& expected,
                        std::uint32_t block = 4) {
    Agreement a;
    for (std::uint32_t by = 0; by < plane.height; by += block)
        for (std::uint32_t bx = 0; bx < plane.width; bx += block) {
            double blockGot = 0, blockWant = 0;
            for (std::uint32_t y = by; y < by + block; ++y)
                for (std::uint32_t x = bx; x < bx + block; ++x) {
                    blockGot += plane.at(x, y, channel);
                    blockWant += expected[std::size_t(y) * plane.width + x];
                }
            a.worstBlock = std::max(a.worstBlock, std::abs(blockGot / blockWant - 1));
            a.got += blockGot;
            a.want += blockWant;
        }
    a.got /= double(plane.width) * plane.height;
    a.want /= double(plane.width) * plane.height;
    return a;
}

// environmentLighting (environment.glsl, sky.glsl) of the default NightSky under the town's band (the dark
// ground and half the town's glow), the only part the window scene's panel sees. A port: keep it equal to
// the shader.
Vec3 groundSkyRadiance(Vec3 direction) {
    const Vec3 d = modern::normalize(direction);
    if (!(d.y <= -0.06f)) // kTownDepth: above it the hashed town lights would need porting too
        throw std::logic_error("groundSkyRadiance: a direction above the town's lights");
    const float glow = std::exp(-std::abs(d.y) * 18.f);
    return Vec3{0.0015f, 0.0018f, 0.003f} + Vec3{0.045f, 0.028f, 0.016f} * (0.5f * glow);
}

// The panel of windowScene seen through each pixel centre, lit directly through the window: albedo / pi times
// the irradiance, a midpoint quadrature of sky * cos(panel) * cos(window) / r^2 over the window's area.
std::array<std::vector<double>, 3> windowDirect(const TestScene& scene, std::uint32_t size) {
    constexpr int kSteps = 96;
    const double height = test::kWindowTop - test::kWindowBottom, width = 2 * test::kWindowHalfWidth;
    const double dA = height / kSteps * width / kSteps;
    std::array<std::vector<double>, 3> expected;
    for (auto& e : expected)
        e.assign(std::size_t(size) * size, 0.0);
    const Camera& camera = scene.frame.camera;
    for (std::uint32_t y = 0; y < size; ++y)
        for (std::uint32_t x = 0; x < size; ++x) {
            const Vec3 d = pixelDirection(camera, size, size, x + 0.5, y + 0.5);
            const Vec3 p = camera.eye + d * (-camera.eye.x / d.x); // on the panel, x = 0
            double e[3] = {0, 0, 0};
            for (int i = 0; i < kSteps; ++i)
                for (int j = 0; j < kSteps; ++j) {
                    const Vec3 q{test::kWindowX, float(test::kWindowBottom + (i + 0.5) * height / kSteps),
                                 float(-test::kWindowHalfWidth + (j + 0.5) * width / kSteps)};
                    const Vec3 toWindow = q - p;
                    const double r2 = modern::dot(toWindow, toWindow);
                    const double cosine = toWindow.x / std::sqrt(r2); // the same on the panel and the window
                    const Vec3 sky = groundSkyRadiance(toWindow);
                    const double weight = cosine * cosine / r2 * dA;
                    e[0] += sky.x * weight;
                    e[1] += sky.y * weight;
                    e[2] += sky.z * weight;
                }
            for (int c = 0; c < 3; ++c)
                expected[c][std::size_t(y) * size + x] = test::kPanelAlbedo / std::numbers::pi * e[c];
        }
    return expected;
}

void testAnalyticMis(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    constexpr std::uint32_t kSize = 32;
    gfx::Image unused = unusedImage(ctx);
    const TraceSettings direct{.spp = 64, .maxBounces = 1, .frameSeed = 5, .clampIndirect = 1e30f};

    // A diffuse floor: both estimators equal Lambert's formula.
    {
        const TestScene scene = test::analyticLightScene(test::lambert({0.5f, 0.5f, 0.5f}));
        const std::vector<double> expected = analyticDirect(scene, kSize, 0.5);
        Tracer tracer(ctx, scene, kSize, kSize);
        for (bool nee : {true, false}) {
            TraceSettings settings = direct;
            settings.nee = nee;
            settings.spp = nee ? 64 : 512; // BSDF sampling alone finds the light far less often
            const Frame f = tracer.trace(scene.frame, settings, unused);
            const Agreement a = compareBlocks(f.lighting, 1, expected);
            std::printf(
                "    diffuse floor, NEE %d: mean %.5f, analytic %.5f (%.2f %%), worst 4x4 block %.2f %%\n",
                nee, a.got, a.want, 100 * a.error(), 100 * a.worstBlock);
            CHECK(std::abs(a.error()) < (nee ? 0.01 : 0.02));
            CHECK(a.worstBlock < (nee ? 0.03 : 0.06));
        }
    }
    // The same floor under a light only 0.25 m above it. A shadow ray towards a point on the light leaves
    // from the offset origin, not from the shading point: aimed along the direction from the shading point
    // and cut short by a fraction of the distance, it ended beside the sampled point, past the light's plane
    // for oblique directions (by up to the offset, 1e-4 (1 + |p|), against a margin of 1e-4 d cos), and the
    // light itself occluded it. NEE then lost the oblique part of every close light: here 35 % of the
    // floor's light, in the Cornell box the tall block's top under the ceiling light (the ceiling above it
    // 10-20 % dark, -1.5 % over the frame).
    {
        TestScene scene = test::analyticLightScene(test::lambert({0.5f, 0.5f, 0.5f}));
        const float e = test::kAnalyticEmission;
        const rt::Mat34 light = test::place({0, 0.25f, 0}, test::kX, 90, {1, 1, 1});
        scene.frame.instances[2].transform = light;
        scene.frame.lights = {modern::rectLightForQuad(light, 1, 1, rt::LightKind::Emitter, {e, e, e}, 1.f)};
        scene.frame.camera.eye = {0, 0.2f, 0};
        const std::vector<double> expected = analyticDirect(scene, kSize, 0.5);
        Tracer tracer(ctx, scene, kSize, kSize);
        for (bool nee : {true, false}) {
            TraceSettings settings = direct;
            settings.nee = nee;
            settings.spp = nee ? 64 : 256;
            const Frame f = tracer.trace(scene.frame, settings, unused);
            const Agreement a = compareBlocks(f.lighting, 1, expected);
            std::printf(
                "    light close over the floor, NEE %d: mean %.5f, analytic %.5f (%.2f %%), worst 4x4 "
                "block %.2f %%\n",
                nee, a.got, a.want, 100 * a.error(), 100 * a.worstBlock);
            CHECK(std::abs(a.error()) < 0.01);
            CHECK(a.worstBlock < 0.06); // a close, wide light: NEE is noisier than under the light at 1 m
        }
    }
    // Two lights selected 1 : 3: the floor is the sum of both Lambert terms. Dropping or misplacing the
    // selection probability on either side of MIS (or picking with other odds than lightSelectPdf says)
    // weights the two terms wrongly.
    {
        const TestScene scene = test::twoLightScene();
        const std::vector<double> expected = analyticDirect(scene, kSize, 0.5);
        Tracer tracer(ctx, scene, kSize, kSize);
        for (bool nee : {true, false}) {
            TraceSettings settings = direct;
            settings.nee = nee;
            settings.spp = nee ? 128 : 512; // NEE finds the dimmer light 3 times in 4: noisier than one light
            const Frame f = tracer.trace(scene.frame, settings, unused);
            const Agreement a = compareBlocks(f.lighting, 1, expected);
            std::printf(
                "    two lights, NEE %d: mean %.5f, analytic %.5f (%.2f %%), worst 4x4 block %.2f %%\n", nee,
                a.got, a.want, 100 * a.error(), 100 * a.worstBlock);
            CHECK(std::abs(a.error()) < (nee ? 0.01 : 0.02));
            CHECK(a.worstBlock < 0.1);
        }
    }
    // Glossy and layered floors: no closed form, but NEE + MIS and BSDF sampling alone must agree.
    rt::Material coated = test::lambert({0.6f, 0.4f, 0.3f});
    coated.coat = 1.f;
    coated.coatRoughness = 0.f; // a delta coat over a diffuse base
    for (const auto& [name, floor] : {std::pair{"rough metal", test::metal({0.9f, 0.8f, 0.7f}, 0.4f)},
                                      std::pair{"smooth coat", coated}}) {
        const TestScene scene = test::analyticLightScene(floor);
        Tracer tracer(ctx, scene, kSize, kSize);
        TraceSettings settings = direct;
        settings.spp = 128;
        const double withNee = tracer.trace(scene.frame, settings, unused).lighting.meanLuminance();
        settings.nee = false;
        const double without = tracer.trace(scene.frame, settings, unused).lighting.meanLuminance();
        std::printf("    %s floor: %.5f with NEE, %.5f without (%.2f %%)\n", name, withNee, without,
                    100 * (withNee / without - 1));
        CHECK(withNee > 0.01);
        CHECK(std::abs(withNee / without - 1) < 0.03);
    }
    // A mirror floor: every scattering is a delta lobe, so NEE never runs and a light hit keeps weight 1. The
    // two settings trace the same paths and must give the same bytes.
    {
        const TestScene scene = test::analyticLightScene(test::metal({1, 1, 1}, 0.f));
        Tracer tracer(ctx, scene, kSize, kSize);
        TraceSettings settings = direct;
        settings.spp = 4;
        const Frame withNee = tracer.trace(scene.frame, settings, unused);
        settings.nee = false;
        const Frame without = tracer.trace(scene.frame, settings, unused);
        CHECK(withNee.lighting.bytes == without.lighting.bytes);
        // The camera sees the light in the mirror: F0 = 1, so exactly its emission.
        CHECK(std::abs(withNee.lighting.at(kSize / 2, kSize / 2, 0) - test::kAnalyticEmission) < 1e-4f);
    }
    idle(ctx);
    std::printf("(b) analytic MIS cases (%.1f s)\n", secondsSince(start));
}

// A panel lit through a window portal: NEE through the portal (to the sky beyond) and BSDF rays escaping
// through it, weighted by MIS, sum to the sky's light; so does BSDF sampling alone. A missing or doubled
// escape weight or a wrong portal density biases the NEE run. With two stacked portals an escaping ray
// crosses both, so its NEE density, and a portal sample's, is the sum over the two.
void testPortalMis(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    constexpr std::uint32_t kSize = 32;
    gfx::Image unused = unusedImage(ctx);
    const TraceSettings direct{.spp = 64, .maxBounces = 1, .frameSeed = 21, .clampIndirect = 1e30f};
    const std::array<std::vector<double>, 3> expected = windowDirect(test::windowScene(false), kSize);
    int failures = 0;
    for (bool stacked : {false, true}) {
        const TestScene scene = test::windowScene(stacked);
        Tracer tracer(ctx, scene, kSize, kSize);
        for (bool nee : {true, false}) {
            if (stacked && !nee)
                continue; // without NEE the portals play no part: the same paths as one portal
            TraceSettings settings = direct;
            settings.nee = nee;
            settings.spp = nee ? 64 : 512;
            const Frame f = tracer.trace(scene.frame, settings, unused);
            for (std::uint32_t c = 0; c < 3; ++c) {
                const Agreement a = compareBlocks(f.lighting, c, expected[c], 8);
                std::printf("    %s, NEE %d, channel %u: mean %.6f, quadrature %.6f (%.2f %%), worst 8x8 "
                            "block %.2f %%\n",
                            stacked ? "two stacked portals" : "one portal", nee, c, a.got, a.want,
                            100 * a.error(), 100 * a.worstBlock);
                const bool agrees =
                    std::abs(a.error()) < (nee ? 0.01 : 0.02) && a.worstBlock < (nee ? 0.05 : 0.1);
                failures += agrees ? 0 : 1;
            }
        }
    }
    CHECK(failures == 0);
    idle(ctx);
    std::printf("(b) portal MIS against the sky's quadrature (%.1f s)\n", secondsSince(start));
}

// A retro screen of two colours as a textured light: NEE (the light's uv) and BSDF rays hitting the screen
// (the mesh's uv) must see the same picture, so the floor agrees with and without NEE channel by channel. A
// picture split left and right pins u, one split top and bottom pins v; each colour lights its own side.
void testTwoToneScreen(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    constexpr std::uint32_t kSize = 32;
    const TestScene scene = test::retroScreenScene();
    const Camera& camera = scene.frame.camera;
    const Rgb8 red{255, 0, 0}, green{0, 255, 0};
    gfx::Image leftRed =
        makeRetro(ctx, [&](std::uint32_t x, std::uint32_t) { return x < 160 ? red : green; });
    gfx::Image topRed = makeRetro(ctx, [&](std::uint32_t, std::uint32_t y) { return y < 100 ? red : green; });
    // The red and green lighting around a floor point (3 x 3 pixels).
    auto patch = [&](const Frame& f, Vec3 point) {
        const PixelPos pos = project(camera, kSize, kSize, point);
        const auto px = std::uint32_t(pos.x), py = std::uint32_t(pos.y);
        std::array<double, 2> rg{0, 0};
        for (std::uint32_t y = py - 1; y <= py + 1; ++y)
            for (std::uint32_t x = px - 1; x <= px + 1; ++x) {
                CHECK(f.depth.at(x, y) > 0.f && f.emission.luminance(x, y) == 0.f); // the floor
                rg[0] += f.lighting.at(x, y, 0);
                rg[1] += f.lighting.at(x, y, 1);
            }
        return rg;
    };
    Tracer tracer(ctx, scene, kSize, kSize);
    const TraceSettings direct{.spp = 64, .maxBounces = 1, .frameSeed = 31, .clampIndirect = 1e30f};
    for (const auto& [name, picture] : {std::pair<const char*, gfx::Image*>{"left red", &leftRed},
                                        std::pair<const char*, gfx::Image*>{"top red", &topRed}}) {
        TraceSettings settings = direct;
        const Frame withNee = tracer.trace(scene.frame, settings, *picture);
        settings.nee = false;
        settings.spp = 512;
        const Frame without = tracer.trace(scene.frame, settings, *picture);
        for (std::uint32_t c = 0; c < 2; ++c) {
            double a = 0, b = 0;
            for (std::uint32_t y = 0; y < kSize; ++y)
                for (std::uint32_t x = 0; x < kSize; ++x) {
                    a += withNee.lighting.at(x, y, c);
                    b += without.lighting.at(x, y, c);
                }
            std::printf("    %s, %s: %.5f with NEE, %.5f without (%.2f %%)\n", name, c == 0 ? "red" : "green",
                        a / (kSize * kSize), b / (kSize * kSize), 100 * (a / b - 1));
            CHECK(a > 0 && std::abs(a / b - 1) < 0.03);
        }
        if (picture == &leftRed) {
            const auto left = patch(withNee, test::kFloorLeft), right = patch(withNee, test::kFloorRight);
            std::printf("    left red: floor left red/green %.4f/%.4f, right %.4f/%.4f\n", left[0] / 9,
                        left[1] / 9, right[0] / 9, right[1] / 9);
            CHECK(left[0] > 1.5 * left[1] && right[1] > 1.5 * right[0]);
        } else {
            const auto near = patch(withNee, test::kFloorNear); // nearer the screen's bottom (green) half
            std::printf("    top red: floor near the screen red/green %.4f/%.4f\n", near[0] / 9, near[1] / 9);
            CHECK(near[1] > 1.5 * near[0]);
        }
    }
    idle(ctx);
    std::printf("(b) two-tone retro screen, NEE vs BSDF per channel (%.1f s)\n", secondsSince(start));
}

// --- (c)
// -----------------------------------------------------------------------------------------------------

void testFurnace(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    const TestScene scene = test::furnaceScene();
    gfx::Image unused = unusedImage(ctx);
    // Russian roulette (from bounce 3) ends paths early and divides the survivors by their odds: unbiased,
    // only noisier, so it takes more samples. Its mean must stay within the noise of the run without it.
    double meanWithout = 0, sigmaWithout = 0;
    for (const bool roulette : {false, true}) {
        Frame f;
        {
            Tracer tracer(ctx, scene, 64, 64);
            f = tracer.trace(
                scene.frame,
                {.spp = roulette ? 8u : 2u, .maxBounces = 32, .frameSeed = 3, .russianRoulette = roulette},
                unused);
        }
        double emission = 0, minLighting = 1e9, maxLighting = 0, sum = 0, sumSquares = 0;
        for (std::uint32_t y = 0; y < 64; ++y)
            for (std::uint32_t x = 0; x < 64; ++x) {
                const double l = f.lighting.at(x, y, 0);
                emission += f.emission.at(x, y, 0);
                minLighting = std::min(minLighting, l);
                maxLighting = std::max(maxLighting, l);
                sum += l;
                sumSquares += l * l;
            }
        constexpr double kPixels = 64 * 64;
        emission /= kPixels;
        const double lighting = f.lighting.meanLuminance();
        // The standard error of the mean, from the spread of the pixels (each the mean of spp samples).
        const double sigma =
            std::sqrt(std::max(sumSquares / kPixels - (sum / kPixels) * (sum / kPixels), 0.0) / kPixels);
        std::printf(
            "(c) furnace, roulette %d: emission %.6f, lighting mean %.6f +- %.6f (min %.6f, max %.6f) "
            "(%.1f s)\n",
            roulette, emission, lighting, sigma, minLighting, maxLighting, secondsSince(start));
        CHECK(std::abs(emission - 1.0) < 1e-3);
        CHECK(std::abs(lighting - 1.0) < 0.02);
        if (roulette) {
            CHECK(std::abs(lighting - meanWithout) < 4 * std::hypot(sigma, sigmaWithout));
        } else {
            meanWithout = lighting;
            sigmaWithout = sigma;
        }
        idle(ctx);
    }
}

// --- (d)
// -----------------------------------------------------------------------------------------------------

void testDeterminism(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    const TestScene scene = test::cornellBox();
    gfx::Image unused = unusedImage(ctx);
    const TraceSettings settings{.spp = 2, .frameSeed = 0xC0FFEEu};
    Frame first, again, other;
    {
        Tracer tracer(ctx, scene, 64, 64);
        first = tracer.trace(scene.frame, settings, unused, scene.frame.camera);
        other = tracer.trace(scene.frame, {.spp = 2, .frameSeed = 0xC0FFEFu}, unused, scene.frame.camera);
    }
    idle(ctx);
    {
        Tracer fresh(ctx, scene, 64, 64); // a fresh tracer, after another frame elsewhere
        again = fresh.trace(scene.frame, settings, unused, scene.frame.camera);
    }
    const std::array<const Plane*, 6> a = first.all(), b = again.all();
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK(a[i]->bytes == b[i]->bytes);
    CHECK(first.lighting.bytes != other.lighting.bytes);
    idle(ctx);
    std::printf("(d) same seed, same bytes in all six images; another seed differs (%.1f s)\n",
                secondsSince(start));
}

// --- (e)
// -----------------------------------------------------------------------------------------------------

void testStress(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    const TestScene scene = test::stressScene();
    gfx::Image retro = makeRetro(ctx, 255, 255, 255);
    {
        Tracer tracer(ctx, scene, 64, 64);
        CHECK(tracer.samplesRetro());
        for (bool nee : {true, false}) {
            const Frame f =
                tracer.trace(scene.frame, {.spp = 4, .maxBounces = 6, .frameSeed = 77, .nee = nee}, retro,
                             scene.frame.camera);
            const char* names[] = {"lighting", "emission", "albedo", "normal", "depth", "motion"};
            const std::array<const Plane*, 6> planes = f.all();
            for (std::size_t i = 0; i < planes.size(); ++i) {
                if (!allFinite(*planes[i]))
                    std::printf("    non-finite values in %s (NEE %d)\n", names[i], nee);
                CHECK(allFinite(*planes[i]));
            }
            if (nee)
                writePicture(f, "path_tracer_stress", kOutDir);
        }
    }
    idle(ctx);
    std::printf("(e) stress scene finite everywhere (%.1f s)\n", secondsSince(start));
}

// --- (f)
// -----------------------------------------------------------------------------------------------------

void testDispatches(gfx::Context& ctx) {
    const TestScene scene = test::cornellBox();
    gfx::Image unused = unusedImage(ctx);
    {
        Tracer tracer(ctx, scene, 8, 8);
        for (const auto [spp, dispatches] : {std::pair{8u, 2u}, std::pair{1u, 1u}, std::pair{5u, 2u},
                                             std::pair{4u, 1u}, std::pair{9u, 3u}}) {
            tracer.trace(scene.frame, {.spp = spp}, unused);
            CHECK(tracer.pathTracer().lastStats().traceDispatches == dispatches);
        }
    }
    CHECK(modern::kSppPerDispatch == 4);
    idle(ctx);
    std::printf("(f) spp 8 -> 2 dispatches, 1 -> 1, 5 -> 2\n");
}

// --- (g)
// -----------------------------------------------------------------------------------------------------

void testGuides(gfx::Context& ctx) {
    const TestScene scene = test::planeScene();
    gfx::Image unused = unusedImage(ctx);
    Tracer tracer(ctx, scene, 64, 64);
    const Camera camera = scene.frame.camera;
    const Frame still = tracer.trace(scene.frame, {.frameSeed = 2}, unused, camera);
    Camera before = camera;
    before.eye.x -= 0.1f; // the camera moved 0.1 m to the right since the previous frame
    before.target.x -= 0.1f;
    const Frame moved = tracer.trace(scene.frame, {.frameSeed = 2}, unused, before);
    const Frame none = tracer.trace(scene.frame, {.frameSeed = 2}, unused, std::nullopt);
    // The plane point seen at ndc x was at ndc x + 0.1 / (3 tan 20) in the previous picture.
    const double shift = 0.1 / (3 * std::tan(test::radians(20))) * 32;
    double worstMotion = 0;
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) {
            CHECK(std::abs(still.depth.at(x, y) - 3.f) < 1e-3f);
            CHECK(std::abs(still.normal.at(x, y, 0)) < 1e-3f && std::abs(still.normal.at(x, y, 1)) < 1e-3f &&
                  std::abs(still.normal.at(x, y, 2) - 1.f) < 1e-3f);
            CHECK(std::abs(still.normal.at(x, y, 3) - test::kPlaneRoughness) < 1e-3f);
            CHECK(std::abs(still.motion.at(x, y, 0)) < 1e-3f && std::abs(still.motion.at(x, y, 1)) < 1e-3f &&
                  still.motion.at(x, y, 2) == 1.f);
            worstMotion = std::max({worstMotion, std::abs(moved.motion.at(x, y, 0) - shift),
                                    double(std::abs(moved.motion.at(x, y, 1)))});
            CHECK(moved.motion.at(x, y, 2) == 1.f);
            CHECK(none.motion.at(x, y, 2) == 0.f);
            CHECK(std::abs(still.albedo.at(x, y, 0) - 0.5f) < 1e-3f);
        }
    std::printf("(g) guides: depth 3, normal +z, motion shift %.4f px within %.5f\n", shift, worstMotion);
    CHECK(worstMotion < 0.01);
    idle(ctx);
}

// --- (h)
// -----------------------------------------------------------------------------------------------------

void testRetroScreen(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    const TestScene scene = test::retroScreenScene();
    gfx::Image red = makeRetro(ctx, 255, 0, 0);
    gfx::Image green = makeRetro(ctx, 0, 128, 0);
    const Camera& camera = scene.frame.camera;
    const PixelPos screen = project(camera, 64, 64, test::kScreenCentre);
    const PixelPos floor = project(camera, 64, 64, test::kFloorPatch);
    const auto sx = std::uint32_t(screen.x), sy = std::uint32_t(screen.y);
    const auto fx = std::uint32_t(floor.x), fy = std::uint32_t(floor.y);
    std::printf("    screen centre at pixel %u,%u, floor patch at %u,%u\n", sx, sy, fx, fy);
    Frame withRed, withGreen;
    {
        Tracer tracer(ctx, scene, 64, 64);
        CHECK(tracer.samplesRetro());
        const TraceSettings settings{.spp = 16, .frameSeed = 9};
        withRed = tracer.trace(scene.frame, settings, red);
        withGreen = tracer.trace(scene.frame, settings, green);
    }
    const float s = test::kScreenScale;
    // Byte 255 is 1.0 and byte 128 is srgbToLinear(128 / 255) = 0.2158, decoded once by the sRGB view. The
    // screen shows them as a CRT (shaders/rt/screen.glsl): plus the glass's glow, times a scanline and
    // vignette factor k in [0.75 * 0.95, 1] that is the same for both pictures (same seed, same samples).
    const float glow[3] = {0.0012f, 0.0016f, 0.0018f}; // kScreenGlow
    const float k = withRed.emission.at(sx, sy, 0) / ((1.f + glow[0]) * s);
    std::printf("    screen factor at the centre pixel %.4f\n", k);
    CHECK(k > 0.75f * 0.95f && k <= 1.f + 1e-4f);
    CHECK(std::abs(withRed.emission.at(sx, sy, 1) - glow[1] * k * s) < 1e-3f * s);
    CHECK(std::abs(withRed.emission.at(sx, sy, 2) - glow[2] * k * s) < 1e-3f * s);
    CHECK(std::abs(withGreen.emission.at(sx, sy, 1) - (0.2158f + glow[1]) * k * s) < 1e-3f * s);
    CHECK(std::abs(withGreen.emission.at(sx, sy, 0) - glow[0] * k * s) < 1e-3f * s);
    // The floor in front of the screen takes its colour.
    double redR = 0, redG = 0, greenR = 0, greenG = 0;
    for (std::uint32_t y = fy - 1; y <= fy + 1; ++y)
        for (std::uint32_t x = fx - 1; x <= fx + 1; ++x) {
            CHECK(withRed.depth.at(x, y) > 0.f && withRed.emission.luminance(x, y) == 0.f); // the floor
            redR += withRed.lighting.at(x, y, 0);
            redG += withRed.lighting.at(x, y, 1);
            greenR += withGreen.lighting.at(x, y, 0);
            greenG += withGreen.lighting.at(x, y, 1);
        }
    std::printf("(h) retro screen: emission red %.4f, green %.4f; floor red/green %.4f/%.4f vs %.4f/%.4f "
                "(%.1f s)\n",
                withRed.emission.at(sx, sy, 0), withGreen.emission.at(sx, sy, 1), redR / 9, redG / 9,
                greenR / 9, greenG / 9, secondsSince(start));
    CHECK(redR > 0.01 && redR > 20 * redG);
    CHECK(greenG > 0.002 && greenG > 20 * greenR);
    writePicture(withRed, "path_tracer_retro_red", kOutDir);
    idle(ctx);
}

// --- Seeds and refusals
// --------------------------------------------------------------------------------------

void testSeeds() {
    CHECK(modern::frameSeedFor(0.0, 0) == rt::pcgHash(0));
    CHECK(modern::frameSeedFor(1.0, 0) == rt::pcgHash(48000));
    CHECK(modern::frameSeedFor(1.0 / 96000.0, 0) == rt::pcgHash(1)); // llround: half away from zero
    CHECK(modern::frameSeedFor(2.5, 3) == rt::pcgHash(120000u + 0x9E3779B9u * 3u));
    CHECK(modern::frameSeedFor(1.0, 1) != modern::frameSeedFor(1.0, 0));
    CHECK(modern::frameSeedFor(1.0, 0) != modern::frameSeedFor(1.0 + 1.0 / 60, 0));
    // A long show: the sample index wraps modulo 2^32 rather than overflowing.
    CHECK(modern::frameSeedFor(100000.0, 0) == rt::pcgHash(std::uint32_t(4800000000ull & 0xFFFFFFFFull)));
}

void testRefusals(gfx::Context& ctx) {
    std::printf("expecting refused scenes and frames:\n");
    TestScene scene = test::cornellBox();
    gfx::Image unused = unusedImage(ctx);
    {
        modern::SceneStatic noMaterials = scene.statics;
        noMaterials.materials.clear();
        CHECK(throws<std::invalid_argument>([&] { modern::SceneGpu(ctx, noMaterials); }));
        modern::SceneStatic badClass = scene.statics;
        badClass.materials[0].materialClass = 4;
        CHECK(throws<std::invalid_argument>([&] { modern::SceneGpu(ctx, badClass); }));
        modern::SceneStatic overCapacity = scene.statics;
        overCapacity.capacity.materials = 2;
        CHECK(throws<std::invalid_argument>([&] { modern::SceneGpu(ctx, overCapacity); }));
        gfx::FrameRing ring(ctx);
        CHECK(throws<std::invalid_argument>([&] { modern::PathTracer(ctx, ring, 0, 8, {}); }));
    }
    idle(ctx);
    const modern::SceneGpu gpu(ctx, scene.statics);
    gfx::FrameRing ring(ctx);
    modern::PathTracer tracer(ctx, ring, 8, 8, modern::tracerCapacity(scene.statics.capacity));
    CHECK(!gpu.samplesRetro());
    CHECK(throws<std::logic_error>(
        [&] { tracer.trace(VK_NULL_HANDLE, gpu, scene.frame, std::nullopt, {}, &unused); })); // not recording
    VkCommandBuffer cmd = ring.begin();
    auto refuses = [&](auto exception, modern::SceneFrame frame, TraceSettings settings = {}) {
        return throws<decltype(exception)>(
            [&] { tracer.trace(cmd, gpu, frame, std::nullopt, settings, &unused); });
    };
    CHECK(refuses(std::invalid_argument(""), scene.frame, {.spp = 0}));
    modern::SceneFrame many = scene.frame;
    many.instances.resize(scene.statics.capacity.instances + 1, scene.frame.instances[0]);
    CHECK(refuses(std::length_error(""), many));
    modern::SceneFrame manyLights = scene.frame;
    manyLights.lights.resize(scene.statics.capacity.lights + 1, scene.frame.lights[0]);
    CHECK(refuses(std::length_error(""), manyLights));
    // A per-frame material table: its classes are hit groups, and it may not bring the retro screen into a
    // scene traced without it.
    modern::SceneFrame badTable = scene.frame;
    badTable.materials = scene.statics.materials;
    badTable.materials[2].materialClass = 7;
    CHECK(refuses(std::invalid_argument(""), badTable));
    badTable.materials = scene.statics.materials;
    badTable.materials[2].texture = rt::kRetroScreenTexture;
    CHECK(refuses(std::invalid_argument(""), badTable));
    // A sphere instance needs the spheres.
    modern::SceneFrame sphere = scene.frame;
    sphere.instances[0].kind = rt::GeometryKind::Spheres;
    CHECK(refuses(std::invalid_argument(""), sphere));
    modern::SceneFrame badMesh = scene.frame;
    badMesh.instances[0].mesh = 99;
    CHECK(refuses(std::out_of_range(""), badMesh));
    modern::SceneFrame badMaterial = scene.frame;
    badMaterial.instances[0].material = 99;
    CHECK(refuses(std::out_of_range(""), badMaterial));
    modern::SceneFrame badLight = scene.frame;
    badLight.instances[0].light = 1;
    CHECK(refuses(std::out_of_range(""), badLight));

    // A light and the instance that is its surface must describe one emitter (MIS weighs NEE towards the
    // light against BSDF rays hitting the instance). The Cornell box's light 0 is instance 8, an Emitter
    // of 12.
    constexpr std::size_t kLightInstance = 8;
    CHECK(scene.frame.instances[kLightInstance].light == 0);
    auto withLight = [&](auto change) {
        modern::SceneFrame frame = scene.frame;
        change(frame.lights[0], frame.instances[kLightInstance]);
        return frame;
    };
    using Light = rt::RectLight;
    using Instance = modern::SceneInstance;
    // Emission other than the material's emission * emissionScale.
    CHECK(refuses(std::invalid_argument(""), withLight([](Light& l, Instance&) { l.emission[1] = 12.01f; })));
    // An area other than |edgeU x edgeV|.
    CHECK(refuses(std::invalid_argument(""), withLight([](Light& l, Instance&) { l.area *= 1.001f; })));
    // A mirroring transform: the instance emits from the side the light does not.
    CHECK(refuses(std::invalid_argument(""), withLight([](Light&, Instance& in) {
                      in.transform = modern::compose(in.transform, modern::scale({-1, 1, 1}));
                  })));
    CHECK(throws<std::invalid_argument>([&] {
        modern::rectLightForQuad(modern::scale({1, -1, 1}), 1, 1, rt::LightKind::Emitter, {1, 1, 1}, 1.f);
    }));
    // A light no instance names is checked too: its kind and its area (a portal's included).
    modern::SceneFrame badPortal = scene.frame;
    badPortal.lights.push_back(modern::rectLightForQuad(test::place({0, 1.9f, 0}, test::kX, 90, {1, 1, 1}), 1,
                                                        1, rt::LightKind::Portal, {0, 0, 0}, 1.f));
    modern::SceneFrame badKind = badPortal;
    badKind.lights.back().kind = 4; // no LightKind
    CHECK(refuses(std::invalid_argument(""), badKind));
    badKind.lights.back().kind = 3; // Tree: the spheres
    CHECK(refuses(std::invalid_argument(""), badKind));
    // A gate names a Portal light of the frame (the Cornell box's light 0 is an Emitter).
    modern::SceneFrame gated = badPortal;
    gated.lights[0].gate = 0;
    CHECK(refuses(std::invalid_argument(""), gated));
    gated.lights[0].gate = 7;
    CHECK(refuses(std::invalid_argument(""), gated));
    badPortal.lights.back().area = 2.f;
    CHECK(refuses(std::invalid_argument(""), badPortal));
    // Nothing was recorded: the frame still traces, here with the emitter gated by the portal; a second
    // trace in the frame is refused.
    gated.lights[0].gate = 1; // the portal
    tracer.trace(cmd, gpu, gated, std::nullopt, {}, &unused);
    CHECK(throws<std::logic_error>([&] { tracer.trace(cmd, gpu, scene.frame, std::nullopt, {}, &unused); }));
    ring.wait(ring.submit());
    idle(ctx);

    // The retro screen: its light must be a RetroScreen light of emission (s, s, s), s = the screen
    // material's emissionScale, on an instance of a kRetroScreenTexture material.
    {
        const TestScene screenScene = test::retroScreenScene();
        gfx::Image retro = makeRetro(ctx, 255, 255, 255);
        const modern::SceneGpu screenGpu(ctx, screenScene.statics);
        gfx::FrameRing screenRing(ctx);
        modern::PathTracer screenTracer(ctx, screenRing, 8, 8,
                                        modern::tracerCapacity(screenScene.statics.capacity));
        constexpr std::size_t kScreenInstance = 2;
        CHECK(screenScene.frame.instances[kScreenInstance].light == 0);
        VkCommandBuffer screenCmd = screenRing.begin();
        auto screenRefuses = [&](auto change) {
            modern::SceneFrame frame = screenScene.frame;
            change(frame.lights[0], frame.instances[kScreenInstance]);
            return throws<std::invalid_argument>(
                [&] { screenTracer.trace(screenCmd, screenGpu, frame, std::nullopt, {}, &retro); });
        };
        // A portal is an opening, not a surface (its emission (2, 2, 2) would pass for the screen's).
        CHECK(screenRefuses(
            [](Light& l, Instance&) { l.kind = static_cast<std::uint32_t>(rt::LightKind::Portal); }));
        CHECK(screenRefuses([](Light& l, Instance&) { l.emission[2] = 2.5f; }));
        CHECK(
            screenRefuses([](Light& l, Instance&) { l.emission[0] = l.emission[1] = l.emission[2] = 1.f; }));
        // An Emitter on the textured material (its own emission * scale is 0 here, so only the texture is
        // wrong).
        CHECK(screenRefuses([](Light& l, Instance&) {
            l.kind = static_cast<std::uint32_t>(rt::LightKind::Emitter);
            l.emission[0] = l.emission[1] = l.emission[2] = 0.f;
        }));
        // A RetroScreen light on an instance of the white floor material, which has no texture (emissionScale
        // 1, so the emission (1, 1, 1) would pass).
        CHECK(screenRefuses([](Light& l, Instance& in) {
            in.material = 0;
            l.emission[0] = l.emission[1] = l.emission[2] = 1.f;
        }));
        // A scene that shows the retro screen needs the retro image.
        CHECK(throws<std::invalid_argument>(
            [&] { screenTracer.trace(screenCmd, screenGpu, screenScene.frame, std::nullopt, {}, nullptr); }));
        screenTracer.trace(screenCmd, screenGpu, screenScene.frame, std::nullopt, {}, &retro);
        screenRing.wait(screenRing.submit());
        idle(ctx);
    }
}

// A second trace in one frame is refused before it writes anything, so the frame's first trace
// still reads its own uniforms, instances and lights when it runs.
void testSecondTrace(gfx::Context& ctx) {
    std::printf("expecting a refused second trace in one frame:\n");
    const TestScene scene = test::cornellBox();
    gfx::Image unused = unusedImage(ctx);
    Tracer tracer(ctx, scene, 16, 16);
    const TraceSettings settings{.spp = 1, .frameSeed = 4};
    const Frame once = tracer.trace(scene.frame, settings, unused);
    tracer.trace(scene.frame, settings, unused); // back to the same slot of the ring
    modern::SceneFrame moved = scene.frame;
    moved.camera.eye.x += 0.3f;
    const Frame twice = tracer.trace(scene.frame, settings, unused, std::nullopt, [&](VkCommandBuffer cmd) {
        CHECK(throws<std::logic_error>([&] {
            tracer.pathTracer().trace(cmd, tracer.scene(), moved, std::nullopt, {.spp = 2, .frameSeed = 5},
                                      &unused);
        }));
    });
    for (std::size_t i = 0; i < 6; ++i)
        CHECK(once.all()[i]->bytes == twice.all()[i]->bytes);
    idle(ctx);
}

// --- (i) ---------------------------------------------------------------------------------------------------

// A shader directory with the path tracer's misses and closest-hits from the build and the payload probe as
// its raygen (rt_pathtrace.rgen.spv): a PathTracer made while EVO_SHADER_DIR names it traces the probe.
std::filesystem::path payloadProbeShaders() {
    const std::filesystem::path dir = std::filesystem::path(EVO_TEST_OUT_DIR) / "payload_probe_shaders";
    std::filesystem::create_directories(dir);
    const std::filesystem::path built(EVO_TEST_SHADER_DIR);
    for (const char* name :
         {"rt_sky.rmiss.spv", "rt_shadow.rmiss.spv", "rt_pane.rmiss.spv", "rt_pbr.rchit.spv",
          "rt_glass.rchit.spv", "rt_emissive.rchit.spv", "rt_checker.rchit.spv", "rt_pbr_sphere.rchit.spv",
          "rt_glass_sphere.rchit.spv", "rt_emissive_sphere.rchit.spv", "rt_checker_sphere.rchit.spv",
          "rt_sphere.rint.spv"})
        std::filesystem::copy_file(built / name, dir / name,
                                   std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(std::filesystem::path(EVO_TEST_SPIRV_DIR) / "test_payload_probe.rgen.spv",
                               dir / "rt_pathtrace.rgen.spv",
                               std::filesystem::copy_options::overwrite_existing);
    return dir;
}

void testPayload(gfx::Context& ctx) {
    // One material per class, each with its own flags, Abbe number and medium; a sphere (many triangles), a
    // box and two quads in a 2 x 2 grid in front of the camera.
    TestScene scene;
    scene.statics.meshes = test::basicMeshes();
    rt::Material pbr = test::lambert({0.5f, 0.5f, 0.5f});
    pbr.flags = rt::kMaterialCausticReceiver;
    pbr.abbe = 0.f;
    pbr.medium = 3;
    rt::Material glass = test::glass(1.5f, 0.f);
    glass.flags = rt::kMaterialThinWalled | rt::kMaterialCausticReceiver;
    glass.abbe = 55.5f;
    glass.medium = 0;
    rt::Material emissive = test::emitter({1, 1, 1});
    emissive.flags = rt::kMaterialCameraOnlyEmission;
    emissive.abbe = 12.25f; // (a value, not a meaning: the payload carries it whatever the class)
    rt::Material checker = test::lambert({0.2f, 0.2f, 0.2f});
    checker.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Checker);
    checker.flags = 7;
    checker.abbe = 30.f;
    checker.medium = 9;
    scene.statics.materials = {pbr, glass, emissive, checker};
    const std::array<std::uint32_t, 4> meshOf{test::kSphereMesh, test::kCubeMesh, test::kQuadMesh,
                                              test::kQuadMesh};
    const std::array<Vec3, 4> at{Vec3{-0.6f, 0.6f, -3}, Vec3{0.6f, 0.6f, -3}, Vec3{-0.6f, -0.6f, -3},
                                 Vec3{0.6f, -0.6f, -3}};
    for (std::uint32_t i = 0; i < 4; ++i) {
        const float size = meshOf[i] == test::kSphereMesh ? 0.5f : 1.f;
        scene.frame.instances.push_back({meshOf[i], i, test::place(at[i], test::kY, 25, {size, size, size})});
    }
    scene.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 60.f};

    const std::filesystem::path dir = payloadProbeShaders();
    gfx::Image unused = unusedImage(ctx);
    test::setEnv("EVO_SHADER_DIR", dir.string().c_str());
    std::optional<Tracer> tracer;
    try {
        tracer.emplace(ctx, scene, 64, 64);
    } catch (...) {
        test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        throw;
    }
    test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
    const Frame f = tracer->trace(scene.frame, {.spp = 1, .frameSeed = 1, .seconds = 12.5f}, unused);

    std::array<int, 4> pixels{};
    std::array<std::vector<bool>, 4> primitives;
    for (std::uint32_t c = 0; c < 4; ++c)
        primitives[c].assign(scene.statics.meshes[meshOf[c]].indices.size() / 3, false);
    int misses = 0;
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x) {
            CHECK(f.albedo.at(x, y, 0) == 12.5f); // FrameUniforms::seconds
            if (f.lighting.at(x, y, 0) == -2.f) {
                ++misses;
                continue;
            }
            const float classPlusOne = f.emission.at(x, y, 3);
            CHECK(classPlusOne >= 1.f && classPlusOne <= 4.f);
            const auto c = static_cast<std::uint32_t>(classPlusOne) - 1;
            const rt::Material& m = scene.statics.materials[c]; // material i has class i
            CHECK(m.materialClass == c);
            CHECK(f.lighting.at(x, y, 0) == float(m.flags));
            CHECK(f.lighting.at(x, y, 1) == m.abbe);
            CHECK(f.lighting.at(x, y, 2) == (m.medium == rt::kNoMedium ? -1.f : float(m.medium)));
            const float primitive = f.lighting.at(x, y, 3);
            CHECK(primitive >= 0.f && primitive < float(primitives[c].size()) &&
                  primitive == std::floor(primitive));
            primitives[c][static_cast<std::size_t>(primitive)] = true;
            for (std::uint32_t k = 0; k < 3; ++k)
                CHECK(f.emission.at(x, y, k) == 0.f); // prevPosition == position until Task 9
            ++pixels[c];
        }
    for (std::uint32_t c = 0; c < 4; ++c)
        CHECK(pixels[c] > 50);
    const auto distinct = [](const std::vector<bool>& seen) {
        return std::count(seen.begin(), seen.end(), true);
    };
    CHECK(distinct(primitives[0]) >= 20); // the sphere: many triangles
    CHECK(distinct(primitives[1]) >= 2);  // the box: at least two of its faces' triangles
    CHECK(distinct(primitives[2]) == 2 && distinct(primitives[3]) == 2); // both triangles of each quad
    CHECK(misses > 0);
    std::printf("(i) hit payload: 4 closest-hits write flags, abbe, medium, primitive (sphere %ld of %zu "
                "triangles seen) and prevPosition\n",
                long(distinct(primitives[0])), primitives[0].size());
    tracer.reset();
    idle(ctx);
}

// --- Look
// ----------------------------------------------------------------------------------------------------

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp) {
    const auto start = std::chrono::steady_clock::now();
    const TestScene scene = test::lookScene();
    gfx::Image unused = unusedImage(ctx);
    Tracer tracer(ctx, scene, 320, 240);
    const Frame f = tracer.trace(scene.frame, {.spp = spp, .maxBounces = 6, .frameSeed = 1}, unused);
    writePicture(f, "path_tracer_look_" + std::to_string(spp) + "spp", dir);
    std::printf("look: %.1f s\n", secondsSince(start));
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
    try {
        testSeeds();
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the results are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("warning: synchronization validation unavailable\n");
            if (argc >= 3 && std::string(argv[1]) == "--look") {
                renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 128u);
                return 0;
            }
            testRefusals(ctx);
            testSecondTrace(ctx);
            testDispatches(ctx);
            testEmitterQuad(ctx);
            testGuides(ctx);
            testFurnace(ctx);
            testDeterminism(ctx);
            testRetroScreen(ctx);
            testStress(ctx);
            testAnalyticMis(ctx);
            testPortalMis(ctx);
            testTwoToneScreen(ctx);
            testCornellMis(ctx);
            testPayload(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("path_tracer_test: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
