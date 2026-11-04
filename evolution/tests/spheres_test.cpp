// Analytic spheres and the particle field on lavapipe, with the validation layer and its
// synchronization validation on; fails on any message.
//
// (m) the sphere math (shaders/rt/sphere.glsl, run by a compute probe) against the double-precision CPU
//     reference: hits from outside, from inside, behind, clipped by tMin and tMax, unnormalised directions,
//     a 2 mm sphere a kilometre away (the robust quadratic), and the spherical uv (modern::uvSphere's map);
// (a) 4 static spheres in two ranges (one range placed twice): the depth guide equals the CPU ray-sphere
//     depth within 1e-4 and the normals are radial within 1e-3, every pixel;
// (b) a smooth glass sphere matches the uvSphere(r, 64, 128) glass sphere's mean within 2 % at 32 spp;
// (c) the particle field: test_particles_grid.comp drives 4096 particles moving with time over 32 frames,
//     two in flight; at 5 probe pixels each frame's depth equals the analytic value for its own time, and
//     every particle's centre and prevCenter (read back from that frame's slot) equal the C++ mirror at its
//     time and at the previous frame's;
// (d) refusals: a count above capacity, a field recorded outside a frame, the frame checks (no such range,
//     a dynamic instance without a field or not the whole field, a sphere light, a class mismatch, particles
//     beyond capacity) and a trace without the field's BLAS;
// (e) the Emissive and Checker classes on spheres show emission (material + colour * emission) and both
//     checker colours (times the sphere's colour); Pbr is tinted by the colour;
// (f) a camera inside a glass sphere sees a finite, non-black picture, and its depth guide is the sphere's
//     inside (an inside hit);
// (p) the hit payload of spheres (the payload probe raygen of path_tracer_test): primitive = the sphere's
//     index in its buffer, prevPosition - position = prevCenter - center, static and particle;
// (s) the modern stage: a scene with a particle field prepares one field (its bytes measured by the
//     allocator), and an export frame after another time equals a fresh one (stateless particles).
// rt_pipeline_test checks the 8-group SBT layout on RTX's properties (g).
//
// `spheres_test --look <dir> [spp]` renders a picture to look at instead (chrome, glass, gold and emissive
// spheres on a checker floor, and a particle vortex).

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/mesh_builder.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scene_gpu.hpp"
#include "post/tonemap_reference.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "rt/particles.hpp"
#include "rt/spheres.hpp"
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
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;
using modern::Camera;
using modern::SceneInstance;
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

const std::filesystem::path kSpirvDir = EVO_TEST_SPIRV_DIR;
const std::filesystem::path kParticleShader = kSpirvDir / "test_particles_grid.comp.spv";

// Waits for the queue: synchronization validation in layer 1.3.275 credits no host wait on the timeline, so
// memory reused between subtests would read as a hazard.
void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

// --- Readback ----------------------------------------------------------------------------------------------

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

struct Plane {
    std::uint32_t width = 0, height = 0, channels = 0;
    std::vector<float> values;
    float at(std::uint32_t x, std::uint32_t y, std::uint32_t c = 0) const {
        return values[(std::size_t(y) * width + x) * channels + c];
    }
};

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
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "spheres test readback"}),
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

// The records of a buffer (a particle field slot's), copied within the frame being recorded.
gfx::Buffer recordCopy(gfx::Context& ctx, VkCommandBuffer cmd, const gfx::Buffer& source,
                       VkDeviceSize bytes) {
    gfx::Buffer copy(ctx, {bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "spheres test records"});
    const VkBufferCopy region{0, 0, bytes};
    vkCmdCopyBuffer(cmd, source.handle(), copy.handle(), 1, &region);
    gfx::bufferBarrier2(cmd, copy.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    return copy;
}

struct Frame {
    Plane lighting, emission, albedo, normal, depth;
};

// A path tracer for one scene with its own ring and, when the scene has a particle shader, its own particle
// field (declared in the order they must outlive each other: the tracer's TLAS goes first and waits for
// the ring, then the field, which waits too, then the ring and the scene's GPU side).
class Tracer {
public:
    Tracer(gfx::Context& ctx, const TestScene& scene, std::uint32_t width, std::uint32_t height,
           bool refit = false)
        : ctx_(ctx), gpu_(ctx, scene.statics), ring_(ctx),
          particles_(scene.statics.particleShader.empty()
                         ? nullptr
                         : std::make_unique<rt::ParticleField>(ctx, ring_,
                                                               std::max(scene.statics.capacity.particles, 1u),
                                                               scene.statics.particleShader, refit)),
          tracer_(ctx, ring_, width, height, modern::tracerCapacity(scene.statics.capacity)) {}

    // Records the frame's particle field (at `seconds`, prevCenter at `previous`) and the trace; `after`
    // runs in the same frame before the readback.
    void record(VkCommandBuffer cmd, const modern::SceneFrame& frame, const TraceSettings& settings,
                double seconds = 0, double previous = 0) {
        modern::TraceExtras extras;
        if (frame.particles) {
            particles_->record(cmd, frame.particles->count, seconds, previous, frame.particles->params,
                               frame.particles->material);
            extras.particleSpheres = &particles_->records();
            extras.particleBlas = particles_->blasAddress();
        }
        tracer_.trace(cmd, gpu_, frame, std::nullopt, settings, &unused_, extras);
    }

    Frame trace(const modern::SceneFrame& frame, const TraceSettings& settings, double seconds = 0,
                double previous = 0) {
        VkCommandBuffer cmd = ring_.begin();
        record(cmd, frame, settings, seconds, previous);
        modern::GBuffer& g = tracer_.gbuffer();
        std::vector<PendingRead> reads;
        for (gfx::Image* image : {&g.lighting, &g.emission, &g.albedo, &g.normal, &g.depth})
            reads.push_back(recordRead(ctx_, cmd, *image));
        ring_.wait(ring_.submit());
        idle(ctx_);
        return {finishRead(reads[0]), finishRead(reads[1]), finishRead(reads[2]), finishRead(reads[3]),
                finishRead(reads[4])};
    }

    gfx::FrameRing& ring() { return ring_; }
    modern::PathTracer& pathTracer() { return tracer_; }
    rt::ParticleField& particles() { return *particles_; }
    const modern::SceneGpu& scene() const { return gpu_; }

private:
    gfx::Context& ctx_;
    gfx::Image unused_{ctx_, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "unused retro"}};
    modern::SceneGpu gpu_;
    gfx::FrameRing ring_;
    std::unique_ptr<rt::ParticleField> particles_;
    modern::PathTracer tracer_;
};

// --- Geometry on the CPU -----------------------------------------------------------------------------------

rt::Vec3d d3(Vec3 v) {
    return {double(v.x), double(v.y), double(v.z)};
}

// The camera ray through a pixel centre (pinhole), exactly as pathtrace.rgen's cameraDirection in floats.
Vec3 pixelDirection(const Camera& camera, std::uint32_t width, std::uint32_t height, std::uint32_t x,
                    std::uint32_t y) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const float tanHalf = std::tan(camera.fovYDegrees * std::numbers::pi_v<float> / 360.f);
    const float aspect = float(width) / float(height);
    const float sx = (2.f * (float(x) + 0.5f) / float(width) - 1.f) * tanHalf * aspect;
    const float sy = (1.f - 2.f * (float(y) + 0.5f) / float(height)) * tanHalf;
    return modern::normalize(b.forward + b.right * sx + b.up * sy);
}

struct SphereAt {
    Vec3 center;
    float radius;
};

// The nearest hit of a ray among spheres: the sphere and t, or none. `ambiguous` is set when the ray passes
// within a relative 1e-3 of a sphere's silhouette (float and double may disagree there).
struct NearestHit {
    int sphere = -1;
    double t = 0;
    bool inside = false;
};
NearestHit nearest(const std::vector<SphereAt>& spheres, Vec3 origin, Vec3 direction, bool* ambiguous) {
    NearestHit best;
    for (std::size_t i = 0; i < spheres.size(); ++i) {
        const SphereAt& s = spheres[i];
        const rt::Vec3d o = d3(origin), d = d3(direction), c = d3(s.center);
        if (ambiguous) {
            const rt::Vec3d f{o[0] - c[0], o[1] - c[1], o[2] - c[2]};
            const double along = f[0] * d[0] + f[1] * d[1] + f[2] * d[2];
            const double dist2 = f[0] * f[0] + f[1] * f[1] + f[2] * f[2] - along * along;
            if (along < 0 && std::abs(std::sqrt(std::max(dist2, 0.0)) - s.radius) < 1e-3 * s.radius)
                *ambiguous = true;
        }
        const auto hit = rt::intersectSphere(o, d, c, s.radius, 0.0, 1e30);
        if (hit && (best.sphere < 0 || hit->t < best.t))
            best = {int(i), hit->t, hit->inside};
    }
    return best;
}

// The sphere the ray hits first if its line passes within `fraction` of that sphere's radius of its centre
// (so every ray within the pixel around it hits it too), else -1.
int wellInside(const std::vector<SphereAt>& spheres, Vec3 origin, Vec3 direction, double fraction) {
    const NearestHit hit = nearest(spheres, origin, direction, nullptr);
    if (hit.sphere < 0)
        return -1;
    const SphereAt& s = spheres[hit.sphere];
    const Vec3 f = origin - s.center;
    const float along = modern::dot(f, direction);
    const double dist2 = modern::dot(f, f) - along * along;
    return std::sqrt(std::max(dist2, 0.0)) < fraction * s.radius ? hit.sphere : -1;
}

// --- (m) ---------------------------------------------------------------------------------------------------

struct SphereCase { // shaders/test/sphere_probe.comp
    float origin[3];
    float tMin;
    float direction[3];
    float tMax;
    float center[3];
    float radius;
};
static_assert(sizeof(SphereCase) == 48);

SphereCase sphereCase(Vec3 o, Vec3 d, Vec3 c, float r, float tMin = 0.f, float tMax = 1e30f) {
    return {{o.x, o.y, o.z}, tMin, {d.x, d.y, d.z}, tMax, {c.x, c.y, c.z}, r};
}

void testSphereMath(gfx::Context& ctx) {
    std::vector<SphereCase> cases;
    std::mt19937 rng(17);
    std::uniform_real_distribution<float> unit(-1.f, 1.f);
    auto randomUnit = [&] {
        for (;;) {
            const Vec3 v{unit(rng), unit(rng), unit(rng)};
            if (modern::dot(v, v) > 0.01f && modern::dot(v, v) <= 1.f)
                return modern::normalize(v);
        }
    };
    // Rays from outside at random points of random spheres (hits, misses, behind), with random lengths.
    for (int i = 0; i < 400; ++i) {
        const Vec3 c{3 * unit(rng), 3 * unit(rng), 3 * unit(rng)};
        const float r = 0.05f + std::abs(unit(rng));
        const Vec3 o = c + randomUnit() * (r * (1.5f + 4.f * std::abs(unit(rng))));
        const Vec3 aim = c + randomUnit() * (r * 1.3f * std::abs(unit(rng)));
        const float length = i % 3 == 0 ? 0.3f : i % 3 == 1 ? 1.f : 7.f;
        Vec3 d = modern::normalize(aim - o) * length;
        if (i % 17 == 0)
            d = -d; // the sphere behind
        cases.push_back(sphereCase(o, d, c, r));
    }
    // Origins inside (an exit hit), and tMin past the entry (an exit hit from outside).
    for (int i = 0; i < 100; ++i) {
        const Vec3 c{unit(rng), unit(rng), unit(rng)};
        const float r = 0.2f + std::abs(unit(rng));
        cases.push_back(sphereCase(c + randomUnit() * (0.9f * r * std::abs(unit(rng))), randomUnit(), c, r));
    }
    cases.push_back(sphereCase({0, 0, 5}, {0, 0, -1}, {0, 0, 0}, 1, 4.5f)); // tMin after the entry: the exit
    cases.push_back(sphereCase({0, 0, 5}, {0, 0, -1}, {0, 0, 0}, 1, 0.f, 3.5f)); // tMax before it: none
    // A 2 mm sphere 1 km away, centred, off-centre and just missed: the naive quadratic loses r^2 next to
    // |o - c|^2 in floats and misses it.
    for (const float off : {0.f, 0.5f, 0.9f, 1.2f}) {
        const Vec3 o{0, 0, 1000};
        const Vec3 c{0.002f * off, 0, 0};
        cases.push_back(sphereCase(o, Vec3{0, 0, -1}, c, 0.002f));
    }
    // Known normals for the uv map: +x (u 0, v 0.5), -z (u 0.25), +z (u 0.75), the top and the bottom.
    cases.push_back(sphereCase({5, 0, 0}, {-1, 0, 0}, {0, 0, 0}, 1));
    cases.push_back(sphereCase({0, 0, -5}, {0, 0, 1}, {0, 0, 0}, 1));
    cases.push_back(sphereCase({0, 0, 5}, {0, 0, -1}, {0, 0, 0}, 1));
    cases.push_back(sphereCase({0, 5, 0}, {0, -1, 0}, {0, 0, 0}, 1));
    cases.push_back(sphereCase({0, -5, 0}, {0, 1, 0}, {0, 0, 0}, 1));

    const auto count = static_cast<std::uint32_t>(cases.size());
    gfx::Buffer in(ctx,
                   {count * sizeof(SphereCase), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "sphere cases"});
    std::memcpy(in.mapped(), cases.data(), count * sizeof(SphereCase));
    in.flush();
    gfx::Buffer out(ctx, {count * 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "sphere results"});
    const std::array<gfx::Binding, 2> bindings = {gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                  gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
    {
        gfx::ComputePipeline probe(ctx, kSpirvDir / "test_sphere_probe.comp.spv", bindings, 4);
        gfx::FrameRing ring(ctx);
        VkCommandBuffer cmd = ring.begin();
        const std::array<VkDescriptorBufferInfo, 2> buffers = {
            VkDescriptorBufferInfo{in.handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{out.handle(), 0, VK_WHOLE_SIZE}};
        probe.bind(cmd, ring, {}, buffers);
        probe.push(cmd, &count, 4);
        probe.dispatch(cmd, count, 1, 64, 1);
        gfx::bufferBarrier2(cmd, out.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                            VK_ACCESS_2_HOST_READ_BIT);
        ring.wait(ring.submit());
    }
    idle(ctx);
    out.invalidate();
    std::vector<float> got(count * 8);
    std::memcpy(got.data(), out.mapped(), got.size() * sizeof(float));

    int hits = 0, insides = 0, misses = 0;
    double worstT = 0, worstUv = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const SphereCase& c = cases[i];
        const rt::Vec3d o{c.origin[0], c.origin[1], c.origin[2]},
            d{c.direction[0], c.direction[1], c.direction[2]}, center{c.center[0], c.center[1], c.center[2]};
        const auto want = rt::intersectSphere(o, d, center, c.radius, c.tMin, c.tMax);
        const float* g = &got[8 * i];
        // A ray within a relative 1e-4 of the silhouette may go either way in floats.
        const rt::Vec3d f{o[0] - center[0], o[1] - center[1], o[2] - center[2]};
        const double dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        const double along = (f[0] * d[0] + f[1] * d[1] + f[2] * d[2]) / dd;
        const rt::Vec3d l{f[0] - along * d[0], f[1] - along * d[1], f[2] - along * d[2]};
        const double closest = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
        if (std::abs(closest - c.radius) < 1e-4 * c.radius)
            continue;
        if (!want) {
            if (g[0] != 0.f)
                std::printf("case %u: GPU hit at t %g, CPU none\n", i, g[1]);
            CHECK(g[0] == 0.f);
            ++misses;
            continue;
        }
        if (g[0] != 1.f)
            std::printf("case %u: GPU miss, CPU t %g\n", i, want->t);
        CHECK(g[0] == 1.f);
        CHECK(g[2] == (want->inside ? float(rt::kSphereHitInside) : float(rt::kSphereHitOutside)));
        // A few ulps of t, or of the distance to the sphere, whichever is larger.
        const double scale =
            std::max(std::abs(want->t) * std::sqrt(dd), std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]));
        const double errT = std::abs(double(g[1]) - want->t) * std::sqrt(dd);
        if (errT > 4e-7 * scale + 1e-6)
            std::printf("case %u: t %.9g, want %.9g (scale %g)\n", i, g[1], want->t, scale);
        CHECK(errT <= 4e-7 * scale + 1e-6);
        worstT = std::max(worstT, errT / (scale + 1e-6));
        // The uv of the normal at the reference point.
        const rt::Vec3d p{o[0] + want->t * d[0], o[1] + want->t * d[1], o[2] + want->t * d[2]};
        rt::Vec3d n{(p[0] - center[0]) / c.radius, (p[1] - center[1]) / c.radius,
                    (p[2] - center[2]) / c.radius};
        const auto uv = rt::sphereUv(n);
        double du = std::abs(double(g[4]) - uv[0]);
        du = std::min(du, 1.0 - du); // u wraps
        const double dv = std::abs(double(g[5]) - uv[1]);
        if (c.radius >= 0.01f) { // a 2 mm sphere's normal from a kilometre away is only good to ~1e-2
            if (std::abs(n[1]) < 0.999)
                CHECK(du <= 2e-4);
            CHECK(dv <= 2e-4);
            worstUv = std::max({worstUv, std::abs(n[1]) < 0.999 ? du : 0.0, dv});
        }
        ++hits;
        insides += want->inside ? 1 : 0;
    }
    // The far tiny sphere: hit three times, missed once (the robust quadratic).
    const std::uint32_t far = count - 5 - 4;
    CHECK(got[8 * far] == 1.f && got[8 * (far + 1)] == 1.f && got[8 * (far + 2)] == 1.f &&
          got[8 * (far + 3)] == 0.f);
    CHECK(std::abs(got[8 * far + 1] - (1000.f - 0.002f)) < 2e-4f);
    // The uv map: +x (0, 0.5), -z (0.25, 0.5), +z (0.75, 0.5), top v 0, bottom v 1.
    const std::uint32_t known = count - 5;
    auto uvAt = [&](std::uint32_t i) { return std::array<float, 2>{got[8 * i + 4], got[8 * i + 5]}; };
    CHECK(std::abs(uvAt(known)[0]) < 1e-5f && std::abs(uvAt(known)[1] - 0.5f) < 1e-5f);
    CHECK(std::abs(uvAt(known + 1)[0] - 0.25f) < 1e-5f);
    CHECK(std::abs(uvAt(known + 2)[0] - 0.75f) < 1e-5f);
    CHECK(std::abs(uvAt(known + 3)[1]) < 1e-5f && std::abs(uvAt(known + 4)[1] - 1.f) < 1e-5f);
    // The CPU map is modern::uvSphere's: every vertex's uv from its normal.
    const rt::Mesh mesh = modern::uvSphere(1.f, 8, 16);
    for (const rt::Vertex& v : mesh.vertices) {
        const auto uv = rt::sphereUv({v.normal[0], v.normal[1], v.normal[2]});
        CHECK(std::abs(uv[1] - v.uv[1]) < 1e-5);
        if (std::abs(v.normal[1]) < 0.999f && v.uv[0] < 1.f)
            CHECK(std::abs(uv[0] - v.uv[0]) < 1e-5);
    }
    CHECK(hits > 300 && insides > 90 && misses > 50);
    std::printf("(m) sphere math: %d hits (%d from inside), %d misses against the CPU; worst t %.2g "
                "(relative), uv %.2g\n",
                hits, insides, misses, worstT, worstUv);
}

// --- Scenes ------------------------------------------------------------------------------------------------

rt::SphereRecord sphere(Vec3 center, float radius, std::uint32_t material, Vec3 color = {1, 1, 1},
                        float emission = 0.f) {
    rt::SphereRecord s{};
    s.center[0] = s.prevCenter[0] = center.x;
    s.center[1] = s.prevCenter[1] = center.y;
    s.center[2] = s.prevCenter[2] = center.z;
    s.radius = radius;
    s.color[0] = color.x;
    s.color[1] = color.y;
    s.color[2] = color.z;
    s.material = material;
    s.emission = emission;
    return s;
}

SceneInstance sphereInstance(std::uint32_t first, std::uint32_t count, std::uint32_t material,
                             const rt::Mat34& transform = modern::identity()) {
    SceneInstance in{0, material, transform};
    in.kind = rt::GeometryKind::Spheres;
    in.firstSphere = first;
    in.sphereCount = count;
    return in;
}

rt::Material checkerMaterial(Vec3 a, Vec3 b, float scale) {
    rt::Material m = test::lambert(a);
    m.baseColor2[0] = b.x;
    m.baseColor2[1] = b.y;
    m.baseColor2[2] = b.z;
    m.patternScale = scale;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Checker);
    return m;
}

// A floor and a back wall (checker), lit by a 1 x 1 light overhead, for the glass and look pictures.
void addRoom(TestScene& s, std::uint32_t checker, std::uint32_t light) {
    using test::kQuadMesh;
    auto& in = s.frame.instances;
    in.push_back({kQuadMesh, checker, test::place({0, 0, -3}, test::kX, -90, {12, 12, 1})});
    in.push_back({kQuadMesh, checker, test::place({0, 3, -7}, test::kY, 0, {12, 6, 1})});
    const rt::Mat34 lamp = test::place({0, 3, -2.5f}, test::kX, 90, {1.5f, 1.5f, 1});
    in.push_back({kQuadMesh, light, lamp, 0});
    const rt::Material& m = s.statics.materials[light];
    s.frame.lights.push_back(modern::rectLightForQuad(
        lamp, 1, 1, rt::LightKind::Emitter,
        {m.emission[0] * m.emissionScale, m.emission[1] * m.emissionScale, m.emission[2] * m.emissionScale},
        1));
}

// --- (a) ---------------------------------------------------------------------------------------------------

void testStaticDepth(gfx::Context& ctx) {
    constexpr std::uint32_t kW = 112, kH = 84;
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::lambert({0.6f, 0.6f, 0.6f}), test::metal({0.9f, 0.9f, 0.9f}, 0.2f)};
    s.statics.capacity.spheres = 4;
    s.statics.spheres = {sphere({-0.9f, 0.4f, -4}, 0.5f, 0), sphere({0.2f, -0.5f, -3.2f}, 0.3f, 0),
                         sphere({1.1f, 0.6f, -5}, 0.8f, 1), sphere({0.15f, 0.3f, -2.6f}, 0.12f, 1)};
    s.statics.sphereRanges = {{0, 2}, {2, 2}};
    const Vec3 shift{-0.2f, -1.1f, -1.5f}; // range 0 placed a second time
    s.frame.instances = {sphereInstance(0, 2, 0), sphereInstance(2, 2, 1),
                         sphereInstance(0, 2, 0, modern::translate(shift))};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 60.f};
    s.frame.environment.select = 1.f;

    std::vector<SphereAt> spheres;
    for (const rt::SphereRecord& r : s.statics.spheres)
        spheres.push_back({{r.center[0], r.center[1], r.center[2]}, r.radius});
    for (int i = 0; i < 2; ++i)
        spheres.push_back({spheres[i].center + shift, spheres[i].radius});

    Tracer tracer(ctx, s, kW, kH);
    const Frame f = tracer.trace(s.frame, {.spp = 1, .maxBounces = 0, .frameSeed = 3}); // the guides
    const modern::CameraBasis basis = modern::cameraBasis(s.frame.camera);
    int hitPixels = 0, missPixels = 0, skipped = 0;
    double worstDepth = 0, worstNormal = 0;
    std::vector<int> seen(spheres.size());
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const Vec3 d = pixelDirection(s.frame.camera, kW, kH, x, y);
            bool ambiguous = false;
            const NearestHit hit = nearest(spheres, s.frame.camera.eye, d, &ambiguous);
            if (ambiguous) {
                ++skipped;
                continue;
            }
            const float depth = f.depth.at(x, y);
            if (hit.sphere < 0) {
                CHECK(depth == -1.f);
                ++missPixels;
                continue;
            }
            const double want = hit.t * modern::dot(d, basis.forward);
            const double err = std::abs(depth - want);
            if (err > 1e-4)
                std::printf("pixel %u,%u: depth %.7f, want %.7f\n", x, y, depth, want);
            CHECK(err <= 1e-4);
            worstDepth = std::max(worstDepth, err);
            const SphereAt& sp = spheres[hit.sphere];
            const Vec3 p = s.frame.camera.eye + d * float(hit.t);
            const Vec3 n = (p - sp.center) / sp.radius; // radial; it faces the camera from outside
            for (std::uint32_t c = 0; c < 3; ++c) {
                const float nc = c == 0 ? n.x : c == 1 ? n.y : n.z;
                const double e = std::abs(f.normal.at(x, y, c) - nc);
                CHECK(e <= 1e-3);
                worstNormal = std::max(worstNormal, e);
            }
            ++seen[hit.sphere];
            ++hitPixels;
        }
    for (int n : seen)
        CHECK(n > 4); // every sphere, both placements of range 0
    CHECK(skipped < 40);
    std::printf("(a) static spheres: %d hit and %d missed pixels against the CPU (%d on a silhouette "
                "skipped); worst depth %.2g, normal %.2g\n",
                hitPixels, missPixels, skipped, worstDepth, worstNormal);
}

// --- (b) ---------------------------------------------------------------------------------------------------

void testGlassMatchesMesh(gfx::Context& ctx) {
    constexpr std::uint32_t kSize = 64, kSpp = 32;
    const Vec3 centre{0, 0.5f, -2.6f};
    constexpr float kRadius = 0.5f;
    auto base = [&] {
        TestScene s;
        s.statics.meshes = test::basicMeshes();
        s.statics.meshes.push_back(modern::uvSphere(kRadius, 64, 128)); // mesh 3
        s.statics.materials = {checkerMaterial({0.8f, 0.8f, 0.8f}, {0.1f, 0.15f, 0.4f}, 0.25f),
                               test::emitter({6, 6, 6}), test::glass(1.5f, 0.f)};
        s.frame.camera = {{0, 0.9f, 0}, centre, {0, 1, 0}, 30.f};
        addRoom(s, 0, 1);
        return s;
    };
    TestScene analytic = base();
    analytic.statics.capacity.spheres = 1;
    analytic.statics.spheres = {sphere(centre, kRadius, 2)};
    analytic.frame.instances.push_back(sphereInstance(0, 1, 2));
    TestScene mesh = base();
    mesh.frame.instances.push_back({3, 2, modern::translate(centre)});

    const TraceSettings settings{.spp = kSpp, .maxBounces = 4, .frameSeed = 11};
    Frame fa, fm;
    {
        Tracer t(ctx, analytic, kSize, kSize);
        fa = t.trace(analytic.frame, settings);
    }
    {
        Tracer t(ctx, mesh, kSize, kSize);
        fm = t.trace(mesh.frame, settings);
    }
    // The mean radiance over the sphere's disc (pixel centres the CPU sees hit it).
    double sumA = 0, sumM = 0;
    int pixels = 0;
    const std::vector<SphereAt> one{{centre, kRadius}};
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const Vec3 d = pixelDirection(analytic.frame.camera, kSize, kSize, x, y);
            if (nearest(one, analytic.frame.camera.eye, d, nullptr).sphere < 0)
                continue;
            for (std::uint32_t c = 0; c < 3; ++c) {
                sumA += fa.lighting.at(x, y, c) + fa.emission.at(x, y, c);
                sumM += fm.lighting.at(x, y, c) + fm.emission.at(x, y, c);
            }
            ++pixels;
        }
    const double ratio = sumA / sumM;
    std::printf("(b) glass: analytic sphere %.4f, uvSphere(64, 128) %.4f over %d pixels (ratio %.4f)\n",
                sumA / (3 * pixels), sumM / (3 * pixels), pixels, ratio);
    CHECK(pixels > 150);
    CHECK(sumM > 0 && std::abs(ratio - 1) <= 0.02);
}

// --- (c) ---------------------------------------------------------------------------------------------------

// The C++ mirror of test_particles_grid.comp's grid (mode 0), in floats like the shader.
struct GridParams {
    float spacing = 0.05f, radius = 0.02f, depth = 0.3f, w = 2.f, drift = 0.01f;
    std::array<float, 16> packed() const {
        std::array<float, 16> p{};
        p[0] = spacing;
        p[1] = radius;
        p[2] = depth;
        p[4] = w;
        p[5] = drift;
        return p;
    }
};

Vec3 gridParticle(std::uint32_t id, float seconds, const GridParams& g) {
    const float i = float(id % 64u), j = float(id / 64u);
    const float phase = 0.37f * float(id);
    return {(i - 31.5f) * g.spacing + g.drift * std::sin(g.w * seconds + phase), (j - 31.5f) * g.spacing,
            g.depth * std::sin(1.3f * g.w * seconds + 2.f * phase)};
}

TestScene particleScene(std::uint32_t count, std::uint32_t capacity) {
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::lambert({0.8f, 0.8f, 0.8f}), test::emitter({4, 4, 4})};
    s.statics.capacity.particles = capacity;
    s.statics.particleShader = kParticleShader;
    modern::ParticleFrame p;
    p.count = count;
    p.params = GridParams{}.packed();
    p.material = 0;
    s.frame.particles = p;
    SceneInstance in = sphereInstance(0, count, 0);
    in.dynamicSpheres = true;
    s.frame.instances = {in};
    s.frame.camera = {{0.1f, 0.05f, 2}, {0.1f, 0.05f, 0}, {0, 1, 0}, 30.f};
    s.frame.environment.select = 1.f;
    return s;
}

// With `refit` (the window's fast mode) the field updates its BLAS between rebuilds: the same hits.
void testParticleField(gfx::Context& ctx, bool refit) {
    constexpr std::uint32_t kW = 64, kH = 64, kCount = 4096, kFrames = 32;
    const GridParams grid;
    TestScene s = particleScene(kCount, kCount);
    const auto timeOf = [](std::uint32_t k) { return 0.25 + double(k) / 30.0; };

    // Probe pixels: five pixels near the picture's centre, at least 6 apart, whose centre ray passes well
    // inside a particle at the first frame (not on a silhouette).
    std::vector<SphereAt> at0;
    for (std::uint32_t id = 0; id < kCount; ++id)
        at0.push_back({gridParticle(id, float(timeOf(0)), grid), grid.radius});
    struct Probe {
        std::uint32_t x, y;
    };
    std::vector<Probe> candidates, probes;
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x)
            if (wellInside(at0, s.frame.camera.eye, pixelDirection(s.frame.camera, kW, kH, x, y), 0.5) >= 0)
                candidates.push_back({x, y});
    const auto fromCentre = [&](const Probe& p) {
        return std::abs(int(p.x) - int(kW / 2)) + std::abs(int(p.y) - int(kH / 2));
    };
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](const Probe& a, const Probe& b) { return fromCentre(a) < fromCentre(b); });
    for (const Probe& c : candidates) {
        const bool apart = std::all_of(probes.begin(), probes.end(), [&](const Probe& p) {
            return std::max(std::abs(int(c.x) - int(p.x)), std::abs(int(c.y) - int(p.y))) >= 6;
        });
        if (apart && probes.size() < 5)
            probes.push_back(c);
    }
    CHECK(probes.size() == 5);

    Tracer tracer(ctx, s, kW, kH, refit);
    struct Pending {
        PendingRead depth;
        gfx::Buffer records;
    };
    std::vector<Pending> pending;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t k = 0; k < kFrames; ++k) {
        // Two frames in flight: no wait here; each frame reads its own slot back in its own command buffer.
        VkCommandBuffer cmd = tracer.ring().begin();
        tracer.record(cmd, s.frame, {.spp = 1, .maxBounces = 0, .frameSeed = k}, timeOf(k),
                      timeOf(k == 0 ? 0 : k - 1));
        PendingRead depth = recordRead(ctx, cmd, tracer.pathTracer().gbuffer().depth);
        gfx::Buffer records =
            recordCopy(ctx, cmd, tracer.particles().records(), kCount * sizeof(rt::SphereRecord));
        CHECK(tracer.particles().count() == kCount);
        tracer.ring().submit();
        pending.push_back({std::move(depth), std::move(records)});
    }
    tracer.ring().waitAll();
    idle(ctx);
    const double seconds = secondsSince(start);

    const modern::CameraBasis basis = modern::cameraBasis(s.frame.camera);
    int probesHit = 0, probesMissed = 0, skipped = 0;
    double worstDepth = 0, worstCentre = 0;
    for (std::uint32_t k = 0; k < kFrames; ++k) {
        const float t = float(timeOf(k)), previous = float(timeOf(k == 0 ? 0 : k - 1));
        const Plane depth = finishRead(pending[k].depth);
        std::vector<SphereAt> spheres;
        for (std::uint32_t id = 0; id < kCount; ++id)
            spheres.push_back({gridParticle(id, t, grid), grid.radius});
        for (const Probe& p : probes) {
            bool ambiguous = false;
            const Vec3 d = pixelDirection(s.frame.camera, kW, kH, p.x, p.y);
            const NearestHit h = nearest(spheres, s.frame.camera.eye, d, &ambiguous);
            if (ambiguous) {
                ++skipped;
                continue;
            }
            const float got = depth.at(p.x, p.y);
            if (h.sphere < 0) {
                CHECK(got == -1.f);
                ++probesMissed;
                continue;
            }
            const double want = h.t * modern::dot(d, basis.forward);
            if (std::abs(got - want) > 1e-4)
                std::printf("frame %u probe %u,%u: depth %.7f, want %.7f\n", k, p.x, p.y, got, want);
            CHECK(std::abs(got - want) <= 1e-4);
            worstDepth = std::max(worstDepth, std::abs(got - want));
            ++probesHit;
        }
        // Every particle's centre at this frame's time and prevCenter at the previous frame's.
        pending[k].records.invalidate();
        std::vector<rt::SphereRecord> records(kCount);
        std::memcpy(records.data(), pending[k].records.mapped(), kCount * sizeof(rt::SphereRecord));
        for (std::uint32_t id = 0; id < kCount; ++id) {
            const Vec3 c = gridParticle(id, t, grid), pc = gridParticle(id, previous, grid);
            const rt::SphereRecord& r = records[id];
            const double e = std::max({std::abs(r.center[0] - c.x), std::abs(r.center[1] - c.y),
                                       std::abs(r.center[2] - c.z), std::abs(r.prevCenter[0] - pc.x),
                                       std::abs(r.prevCenter[1] - pc.y), std::abs(r.prevCenter[2] - pc.z)});
            CHECK(e <= 1e-5);
            worstCentre = std::max(worstCentre, e);
            CHECK(r.radius == grid.radius && r.material == 0u);
        }
    }
    CHECK(probesHit >= 100); // the probes see particles in most frames
    // Refit: each slot updates its BLAS kRefitFrames times between builds (16 frames per slot here).
    CHECK(tracer.particles().refits() == (refit ? 2u * (16u - (16u + rt::ParticleField::kRefitFrames) /
                                                                  (rt::ParticleField::kRefitFrames + 1))
                                                : 0u));
    std::printf(
        "(c) particle field%s: %u particles, %u frames two in flight in %.2f s; probes %d hit, %d missed "
        "(%d on a silhouette skipped), worst depth %.2g; centres and prevCenters within %.2g\n",
        refit ? " (refit)" : "", kCount, kFrames, seconds, probesHit, probesMissed, skipped, worstDepth,
        worstCentre);
}

// --- (d) ---------------------------------------------------------------------------------------------------

void testRefusals(gfx::Context& ctx) {
    std::printf("(d) refusals:\n");
    {
        TestScene s = particleScene(64, 64);
        Tracer tracer(ctx, s, 16, 16);
        const std::array<float, 16> params{};
        CHECK(throws<std::logic_error>([&] { tracer.particles().record(VK_NULL_HANDLE, 1, 0, 0, params); }));
        VkCommandBuffer cmd = tracer.ring().begin();
        CHECK(throws<std::length_error>([&] { tracer.particles().record(cmd, 65, 0, 0, params); }));
        // A frame that places its field without the field's spheres and BLAS.
        CHECK(throws<std::invalid_argument>(
            [&] { tracer.pathTracer().trace(cmd, tracer.scene(), s.frame, std::nullopt, {}, nullptr); }));
        tracer.ring().wait(tracer.ring().submit());
        idle(ctx);
    }
    {
        gfx::FrameRing ring(ctx);
        CHECK(throws<std::invalid_argument>([&] { rt::ParticleField(ctx, ring, 0, kParticleShader); }));
    }
    idle(ctx);

    // The frame checks (CPU).
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::lambert({0.5f, 0.5f, 0.5f}), test::glass(1.5f, 0.f),
                           test::metal({1, 1, 1}, 0)};
    s.statics.capacity.spheres = 3;
    s.statics.spheres = {sphere({0, 0, -3}, 0.5f, 0), sphere({1, 0, -3}, 0.5f, 2),
                         sphere({2, 0, -3}, 0.5f, 1)};
    s.statics.sphereRanges = {{0, 2}, {2, 1}};
    s.frame.instances = {sphereInstance(0, 2, 0), sphereInstance(2, 1, 1)};
    modern::checkSceneFrame(s.statics, s.frame); // valid: Pbr spheres (lambert, metal) and a glass one
    auto refused = [&](auto change, auto exception) {
        TestScene t = s;
        change(t);
        return throws<decltype(exception)>([&] { modern::checkSceneFrame(t.statics, t.frame); });
    };
    CHECK(refused([](TestScene& t) { t.frame.instances[0].sphereCount = 1; }, std::invalid_argument("")));
    CHECK(refused([](TestScene& t) { t.frame.instances[1].material = 0; }, std::invalid_argument("")));
    CHECK(refused([](TestScene& t) { t.statics.spheres[2].material = 7; }, std::out_of_range("")));
    CHECK(refused([](TestScene& t) { t.frame.instances[0].light = 0; }, std::invalid_argument("")));
    CHECK(
        refused([](TestScene& t) { t.frame.instances[0].dynamicSpheres = true; }, std::invalid_argument("")));
    CHECK(refused([](TestScene& t) { t.frame.instances[0].kind = rt::GeometryKind(7); },
                  std::invalid_argument("")));
    TestScene p = particleScene(100, 100);
    modern::checkSceneFrame(p.statics, p.frame);
    CHECK(refused(
        [&](TestScene& t) {
            t = p;
            t.frame.particles->count = 101;
            t.frame.instances[0].sphereCount = 101;
        },
        std::length_error("")));
    CHECK(refused(
        [&](TestScene& t) {
            t = p;
            t.frame.instances[0].sphereCount = 50;
        },
        std::invalid_argument("")));
    CHECK(refused(
        [&](TestScene& t) {
            t = p;
            t.frame.instances[0].material = 1;
        },
        std::invalid_argument("")));
    CHECK(refused(
        [&](TestScene& t) {
            t = p;
            t.frame.particles->material = 5;
        },
        std::out_of_range("")));
    CHECK(refused(
        [&](TestScene& t) {
            t = p;
            t.statics.particleShader.clear();
        },
        std::invalid_argument("")));
    CHECK(refused(
        [&](TestScene& t) {
            t = p;
            t.frame.particles.reset();
        },
        std::invalid_argument("")));

    // The static side: more spheres than the capacity, a range past the spheres.
    TestScene big = s;
    big.statics.capacity.spheres = 2;
    CHECK(throws<std::invalid_argument>([&] { modern::SceneGpu(ctx, big.statics); }));
    TestScene past = s;
    past.statics.sphereRanges = {{0, 2}, {2, 2}};
    CHECK(throws<std::invalid_argument>([&] { modern::SceneGpu(ctx, past.statics); }));
    idle(ctx);
}

// --- (e) ---------------------------------------------------------------------------------------------------

void testClasses(gfx::Context& ctx) {
    constexpr std::uint32_t kW = 96, kH = 64;
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    rt::Material emissive = test::emitter({0.5f, 1.f, 2.f}, 2.f); // emits (1, 2, 4)
    emissive.baseColor[0] = emissive.baseColor[1] = emissive.baseColor[2] = 0.f;
    const Vec3 checkerA{0.8f, 0.7f, 0.6f}, checkerB{0.2f, 0.3f, 0.4f};
    s.statics.materials = {emissive, checkerMaterial(checkerA, checkerB, 0.15f),
                           test::lambert({0.5f, 0.5f, 0.5f})};
    const Vec3 emissiveColor{1.f, 0.5f, 0.25f}, checkerColor{0.5f, 1.f, 1.f}, pbrColor{0.2f, 0.6f, 1.f};
    s.statics.capacity.spheres = 3;
    // Apart on the screen: a jittered sample of one sphere's pixel never reaches another.
    s.statics.spheres = {sphere({-1.f, 0, -3}, 0.4f, 0, emissiveColor, 3.f),
                         sphere({0.15f, 0, -2.6f}, 0.5f, 1, checkerColor),
                         sphere({1.3f, 0, -3}, 0.3f, 2, pbrColor)};
    s.statics.sphereRanges = {{0, 1}, {1, 1}, {2, 1}};
    s.frame.instances = {sphereInstance(0, 1, 0), sphereInstance(1, 1, 1), sphereInstance(2, 1, 2)};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 45.f};
    s.frame.environment.select = 1.f;
    Tracer tracer(ctx, s, kW, kH);
    const Frame f = tracer.trace(s.frame, {.spp = 1, .maxBounces = 0, .frameSeed = 5});

    std::vector<SphereAt> spheres;
    for (const rt::SphereRecord& r : s.statics.spheres)
        spheres.push_back({{r.center[0], r.center[1], r.center[2]}, r.radius});
    // Emission (primary hit): the material's (1, 2, 4) + colour * 3.
    const Vec3 wantEmission = Vec3{1, 2, 4} + emissiveColor * 3.f;
    int emissivePixels = 0, colourA = 0, colourB = 0, pbrPixels = 0;
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            // Pixels whose every jittered sample hits the sphere their centre ray hits.
            const int hit =
                wellInside(spheres, s.frame.camera.eye, pixelDirection(s.frame.camera, kW, kH, x, y), 0.8);
            if (hit < 0)
                continue;
            const Vec3 albedo{f.albedo.at(x, y, 0), f.albedo.at(x, y, 1), f.albedo.at(x, y, 2)};
            const Vec3 emission{f.emission.at(x, y, 0), f.emission.at(x, y, 1), f.emission.at(x, y, 2)};
            auto near = [](Vec3 a, Vec3 b, float tolerance) {
                return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance &&
                       std::abs(a.z - b.z) <= tolerance;
            };
            auto times = [](Vec3 a, Vec3 b) { return Vec3{a.x * b.x, a.y * b.y, a.z * b.z}; };
            if (hit == 0) {
                if (!near(emission, wantEmission, 0.01f))
                    std::printf("pixel %u,%u: emission %g %g %g, want %g %g %g (lighting %g, albedo %g %g "
                                "%g, depth %g)\n",
                                x, y, emission.x, emission.y, emission.z, wantEmission.x, wantEmission.y,
                                wantEmission.z, f.lighting.at(x, y, 0), albedo.x, albedo.y, albedo.z,
                                f.depth.at(x, y));
                CHECK(near(emission, wantEmission, 0.01f));
                ++emissivePixels;
            } else if (hit == 1) {
                CHECK(near(emission, {0, 0, 0}, 1e-6f));
                const bool a = near(albedo, times(checkerA, checkerColor), 2e-3f);
                const bool b = near(albedo, times(checkerB, checkerColor), 2e-3f);
                CHECK(a || b);
                colourA += a ? 1 : 0;
                colourB += b ? 1 : 0;
            } else {
                CHECK(near(albedo, times({0.5f, 0.5f, 0.5f}, pbrColor), 2e-3f));
                ++pbrPixels;
            }
        }
    CHECK(emissivePixels > 50 && colourA > 50 && colourB > 50 && pbrPixels > 30);
    std::printf("(e) classes: Emissive sphere emits material + colour * emission (%d pixels), Checker shows "
                "both colours x the sphere's (%d / %d pixels), Pbr tinted (%d pixels)\n",
                emissivePixels, colourA, colourB, pbrPixels);
}

// --- (f) ---------------------------------------------------------------------------------------------------

void testInsideGlass(gfx::Context& ctx) {
    constexpr std::uint32_t kSize = 32;
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {checkerMaterial({0.8f, 0.8f, 0.8f}, {0.1f, 0.1f, 0.1f}, 0.3f),
                           test::emitter({6, 6, 6}), test::glass(1.5f, 0.f)};
    addRoom(s, 0, 1);
    const Vec3 eye{0, 1, -1};
    constexpr float kRadius = 0.6f;
    s.statics.capacity.spheres = 1;
    s.statics.spheres = {sphere(eye + Vec3{0.1f, 0.05f, 0}, kRadius, 2)};
    s.frame.instances.push_back(sphereInstance(0, 1, 2));
    s.frame.camera = {eye, {0, 0.5f, -3}, {0, 1, 0}, 60.f};
    Tracer tracer(ctx, s, kSize, kSize);
    const Frame f = tracer.trace(s.frame, {.spp = 4, .maxBounces = 4, .frameSeed = 2});
    double sum = 0;
    bool finite = true;
    const std::vector<SphereAt> one{{eye + Vec3{0.1f, 0.05f, 0}, kRadius}};
    const modern::CameraBasis basis = modern::cameraBasis(s.frame.camera);
    double worstDepth = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            for (std::uint32_t c = 0; c < 3; ++c) {
                const float v = f.lighting.at(x, y, c) + f.emission.at(x, y, c);
                finite = finite && std::isfinite(v);
                sum += v;
            }
            // Every primary ray leaves the sphere: the depth guide is its inside, a back-facing hit.
            const Vec3 d = pixelDirection(s.frame.camera, kSize, kSize, x, y);
            const NearestHit h = nearest(one, eye, d, nullptr);
            CHECK(h.sphere == 0 && h.inside);
            const double want = h.t * modern::dot(d, basis.forward);
            CHECK(std::abs(f.depth.at(x, y) - want) <= 1e-4);
            worstDepth = std::max(worstDepth, std::abs(f.depth.at(x, y) - want));
        }
    const double mean = sum / (3.0 * kSize * kSize);
    CHECK(finite);
    CHECK(mean > 0.01);
    std::printf("(f) a camera inside a glass sphere: finite, mean radiance %.4f; inside depth within %.2g\n",
                mean, worstDepth);
}

// --- (p) ---------------------------------------------------------------------------------------------------

// A shader directory with the path tracer's misses, closest-hits and sphere shaders from the build and
// path_tracer_test's payload probe as its raygen.
std::filesystem::path payloadProbeShaders() {
    const std::filesystem::path dir = std::filesystem::path(EVO_TEST_OUT_DIR) / "spheres_payload_shaders";
    std::filesystem::create_directories(dir);
    const std::filesystem::path built(EVO_TEST_SHADER_DIR);
    for (const char* name :
         {"rt_sky.rmiss.spv", "rt_shadow.rmiss.spv", "rt_pane.rmiss.spv", "rt_shadow_through.rmiss.spv",
          "rt_pbr.rchit.spv", "rt_glass.rchit.spv", "rt_emissive.rchit.spv", "rt_checker.rchit.spv",
          "rt_pbr_sphere.rchit.spv", "rt_glass_sphere.rchit.spv", "rt_emissive_sphere.rchit.spv",
          "rt_checker_sphere.rchit.spv", "rt_sphere.rint.spv"})
        std::filesystem::copy_file(built / name, dir / name,
                                   std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(kSpirvDir / "test_payload_probe.rgen.spv", dir / "rt_pathtrace.rgen.spv",
                               std::filesystem::copy_options::overwrite_existing);
    return dir;
}

void testPayload(gfx::Context& ctx) {
    constexpr std::uint32_t kW = 72, kH = 48;
    // Static spheres 2..4 (range {2, 3}) on the left, each with its own prevCenter offset, and the particle
    // grid on the right (shifted by the instance transform).
    TestScene s = particleScene(4096, 4096);
    s.statics.capacity.spheres = 5;
    const Vec3 offsets[3] = {{0.1f, 0, 0}, {0, -0.05f, 0}, {0, 0, 0.2f}};
    s.statics.spheres = {sphere({9, 9, 9}, 0.1f, 0), sphere({9, 9, 8}, 0.1f, 0)};
    for (int i = 0; i < 3; ++i) {
        rt::SphereRecord r = sphere({-1.1f, 0.5f - 0.5f * float(i), -0.5f}, 0.2f, 0);
        r.prevCenter[0] += offsets[i].x;
        r.prevCenter[1] += offsets[i].y;
        r.prevCenter[2] += offsets[i].z;
        s.statics.spheres.push_back(r);
    }
    s.statics.sphereRanges = {{0, 2}, {2, 3}};
    s.frame.instances[0].transform = modern::translate({1.8f, 0, 0}); // the grid right of the centre
    s.frame.instances.push_back(sphereInstance(2, 3, 0));
    s.frame.camera = {{0, 0, 2.5f}, {0, 0, 0}, {0, 1, 0}, 60.f};
    const GridParams grid;
    const double t = 1.5, previous = 1.4;

    const std::filesystem::path dir = payloadProbeShaders();
    test::setEnv("EVO_SHADER_DIR", dir.string().c_str());
    std::optional<Tracer> tracer;
    try {
        tracer.emplace(ctx, s, kW, kH);
    } catch (...) {
        test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        throw;
    }
    test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
    const Frame f = tracer->trace(s.frame, {.spp = 1, .frameSeed = 1}, t, previous);
    int staticPixels = 0, particlePixels = 0;
    std::vector<int> staticSeen(5);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const float primitive = f.lighting.at(x, y, 3);
            if (f.lighting.at(x, y, 0) == -2.f)
                continue; // a miss
            const Vec3 moved{f.emission.at(x, y, 0), f.emission.at(x, y, 1), f.emission.at(x, y, 2)};
            const auto id = static_cast<std::uint32_t>(primitive);
            CHECK(float(id) == primitive);
            if (x < kW / 2 - 2) { // the static spheres
                CHECK(id >= 2 && id <= 4);
                const Vec3 want = offsets[id - 2];
                CHECK(std::abs(moved.x - want.x) < 1e-3f && std::abs(moved.y - want.y) < 1e-3f &&
                      std::abs(moved.z - want.z) < 1e-3f);
                ++staticSeen[id];
                ++staticPixels;
            } else if (x > kW / 2 + 2) {
                CHECK(id < 4096);
                const Vec3 want = gridParticle(id, float(previous), grid) - gridParticle(id, float(t), grid);
                CHECK(std::abs(moved.x - want.x) < 2e-3f && std::abs(moved.y - want.y) < 2e-3f &&
                      std::abs(moved.z - want.z) < 2e-3f);
                ++particlePixels;
            }
        }
    CHECK(staticSeen[2] > 3 && staticSeen[3] > 3 && staticSeen[4] > 3 && particlePixels > 100);
    tracer.reset();
    idle(ctx);
    std::printf("(p) payload: primitive = the sphere's index, prevPosition - position = prevCenter - center "
                "(%d static, %d particle pixels)\n",
                staticPixels, particlePixels);
}

// --- (s) ---------------------------------------------------------------------------------------------------

constexpr std::uint32_t kStageW = 64, kStageH = 36;

// A particle grid scene for the modern stage: its frame follows the chapter-local time (the camera does not).
class ParticleModernScene final : public modern::ModernScene {
public:
    ParticleModernScene() : scene_(particleScene(4096, 4096)) {
        scene_.statics.materials.push_back(test::emitter({3, 3, 3}));
        addRoom(scene_, 0, 2);
        scene_.frame.camera = {{0.1f, 0.05f, 2.5f}, {0.1f, 0.05f, 0}, {0, 1, 0}, 40.f};
    }
    const modern::SceneStatic& staticScene() const override { return scene_.statics; }
    modern::SceneFrame frame(const show::TimelineFrame&) const override { return scene_.frame; }
    modern::Camera camera(const show::TimelineFrame&) const override { return scene_.frame.camera; }
    bool samplesRetro() const override { return false; }

private:
    TestScene scene_;
};

show::TimelineFrame forced2026(double seconds) {
    show::TimelineFrame frame = show::sampleTimeline(seconds);
    frame.era = 1.f;
    frame.wipe = 1.f;
    frame.eraCut = 0.f;
    return frame;
}

class StageHarness {
public:
    StageHarness(gfx::Context& ctx, render::RenderMode mode)
        : readback_(ctx, kStageW, kStageH), ring_(ctx), renderer_(ctx, ring_, settings(mode)) {}
    std::vector<std::uint8_t> render(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return readback_.rgb();
    }
    render::Renderer& renderer() { return renderer_; }

private:
    static render::RenderSettings settings(render::RenderMode mode) {
        render::RenderSettings s{kStageW, kStageH};
        s.hud = false;
        s.mode = mode;
        s.spp = 1;
        s.sceneFactory = [](modern::SceneId id,
                            modern::SceneDetail detail) -> std::unique_ptr<modern::ModernScene> {
            if (id == modern::SceneId::Ship)
                return std::make_unique<ParticleModernScene>();
            return modern::makeScene(id, detail);
        };
        s.sceneMap = [](show::Effect effect) {
            return effect == show::Effect::Flyby ? modern::SceneId::Ship : modern::sceneFor(effect);
        };
        return s;
    }
    gfx::Readback readback_;
    gfx::FrameRing ring_;
    render::Renderer renderer_;
};

VkDeviceSize allocatedBytes(gfx::Context& ctx) {
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(ctx.allocator(), &stats);
    return stats.total.statistics.allocationBytes;
}

void testStage(gfx::Context& ctx) {
    const double start = show::chapterStart(1), end = show::chapterStart(2);
    const show::TimelineFrame a = forced2026(start + 0.3 * (end - start));
    const show::TimelineFrame b = forced2026(start + 0.31 * (end - start));
    {
        StageHarness h(ctx, render::RenderMode::Export);
        render::ModernStage& stage = h.renderer().modern();
        CHECK(stage.particles() == nullptr);
        const VkDeviceSize stageImages = stage.hdr().bytes() + stage.image().bytes();
        const VkDeviceSize before = allocatedBytes(ctx);
        stage.prepare(modern::SceneId::Ship);
        CHECK(stage.particles() != nullptr && stage.particles()->capacity() == 4096);
        CHECK(allocatedBytes(ctx) - before == stage.gpuBytes() - stageImages);
        CHECK(stage.gpuBytes() == stage.imageBytes() + stage.tracer()->bufferBytes() +
                                      stage.sceneGpu(modern::SceneId::Ship)->bytes() +
                                      stage.particles()->bytes());
    }
    idle(ctx);
    // Export: b after a equals a fresh b, and a differs (the particles moved). The window: a, b, then back
    // to a (a seek, which resets the history) equals a fresh renderer's first frame at a.
    std::vector<std::uint8_t> exportB, exportA, windowSeek;
    {
        StageHarness h(ctx, render::RenderMode::Export);
        exportA = h.render(a);
        exportB = h.render(b);
    }
    idle(ctx);
    {
        StageHarness h(ctx, render::RenderMode::Export);
        CHECK(h.render(b) == exportB);
    }
    idle(ctx);
    CHECK(exportA != exportB);
    {
        StageHarness h(ctx, render::RenderMode::Interactive);
        h.render(a);
        h.render(b);
        windowSeek = h.render(a);
    }
    idle(ctx);
    {
        StageHarness h(ctx, render::RenderMode::Interactive);
        CHECK(h.render(a) == windowSeek);
    }
    idle(ctx);
    std::printf(
        "(s) modern stage: one particle field measured by the allocator; an export frame after another "
        "time, and a window frame after a seek, equal a fresh renderer's\n");
}

// --- Look --------------------------------------------------------------------------------------------------

std::uint8_t encode(float linear) {
    const float v = post::srgbOetf(linear / (1.f + linear)); // Reinhard, for looking only
    return static_cast<std::uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
}

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp) {
    const auto start = std::chrono::steady_clock::now();
    constexpr std::uint32_t kW = 640, kH = 360, kParticles = 24000;
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    rt::Material gold = test::metal({1.f, 0.78f, 0.34f}, 0.08f);
    rt::Material chrome = test::metal({0.95f, 0.95f, 0.95f}, 0.f);
    rt::Material particleChrome = test::metal({1, 1, 1}, 0.05f);
    particleChrome.emissionScale = 1.f;
    s.statics.materials = {checkerMaterial({0.85f, 0.85f, 0.85f}, {0.05f, 0.05f, 0.06f}, 0.5f),
                           test::emitter({7, 6.5f, 6}),
                           chrome,
                           gold,
                           test::glass(1.5f, 0.f),
                           test::emitter({0, 0, 0}),
                           particleChrome};
    addRoom(s, 0, 1);
    s.frame.instances[1].transform = test::place({0, 4, -9}, test::kY, 0, {16, 8, 1});
    // Static spheres: a row of chrome and gold (Pbr), glass (Glass) and emissive (Emissive), one range each.
    s.statics.capacity.spheres = 64;
    std::uint32_t first = 0;
    auto addRange = [&](std::uint32_t material, const std::vector<rt::SphereRecord>& spheres) {
        for (const rt::SphereRecord& r : spheres)
            s.statics.spheres.push_back(r);
        s.statics.sphereRanges.push_back({first, std::uint32_t(spheres.size())});
        s.frame.instances.push_back(sphereInstance(first, std::uint32_t(spheres.size()), material));
        first += std::uint32_t(spheres.size());
    };
    addRange(2, {sphere({-2.2f, 0.45f, -4.2f}, 0.45f, 2), sphere({1.1f, 0.35f, -3.4f}, 0.35f, 3),
                 sphere({2.4f, 0.55f, -4.8f}, 0.55f, 2), sphere({-0.9f, 0.25f, -2.6f}, 0.25f, 3)});
    addRange(4, {sphere({-0.2f, 0.5f, -3.6f}, 0.5f, 4),
                 sphere({1.6f, 0.22f, -2.4f}, 0.22f, 4, {0.6f, 0.85f, 1.f})});
    addRange(5, {sphere({0.6f, 0.15f, -2.2f}, 0.15f, 5, {1.f, 0.4f, 0.1f}, 6.f),
                 sphere({-1.7f, 0.12f, -2.1f}, 0.12f, 5, {0.2f, 0.6f, 1.f}, 6.f)});
    // The particle vortex above them.
    s.statics.capacity.particles = kParticles;
    s.statics.particleShader = kParticleShader;
    modern::ParticleFrame p;
    p.count = kParticles;
    p.params[3 * 4 + 3] = 1.f; // mode 1: the vortex
    p.params[0 * 4 + 3] = float(kParticles);
    p.params[2 * 4 + 0] = 2.4f;   // disc radius
    p.params[2 * 4 + 1] = 1.3f;   // height
    p.params[2 * 4 + 2] = 0.018f; // particle radius
    p.params[2 * 4 + 3] = 12.f;   // emission of the glowing ones
    p.material = 6;
    s.frame.particles = p;
    SceneInstance field = sphereInstance(0, kParticles, 6, modern::translate({0, 0, -4.2f}));
    field.dynamicSpheres = true;
    s.frame.instances.push_back(field);
    s.frame.camera = {{0, 2.6f, 1.4f}, {0, 0.7f, -3.8f}, {0, 1, 0}, 50.f};
    s.frame.environment = modern::environmentDefaults(modern::EnvironmentKind::Dusk);
    s.frame.environment.select = 0.3f;

    Tracer tracer(ctx, s, kW, kH);
    const Frame f = tracer.trace(s.frame, {.spp = spp, .maxBounces = 6, .frameSeed = 1}, 2.0, 2.0);
    std::vector<std::uint8_t> rgb(std::size_t(kW) * kH * 3);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x)
            for (std::uint32_t c = 0; c < 3; ++c)
                rgb[(std::size_t(y) * kW + x) * 3 + c] =
                    encode(f.lighting.at(x, y, c) + f.emission.at(x, y, c));
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / ("spheres_look_" + std::to_string(spp) + "spp.png");
    image::writePng(path, int(kW), int(kH), rgb.data());
    std::printf("look: %s in %.1f s\n", path.string().c_str(), secondsSince(start));
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
    try {
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the results are checked\n");
            else if (!ctx.syncValidationEnabled()) {
                std::printf("SKIP sync validation unavailable\n");
                return 0;
            }
            if (argc >= 3 && std::string(argv[1]) == "--look") {
                renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 64u);
                return 0;
            }
            auto timed = [&](const char* name, auto&& run) {
                const auto t0 = std::chrono::steady_clock::now();
                run();
                std::printf("  [%s %.2f s]\n", name, secondsSince(t0));
            };
            timed("m", [&] { testSphereMath(ctx); });
            timed("a", [&] { testStaticDepth(ctx); });
            timed("e", [&] { testClasses(ctx); });
            timed("f", [&] { testInsideGlass(ctx); });
            timed("d", [&] { testRefusals(ctx); });
            timed("c", [&] { testParticleField(ctx, false); });
            timed("c", [&] { testParticleField(ctx, true); });
            timed("p", [&] { testPayload(ctx); });
            timed("b", [&] { testGlassMatchesMesh(ctx); });
            timed("s", [&] { testStage(ctx); });
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("spheres_test: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
