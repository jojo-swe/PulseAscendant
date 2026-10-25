// Per-object motion vectors and the export shutter (step 5, Task 9) on lavapipe, with the validation layer
// and its synchronization validation on; fails on any message.
//
// (a) a quad moving 2 px per frame under a static camera: the motion guide is (-2, 0) on it and (0, 0) on the
//     static wall behind it, and with the camera moving too it equals the analytic reprojection;
// (b) previous transforms that do not describe this frame's instances (another count, another mesh) fall back
//     to the camera's motion alone;
// (c) a moving sphere (a static record with a prevCenter shift, and a static range whose instance moved) and
//     the particle field (prevCenter): the motion guide equals the analytic reprojection of the sphere's hit
//     point;
// (d) the denoiser follows a moving object: a slanted quad moving 2 px per frame keeps its history (length
//     8 after 8 frames), and has none (length 1: the depth moves under the pixel) without the previous
//     transforms, which is step 4's behaviour;
// (e) the shutter in the tracer: a bright quad moving 8 px per 1/60 s, exported at spp 8 and 32 with a
// shutter
//     of 1/120 s, has an edge that is a ramp of 4 +- 1 px (without a shutter at most 1 px), its streak holds
//     the energy of the still quad, the particle callback runs once per slice in order, and the slice
//     refusals;
// (g) the modern stage: an export with a shutter asks the scene for its slices' times only (inside the
// chapter,
//     also at the chapter's first and last frames), its streak holds the energy of the still picture, a frame
//     is a pure function of the show time (frame T equals itself after T - 1/60; two runs agree byte for
//     byte), and the window, which has no shutter, ignores the setting (same bytes, one scene frame per
//     frame) and keeps its seeks (a, b, a equals a fresh a with the moving scene's previous transforms);
// `motion_test --look <dir> [spp]` renders a picture to look at instead: a panning camera over a checker
// floor with a fast glowing sphere, a rolling chrome one and a still gold one, with and without a shutter.
//
// (f) the shutter on the CPU: the slice count and offsets, and slice times that stay inside their chapter at
//     both edges of every chapter of the show (the last frame of FLYBY never evaluates DOTS);

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/shutter.hpp"
#include "post/denoiser.hpp"
#include "post/tonemap_reference.hpp"
#include "render/renderer.hpp"
#include "rt/particles.hpp"
#include "rt/spheres.hpp"
#include "show/timeline.hpp"

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
#include <memory>
#include <optional>
#include <span>
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

const std::filesystem::path kSpirvDir = EVO_TEST_SPIRV_DIR;
const std::filesystem::path kParticleShader = kSpirvDir / "test_particles_grid.comp.spv";

// Waits for the queue: synchronization validation in layer 1.3.275 credits no host wait on the timeline, so
// memory reused between subtests would read as a hazard (Decision 13 of step 4).
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
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "motion test readback"}),
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
    gfx::Buffer copy(ctx, {bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "motion test records"});
    const VkBufferCopy region{0, 0, bytes};
    vkCmdCopyBuffer(cmd, source.handle(), copy.handle(), 1, &region);
    gfx::bufferBarrier2(cmd, copy.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    return copy;
}

struct Frame {
    Plane lighting, emission, depth, motion;
    std::vector<rt::SphereRecord> particles; // the particle field's records of the frame, if it had one
};

// A path tracer for one scene with its own ring and, when the scene has a particle shader, its own particle
// field (declared in the order they must outlive each other: the tracer's TLAS goes first and waits for the
// ring, then the field, which waits too, then the ring and the scene's GPU side).
class Tracer {
public:
    Tracer(gfx::Context& ctx, const TestScene& scene, std::uint32_t width, std::uint32_t height)
        : ctx_(ctx), gpu_(ctx, scene.statics), ring_(ctx),
          particles_(scene.statics.particleShader.empty()
                         ? nullptr
                         : std::make_unique<rt::ParticleField>(ctx, ring_,
                                                               std::max(scene.statics.capacity.particles, 1u),
                                                               scene.statics.particleShader)),
          tracer_(ctx, ring_, width, height, modern::tracerCapacity(scene.statics.capacity)) {}

    // Traces `frame` with the previous frame's camera and instances (`previousInstances`, empty: none) and,
    // for a particle field, its records at `seconds` with prevCenter at `previousSeconds`.
    Frame trace(const modern::SceneFrame& frame, const std::optional<Camera>& previousCamera,
                std::span<const SceneInstance> previousInstances, const TraceSettings& settings,
                double seconds = 0, double previousSeconds = 0) {
        VkCommandBuffer cmd = ring_.begin();
        modern::TraceExtras extras;
        extras.previousInstances = previousInstances;
        if (frame.particles) {
            particles_->record(cmd, frame.particles->count, seconds, previousSeconds, frame.particles->params,
                               frame.particles->material);
            extras.particleSpheres = &particles_->records();
            extras.particleBlas = particles_->blasAddress();
        }
        tracer_.trace(cmd, gpu_, frame, previousCamera, settings, &unused_, extras);
        modern::GBuffer& g = tracer_.gbuffer();
        std::vector<PendingRead> reads;
        for (gfx::Image* image : {&g.lighting, &g.emission, &g.depth, &g.motion})
            reads.push_back(recordRead(ctx_, cmd, *image));
        std::optional<gfx::Buffer> records;
        if (frame.particles)
            records.emplace(recordCopy(ctx_, cmd, particles_->records(),
                                       frame.particles->count * sizeof(rt::SphereRecord)));
        ring_.wait(ring_.submit());
        idle(ctx_);
        Frame f{finishRead(reads[0]), finishRead(reads[1]), finishRead(reads[2]), finishRead(reads[3]), {}};
        if (records) {
            records->invalidate();
            f.particles.resize(frame.particles->count);
            std::memcpy(f.particles.data(), records->mapped(), f.particles.size() * sizeof(rt::SphereRecord));
        }
        return f;
    }

    // Traces the time slices of one export frame and reads the guides back; `before` is the particle
    // callback.
    Frame traceSlices(std::span<const modern::SceneFrame> slices, const TraceSettings& settings,
                      const std::function<void(VkCommandBuffer, std::uint32_t)>& before = {}) {
        VkCommandBuffer cmd = ring_.begin();
        tracer_.trace(cmd, gpu_, slices, std::nullopt, settings, &unused_, {}, before);
        modern::GBuffer& g = tracer_.gbuffer();
        std::vector<PendingRead> reads;
        for (gfx::Image* image : {&g.lighting, &g.emission, &g.depth, &g.motion})
            reads.push_back(recordRead(ctx_, cmd, *image));
        ring_.wait(ring_.submit());
        idle(ctx_);
        return {finishRead(reads[0]), finishRead(reads[1]), finishRead(reads[2]), finishRead(reads[3]), {}};
    }

    modern::PathTracer& pathTracer() { return tracer_; }

private:
    gfx::Context& ctx_;
    gfx::Image unused_{ctx_, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "unused retro"}};
    modern::SceneGpu gpu_;
    gfx::FrameRing ring_;
    std::unique_ptr<rt::ParticleField> particles_;
    modern::PathTracer tracer_;
};

// --- Geometry on the CPU -----------------------------------------------------------------------------------

struct Vec3d {
    double x, y, z;
};
Vec3d d3(Vec3 v) {
    return {v.x, v.y, v.z};
}
Vec3d operator-(Vec3d a, Vec3d b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3d operator+(Vec3d a, Vec3d b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3d operator*(Vec3d a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
double dot(Vec3d a, Vec3d b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3d normalized(Vec3d v) {
    return v * (1.0 / std::sqrt(dot(v, v)));
}

// The centre of pixel (x, y) in camera space: the view ray's direction, as pathtrace.rgen's cameraDirection.
Vec3d pixelRay(const Camera& camera, std::uint32_t width, std::uint32_t height, std::uint32_t x,
               std::uint32_t y) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const double tanHalf = std::tan(camera.fovYDegrees * 3.14159265358979323846 / 360.0);
    const double aspect = double(width) / double(height);
    const double sx = (2.0 * (x + 0.5) / width - 1.0) * tanHalf * aspect;
    const double sy = (1.0 - 2.0 * (y + 0.5) / height) * tanHalf;
    return normalized(d3(b.forward) + d3(b.right) * sx + d3(b.up) * sy);
}

// Where a world point is in a camera's picture, in pixels (the previous-frame reprojection of the guide).
std::array<double, 2> projectPixel(const Camera& camera, Vec3d p, std::uint32_t width, std::uint32_t height) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const double tanHalf = std::tan(camera.fovYDegrees * 3.14159265358979323846 / 360.0);
    const double aspect = double(width) / double(height);
    const Vec3d v = p - d3(camera.eye);
    const double z = dot(v, d3(b.forward));
    const double ndcX = dot(v, d3(b.right)) / (z * tanHalf * aspect);
    const double ndcY = dot(v, d3(b.up)) / (z * tanHalf);
    return {(ndcX + 1.0) * 0.5 * width, (1.0 - ndcY) * 0.5 * height};
}

// The motion the guide should hold for a hit point `p` that was at `before` in the previous frame, seen by
// pixel (x, y): where it was in the previous camera's picture minus this pixel's centre.
std::array<double, 2> expectedMotion(const Camera& previousCamera, Vec3d before, std::uint32_t width,
                                     std::uint32_t height, std::uint32_t x, std::uint32_t y) {
    const std::array<double, 2> prev = projectPixel(previousCamera, before, width, height);
    return {prev[0] - (x + 0.5), prev[1] - (y + 0.5)};
}

// The size of a pixel at `distance` along the view direction, in world units.
double pixelSize(const Camera& camera, std::uint32_t height, double distance) {
    return 2.0 * distance * std::tan(camera.fovYDegrees * 3.14159265358979323846 / 360.0) / height;
}

rt::SphereRecord sphere(Vec3 center, float radius, std::uint32_t material, Vec3 prevOffset = {0, 0, 0}) {
    rt::SphereRecord s{};
    s.center[0] = center.x;
    s.center[1] = center.y;
    s.center[2] = center.z;
    s.prevCenter[0] = center.x + prevOffset.x;
    s.prevCenter[1] = center.y + prevOffset.y;
    s.prevCenter[2] = center.z + prevOffset.z;
    s.radius = radius;
    s.color[0] = s.color[1] = s.color[2] = 1.f;
    s.material = material;
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

// The first hit of the ray (origin, direction) with a sphere (double precision); `inside` is how far inside
// the silhouette the ray passes, as a fraction of the radius (0: through the centre, 1: grazing).
struct SphereHit {
    double t = 0;
    double radial = 1.0;
};
std::optional<SphereHit> hitSphere(Vec3d origin, Vec3d direction, Vec3d center, double radius) {
    const auto hit =
        rt::intersectSphere({origin.x, origin.y, origin.z}, {direction.x, direction.y, direction.z},
                            {center.x, center.y, center.z}, radius, 0.0, 1e30);
    if (!hit)
        return std::nullopt;
    const Vec3d f = origin - center;
    const double along = dot(f, direction);
    const double dist2 = dot(f, f) - along * along;
    return SphereHit{hit->t, std::sqrt(std::max(dist2, 0.0)) / radius};
}

// --- (a) and (b): a moving quad ----------------------------------------------------------------------------

constexpr std::uint32_t kW = 64, kH = 36;
constexpr float kQuadZ = -3.f, kWallZ = -6.f;

// A wall far behind (instance 0, static) and a quad 2 wide in front of it (instance 1), lit by nothing: only
// the guides matter.
TestScene quadScene(float quadX) {
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::lambert({0.7f, 0.7f, 0.7f})};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 40.f};
    s.frame.instances = {{test::kQuadMesh, 0, test::place({0, 0, kWallZ}, test::kY, 0, {24, 12, 1})},
                         {test::kQuadMesh, 0, test::place({quadX, 0, kQuadZ}, test::kY, 0, {2, 2, 1})}};
    return s;
}

// What the quad scene's motion guide should hold at (x, y), or nullopt where the pixel is too close to the
// quad's edge to say (a ray within `margin` pixels of it).
struct QuadCase {
    float currentX, previousX;
    Camera previousCamera;
};

struct MotionStats {
    int quadPixels = 0, wallPixels = 0;
    double worstQuad = 0, worstWall = 0;
};

MotionStats checkQuadMotion(const Frame& f, const Camera& camera, const QuadCase& c,
                            bool expectObjectMotion) {
    MotionStats stats;
    const double margin = 3.0 * pixelSize(camera, kH, -kQuadZ);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const Vec3d d = pixelRay(camera, kW, kH, x, y);
            const double tQuad = kQuadZ / d.z; // the plane z = kQuadZ
            const Vec3d onQuad = d * tQuad;
            const bool nearEdge = std::abs(std::abs(onQuad.x - c.currentX) - 1.0) < margin ||
                                  std::abs(std::abs(onQuad.y) - 1.0) < margin;
            const bool onTheQuad = std::abs(onQuad.x - c.currentX) < 1.0 && std::abs(onQuad.y) < 1.0;
            // The quad's previous place also uncovers wall: pixels there are skipped as well.
            const bool nearPrevious = std::abs(onQuad.x - c.previousX) < 1.0 + margin && !onTheQuad;
            if (nearEdge)
                continue;
            Vec3d hit;
            Vec3d before;
            bool quad;
            if (onTheQuad) {
                hit = onQuad;
                before = onQuad + Vec3d{c.previousX - c.currentX, 0, 0};
                quad = true;
            } else {
                if (nearPrevious)
                    continue;
                hit = d * (kWallZ / d.z);
                before = hit;
                quad = false;
            }
            if (!expectObjectMotion)
                before = hit;
            const auto want = expectedMotion(c.previousCamera, before, kW, kH, x, y);
            const float mx = f.motion.at(x, y, 0), my = f.motion.at(x, y, 1), mz = f.motion.at(x, y, 2);
            CHECK(mz == 1.f);
            const double err = std::max(std::abs(mx - want[0]), std::abs(my - want[1]));
            if (err > 0.02)
                std::printf("pixel %u,%u (%s): motion (%g, %g), want (%g, %g)\n", x, y,
                            quad ? "quad" : "wall", mx, my, want[0], want[1]);
            CHECK(err <= 0.02);
            if (quad) {
                ++stats.quadPixels;
                stats.worstQuad = std::max(stats.worstQuad, err);
            } else {
                ++stats.wallPixels;
                stats.worstWall = std::max(stats.worstWall, err);
            }
        }
    return stats;
}

void testQuadMotion(gfx::Context& ctx) {
    const Camera camera = quadScene(0).frame.camera;
    const float step = float(2.0 * pixelSize(camera, kH, -kQuadZ)); // 2 px at the quad's depth
    const TestScene previous = quadScene(0);
    const TestScene current = quadScene(step);
    Tracer tracer(ctx, current, kW, kH);
    const TraceSettings settings{.spp = 1, .maxBounces = 0, .frameSeed = 3};

    // Static camera: (-2, 0) on the quad (it was 2 px left), (0, 0) on the wall.
    {
        const Frame f = tracer.trace(current.frame, camera, previous.frame.instances, settings);
        const MotionStats stats = checkQuadMotion(f, camera, {step, 0.f, camera}, true);
        CHECK(stats.quadPixels > 300 && stats.wallPixels > 300);
        // The headline numbers, straight from the guide.
        CHECK(std::abs(f.motion.at(kW / 2, kH / 2, 0) + 2.f) < 0.01f &&
              std::abs(f.motion.at(kW / 2, kH / 2, 1)) < 0.01f);
        CHECK(std::abs(f.motion.at(2, 2, 0)) < 0.01f && std::abs(f.motion.at(2, 2, 1)) < 0.01f);
        std::printf(
            "(a) quad moving 2 px: motion (-2, 0) on %d quad pixels (worst error %.3g px), (0, 0) on %d "
            "wall pixels (worst %.3g)\n",
            stats.quadPixels, stats.worstQuad, stats.wallPixels, stats.worstWall);
    }
    // The camera moved as well: the analytic reprojection through the previous camera.
    {
        Camera before = camera;
        before.eye = {0.25f, -0.1f, 0.2f};
        before.target = {0.2f, -0.1f, -1.f};
        const Frame f = tracer.trace(current.frame, before, previous.frame.instances, settings);
        const MotionStats stats = checkQuadMotion(f, camera, {step, 0.f, before}, true);
        CHECK(stats.quadPixels > 300 && stats.wallPixels > 300);
        std::printf("(a) quad and camera moving: %d quad and %d wall pixels equal the analytic reprojection "
                    "(worst %.3g, %.3g px)\n",
                    stats.quadPixels, stats.wallPixels, stats.worstQuad, stats.worstWall);
    }
    // No previous transforms handed over: the camera's motion alone.
    {
        const Frame f = tracer.trace(current.frame, camera, {}, settings);
        const MotionStats stats = checkQuadMotion(f, camera, {step, 0.f, camera}, false);
        CHECK(stats.quadPixels > 300);
    }
    // (b) Previous transforms of another instance count, or of other meshes, are no previous transforms.
    {
        std::vector<SceneInstance> fewer(previous.frame.instances.begin(),
                                         previous.frame.instances.begin() + 1);
        const Frame f = tracer.trace(current.frame, camera, fewer, settings);
        const MotionStats stats = checkQuadMotion(f, camera, {step, 0.f, camera}, false);
        CHECK(stats.quadPixels > 300);
        std::vector<SceneInstance> other = previous.frame.instances;
        other[1].mesh = test::kCubeMesh; // same count, another mesh
        const Frame g = tracer.trace(current.frame, camera, other, settings);
        CHECK(checkQuadMotion(g, camera, {step, 0.f, camera}, false).quadPixels > 300);
        std::vector<SceneInstance> spheres = previous.frame.instances;
        spheres[1].kind = rt::GeometryKind::Spheres; // same count and mesh, another kind
        const Frame h = tracer.trace(current.frame, camera, spheres, settings);
        CHECK(checkQuadMotion(h, camera, {step, 0.f, camera}, false).quadPixels > 300);
        std::printf("(b) previous transforms of another count, mesh or kind: camera motion only\n");
    }
}

// --- (c): spheres and particles ----------------------------------------------------------------------------

struct SphereAt {
    Vec3d center, previous; // this frame's centre, and where the same sphere was in the previous frame
    double radius;
};

// Checks every pixel whose centre ray passes well inside a sphere (and hits no other first) against the
// reprojection of the hit point carried back by the sphere's own motion.
struct SphereStats {
    int pixels = 0;
    double worst = 0;
};
SphereStats checkSphereMotion(const Frame& f, const Camera& camera, const Camera& previousCamera,
                              const std::vector<SphereAt>& spheres, std::uint32_t width, std::uint32_t height,
                              double wellInside = 0.6) {
    SphereStats stats;
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const Vec3d d = pixelRay(camera, width, height, x, y);
            int best = -1;
            double bestT = 0, bestRadial = 1;
            for (std::size_t i = 0; i < spheres.size(); ++i) {
                const auto hit = hitSphere(d3(camera.eye), d, spheres[i].center, spheres[i].radius);
                if (hit && (best < 0 || hit->t < bestT)) {
                    best = int(i);
                    bestT = hit->t;
                    bestRadial = hit->radial;
                }
            }
            if (best < 0 || bestRadial > wellInside)
                continue;
            const Vec3d p = d3(camera.eye) + d * bestT;
            const SphereAt& s = spheres[best];
            const Vec3d before = p + (s.previous - s.center);
            const auto want = expectedMotion(previousCamera, before, width, height, x, y);
            const double err =
                std::max(std::abs(f.motion.at(x, y, 0) - want[0]), std::abs(f.motion.at(x, y, 1) - want[1]));
            if (err > 0.03)
                std::printf("pixel %u,%u: motion (%g, %g), want (%g, %g)\n", x, y, f.motion.at(x, y, 0),
                            f.motion.at(x, y, 1), want[0], want[1]);
            CHECK(f.motion.at(x, y, 2) == 1.f);
            CHECK(err <= 0.03);
            ++stats.pixels;
            stats.worst = std::max(stats.worst, err);
        }
    return stats;
}

void testSphereMotion(gfx::Context& ctx) {
    constexpr std::uint32_t w = 96, h = 54;
    // Sphere A: a static record whose prevCenter says it moved. Sphere B: a static record in a range whose
    // instance moved (the previous transform). Sphere C stands still.
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::lambert({0.7f, 0.7f, 0.7f})};
    s.statics.capacity.spheres = 3;
    const Vec3 shiftA{0.3f, -0.1f, 0.2f};
    s.statics.spheres = {sphere({-1.3f, 0.2f, -4.f}, 0.6f, 0, shiftA), sphere({0.f, 0.f, 0.f}, 0.6f, 0),
                         sphere({0.2f, -0.9f, -3.f}, 0.4f, 0)};
    s.statics.sphereRanges = {{0, 1}, {1, 1}, {2, 1}};
    const rt::Mat34 bNow = modern::translate({1.2f, 0.3f, -4.5f}),
                    bBefore = modern::translate({0.9f, 0.2f, -4.7f});
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 50.f};
    s.frame.instances = {sphereInstance(0, 1, 0), sphereInstance(1, 1, 0, bNow), sphereInstance(2, 1, 0)};
    std::vector<SceneInstance> before = s.frame.instances;
    before[1].transform = bBefore;
    Camera previousCamera = s.frame.camera;
    previousCamera.eye = {-0.1f, 0.05f, 0.1f};

    const auto centerOf = [&](const rt::SphereRecord& r, const rt::Mat34& m) {
        return Vec3d{m.m[0][0] * r.center[0] + m.m[0][1] * r.center[1] + m.m[0][2] * r.center[2] + m.m[0][3],
                     m.m[1][0] * r.center[0] + m.m[1][1] * r.center[1] + m.m[1][2] * r.center[2] + m.m[1][3],
                     m.m[2][0] * r.center[0] + m.m[2][1] * r.center[1] + m.m[2][2] * r.center[2] + m.m[2][3]};
    };
    const rt::SphereRecord& a = s.statics.spheres[0];
    const rt::SphereRecord& b = s.statics.spheres[1];
    const rt::SphereRecord& c = s.statics.spheres[2];
    const std::vector<SphereAt> spheres = {
        {centerOf(a, modern::identity()), centerOf(a, modern::identity()) + d3(shiftA), a.radius},
        {centerOf(b, bNow), centerOf(b, bBefore), b.radius},
        {centerOf(c, modern::identity()), centerOf(c, modern::identity()), c.radius}};

    Tracer tracer(ctx, s, w, h);
    const Frame f =
        tracer.trace(s.frame, previousCamera, before, {.spp = 1, .maxBounces = 0, .frameSeed = 5});
    const SphereStats stats = checkSphereMotion(f, s.frame.camera, previousCamera, spheres, w, h);
    CHECK(stats.pixels > 150);
    std::printf("(c) moving spheres (prevCenter shift, moved instance, still): %d pixels equal the analytic "
                "reprojection (worst %.3g px)\n",
                stats.pixels, stats.worst);
}

// The C++ mirror of test_particles_grid.comp's grid (mode 0), in floats like the shader.
struct GridParams {
    float spacing = 0.25f, radius = 0.1f, depth = 0.3f, w = 4.f, drift = 0.08f;
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

void testParticleMotion(gfx::Context& ctx) {
    constexpr std::uint32_t w = 128, h = 72, kCount = 4096;
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::lambert({0.8f, 0.8f, 0.8f})};
    s.statics.capacity.particles = kCount;
    s.statics.particleShader = kParticleShader;
    modern::ParticleFrame p;
    p.count = kCount;
    p.params = GridParams{}.packed();
    s.frame.particles = p;
    SceneInstance field = sphereInstance(0, kCount, 0, modern::translate({0.f, 0.f, -3.f}));
    field.dynamicSpheres = true;
    s.frame.instances = {field};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 40.f};
    std::vector<SceneInstance> previousInstances = s.frame.instances;
    previousInstances[0].transform = modern::translate({0.1f, 0.f, -3.f}); // the field itself drifted too
    Camera previousCamera = s.frame.camera;
    previousCamera.eye = {0.05f, 0.f, 0.f};

    Tracer tracer(ctx, s, w, h);
    const double t = 1.5, previous = 1.4;
    const Frame f = tracer.trace(s.frame, previousCamera, previousInstances,
                                 {.spp = 1, .maxBounces = 0, .frameSeed = 9}, t, previous);
    // The spheres' records as the field wrote them, placed by the instance's transform now and before.
    std::vector<SphereAt> spheres;
    for (const rt::SphereRecord& r : f.particles) {
        const Vec3d now = Vec3d{r.center[0], r.center[1], r.center[2]} + Vec3d{0, 0, -3};
        const Vec3d then = Vec3d{r.prevCenter[0], r.prevCenter[1], r.prevCenter[2]} + Vec3d{0.1, 0, -3};
        spheres.push_back({now, then, r.radius});
    }
    // The records are what the shader's mirror says: prevCenter really is the centre at the previous time.
    CHECK(f.particles.size() == kCount);
    const SphereStats stats = checkSphereMotion(f, s.frame.camera, previousCamera, spheres, w, h);
    CHECK(stats.pixels > 100);
    std::printf("(c) particle field: %d pixels equal the analytic reprojection through prevCenter and the "
                "instance's previous transform (worst %.3g px)\n",
                stats.pixels, stats.worst);
}

// --- (d): the denoiser follows a moving object -------------------------------------------------------------

// The tracer's G-buffer through the denoiser, frame by frame, as the modern stage does.
class Chain {
public:
    Chain(gfx::Context& ctx, const TestScene& scene, std::uint32_t width, std::uint32_t height)
        : ctx_(ctx), gpu_(ctx, scene.statics), ring_(ctx), denoiser_(ctx, ring_, width, height),
          tracer_(ctx, ring_, width, height, modern::tracerCapacity(scene.statics.capacity)) {}

    // Traces and denoises one frame; returns the denoiser's history length image (R) after it.
    Plane frame(const modern::SceneFrame& frame, const std::optional<Camera>& previousCamera,
                std::span<const SceneInstance> previousInstances) {
        VkCommandBuffer cmd = ring_.begin();
        modern::TraceExtras extras;
        extras.previousInstances = previousInstances;
        tracer_.trace(cmd, gpu_, frame, previousCamera, {.spp = 1, .maxBounces = 0, .frameSeed = 1}, &unused_,
                      extras);
        modern::GBuffer& g = tracer_.gbuffer();
        denoiser_.denoise(cmd, {g.lighting, g.emission, g.albedo, g.normal, g.depth, g.motion, g.volume},
                          post::DenoiseSettings{}, hdr_);
        PendingRead read = recordRead(ctx_, cmd, denoiser_.historyLength());
        ring_.wait(ring_.submit());
        idle(ctx_);
        return finishRead(read);
    }

private:
    gfx::Context& ctx_;
    gfx::Image unused_{ctx_, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "unused retro"}};
    gfx::Image hdr_{ctx_,
                    {kW, kH, VK_FORMAT_R16G16B16A16_SFLOAT,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 1, "chain hdr"}};
    modern::SceneGpu gpu_;
    gfx::FrameRing ring_;
    post::Denoiser denoiser_;
    modern::PathTracer tracer_;
};

// A quad slanted 60 degrees about y, 4 across, at z = -3 and `x`: under a static camera its depth at a pixel
// changes by far more than the denoiser's 5 % when it slides 2 px sideways.
rt::Mat34 slantedQuad(float x) {
    return test::place({x, 0, kQuadZ}, test::kY, 60, {4, 3, 1});
}

void testDenoiserFollows(gfx::Context& ctx) {
    constexpr int kFrames = 8;
    TestScene s = quadScene(0);
    const Camera camera = s.frame.camera;
    // One pixel is 0.0607 world units at the quad's depth; the slanted quad moves 2 px along x per frame.
    const float step = float(2.0 * pixelSize(camera, kH, -kQuadZ));
    std::vector<modern::SceneFrame> frames;
    for (int f = 0; f < kFrames; ++f) {
        modern::SceneFrame frame = s.frame;
        frame.instances[1].transform = slantedQuad(float(f - kFrames / 2) * step);
        frames.push_back(frame);
    }
    // The pixels of the last frame that show a point of the quad well inside it that was visible, on the
    // quad, in every earlier frame as well (the history chain stays on the object and the picture).
    const rt::Mat34 last = frames.back().instances[1].transform;
    auto axis = [&](const rt::Mat34& m, int column) {
        return Vec3d{m.m[0][column], m.m[1][column], m.m[2][column]};
    };
    auto centre = [&](const rt::Mat34& m) { return Vec3d{m.m[0][3], m.m[1][3], m.m[2][3]}; };
    std::vector<std::array<std::uint32_t, 2>> pixels;
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const Vec3d d = pixelRay(camera, kW, kH, x, y);
            const Vec3d c = centre(last), n = normalized(Vec3d{last.m[0][2], last.m[1][2], last.m[2][2]});
            const double t = dot(c, n) / dot(d, n);
            if (!(t > 0))
                continue;
            const Vec3d hit = d * t;
            const Vec3d u = axis(last, 0), v = axis(last, 1);
            const double lu = dot(hit - c, u) / dot(u, u),
                         lv = dot(hit - c, v) / dot(v, v); // the quad: [-0.5, 0.5]
            bool inside = std::abs(lu) < 0.5 - 0.1 && std::abs(lv) < 0.5 - 0.1;
            // The same point in every earlier frame: on screen with 3 px to spare.
            for (int f = 0; f < kFrames - 1 && inside; ++f) {
                const rt::Mat34& m = frames[f].instances[1].transform;
                const Vec3d p = centre(m) + axis(m, 0) * lu + axis(m, 1) * lv;
                const auto px = projectPixel(camera, p, kW, kH);
                inside = px[0] > 3 && px[0] < kW - 3 && px[1] > 3 && px[1] < kH - 3;
            }
            if (inside)
                pixels.push_back({x, y});
        }
    CHECK(pixels.size() > 150);

    auto run = [&](bool withPrevious) {
        Chain chain(ctx, s, kW, kH);
        Plane length;
        for (int f = 0; f < kFrames; ++f) {
            std::span<const SceneInstance> previous;
            if (f > 0 && withPrevious)
                previous = frames[f - 1].instances;
            length = chain.frame(frames[f], f > 0 ? std::optional<Camera>(camera) : std::nullopt, previous);
        }
        return length;
    };
    auto stats = [&](const Plane& length) {
        double sum = 0;
        float least = 1e9f;
        for (const auto& p : pixels) {
            const float l = length.at(p[0], p[1], 0);
            sum += l;
            least = std::min(least, l);
        }
        return std::pair<double, float>{sum / pixels.size(), least};
    };
    const auto followed = stats(run(true));
    const auto lost = stats(run(false));
    std::printf("(d) denoiser history length on the moving quad after %d frames: %.2f mean (least %g) with "
                "previous transforms, %.2f mean (least %g) without\n",
                kFrames, followed.first, followed.second, lost.first, lost.second);
    CHECK(followed.second >= 6.f);
    CHECK(lost.first < 2.0);
}

// --- (e): the shutter in the tracer ------------------------------------------------------------------------

// A bright emitter quad, 4 wide, at the height of the camera: only its emission (the primary hit) matters.
TestScene brightQuadScene() {
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::emitter({5, 5, 5}), test::lambert({0, 0, 0})};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 40.f};
    s.frame.instances = {{test::kQuadMesh, 1, test::place({0, 0, kWallZ}, test::kY, 0, {24, 12, 1})},
                         {test::kQuadMesh, 0, test::place({0, 0, kQuadZ}, test::kY, 0, {1, 1, 1})}};
    return s;
}

// The quad moving `pixelsPerFrame` px per 1/60 s, seen by a shutter of `shutter` seconds in `slices` slices
// around the frame's time (the quad is at x0 then).
std::vector<modern::SceneFrame> shutterFrames(float x0, double pixelsPerFrame, double shutter,
                                              std::uint32_t slices) {
    const TestScene s = brightQuadScene();
    const double perSecond = pixelsPerFrame * pixelSize(s.frame.camera, kH, -kQuadZ) * 60.0;
    std::vector<modern::SceneFrame> frames;
    for (std::uint32_t k = 0; k < slices; ++k) {
        modern::SceneFrame frame = s.frame;
        const double x = x0 + perSecond * modern::sliceOffset(k, slices, shutter);
        frame.instances[1].transform = test::place({float(x), 0, kQuadZ}, test::kY, 0, {1, 1, 1});
        frames.push_back(frame);
    }
    return frames;
}

// The number of pixels of the middle row of the emission image whose value is neither the dark side nor the
// quad's plateau (within 2 % of the plateau) around the quad's right edge, and the image's total energy.
struct Edge {
    int ramp = 0;
    double energy = 0;
};
Edge measureEdge(const Frame& f) {
    Edge edge;
    const std::uint32_t y = kH / 2;
    float plateau = 0;
    for (std::uint32_t x = 0; x < kW; ++x)
        plateau = std::max(plateau, f.emission.at(x, y, 0));
    for (std::uint32_t x = kW / 2; x < kW; ++x) { // the right edge
        const float v = f.emission.at(x, y, 0);
        if (v > 0.02f * plateau && v < 0.98f * plateau)
            ++edge.ramp;
    }
    for (std::uint32_t yy = 0; yy < kH; ++yy)
        for (std::uint32_t x = 0; x < kW; ++x)
            edge.energy += f.emission.at(x, yy, 0);
    return edge;
}

void testShutter(gfx::Context& ctx) {
    constexpr double kShutter = 1.0 / 120.0; // a 180 degree shutter at 60 fps
    const TestScene s = brightQuadScene();
    Tracer tracer(ctx, s, kW, kH);
    auto render = [&](std::uint32_t spp, double shutter, double pixelsPerFrame, float x0) {
        const std::uint32_t slices = modern::shutterSlices(spp, shutter);
        const std::vector<modern::SceneFrame> frames = shutterFrames(x0, pixelsPerFrame, shutter, slices);
        return tracer.traceSlices(frames, {.spp = spp, .maxBounces = 0, .frameSeed = 21});
    };
    // The quad's edge sits between pixels or on a pixel's middle; try a few offsets and keep the worst case.
    auto ramp = [&](std::uint32_t spp, double shutter, double speed) {
        int widest = 0, narrowest = 1000;
        for (const float x0 : {0.f, 0.013f, 0.031f}) {
            const Edge e = measureEdge(render(spp, shutter, speed, x0));
            widest = std::max(widest, e.ramp);
            narrowest = std::min(narrowest, e.ramp);
        }
        return std::pair<int, int>{narrowest, widest};
    };
    const auto sharp = ramp(8, 0.0, 8);
    const auto two = ramp(8, kShutter, 8);
    const auto eight = ramp(32, kShutter, 8);
    std::printf("(e) edge ramp (px): no shutter %d-%d, spp 8 / 2 slices %d-%d, spp 32 / 8 slices %d-%d\n",
                sharp.first, sharp.second, two.first, two.second, eight.first, eight.second);
    CHECK(sharp.second <= 1);
    CHECK(two.first >= 3 && two.second <= 5);
    CHECK(eight.first >= 3 && eight.second <= 5);

    // Energy: the streak holds the still quad's energy (the quad stays on screen), at spp 8 and 32.
    const double still = measureEdge(render(8, 0.0, 0, 0.f)).energy;
    const double blur2 = measureEdge(render(8, kShutter, 8, 0.f)).energy;
    const double blur8 = measureEdge(render(32, kShutter, 8, 0.f)).energy;
    const double fast = measureEdge(render(32, 2.0 * kShutter, 16, 0.f)).energy;
    std::printf("(e) streak energy against the still quad: %.4f (2 slices), %.4f (8 slices), %.4f (8 slices, "
                "twice the speed and shutter)\n",
                blur2 / still, blur8 / still, fast / still);
    CHECK(std::abs(blur2 / still - 1.0) < 0.01);
    CHECK(std::abs(blur8 / still - 1.0) < 0.01);
    CHECK(std::abs(fast / still - 1.0) < 0.01);

    // The camera moves instead of the quad: each slice has its own uniforms, so the edge is the same ramp.
    {
        const double perSecond = 8 * pixelSize(s.frame.camera, kH, -kQuadZ) * 60.0;
        std::vector<modern::SceneFrame> frames;
        for (std::uint32_t k = 0; k < 8; ++k) {
            modern::SceneFrame frame = s.frame;
            const float dx =
                float(-perSecond * modern::sliceOffset(k, 8, kShutter)); // the quad moves with +dx
            frame.camera.eye.x -= dx;
            frame.camera.target.x -= dx;
            frames.push_back(frame);
        }
        const Edge e = measureEdge(tracer.traceSlices(frames, {.spp = 32, .maxBounces = 0, .frameSeed = 21}));
        std::printf("(e) the camera moving instead: an edge ramp of %d px\n", e.ramp);
        CHECK(e.ramp >= 3 && e.ramp <= 5);
    }
    // Each slice has its own material table: two slices of emission 4 and 6 average to 5.
    {
        std::vector<modern::SceneFrame> frames = shutterFrames(0.f, 0, kShutter, 2);
        for (std::uint32_t k = 0; k < 2; ++k) {
            frames[k].materials = s.statics.materials;
            const float emission = k == 0 ? 4.f : 6.f;
            frames[k].materials[0].emission[0] = frames[k].materials[0].emission[1] =
                frames[k].materials[0].emission[2] = emission;
        }
        const Frame f = tracer.traceSlices(frames, {.spp = 8, .maxBounces = 0, .frameSeed = 21});
        const float centre = f.emission.at(kW / 2, kH / 2, 0);
        std::printf("(e) per-slice material tables (4 and 6): the quad shows %.3f\n", centre);
        CHECK(std::abs(centre - 5.f) < 0.02f);
    }
    // The guides come from the middle slice (slices / 2).
    {
        const std::vector<modern::SceneFrame> frames = shutterFrames(0.f, 8, kShutter, 2);
        const Frame f = tracer.traceSlices(frames, {.spp = 8, .maxBounces = 0, .frameSeed = 21});
        const std::vector<modern::SceneFrame> single{frames[1]};
        const Frame g = tracer.traceSlices(single, {.spp = 4, .maxBounces = 0, .frameSeed = 21});
        CHECK(f.depth.values == g.depth.values);
    }

    // The particle callback: once per slice, in order, in the frame being recorded.
    {
        const std::vector<modern::SceneFrame> frames = shutterFrames(0.f, 8, kShutter, 4);
        std::vector<std::uint32_t> calls;
        tracer.traceSlices(frames, {.spp = 16, .maxBounces = 0, .frameSeed = 21},
                           [&](VkCommandBuffer cmd, std::uint32_t slice) {
                               CHECK(cmd != VK_NULL_HANDLE);
                               calls.push_back(slice);
                           });
        CHECK((calls == std::vector<std::uint32_t>{0, 1, 2, 3}));
        calls.clear();
        tracer.traceSlices(std::span<const modern::SceneFrame>(frames.data(), 1), {.spp = 4},
                           [&](VkCommandBuffer, std::uint32_t slice) { calls.push_back(slice); });
        CHECK((calls == std::vector<std::uint32_t>{0}));
    }

    // Refusals, before anything is recorded: no slices, more than kMaxShutterSlices, more slices than
    // dispatches (spp 4 is one dispatch).
    {
        const std::vector<modern::SceneFrame> nine = shutterFrames(0.f, 8, kShutter, 9);
        auto refuses = [&](std::span<const modern::SceneFrame> slices, std::uint32_t spp) {
            VkCommandBuffer cmd = nullptr;
            gfx::FrameRing ring(ctx);
            modern::SceneGpu gpu(ctx, s.statics);
            gfx::Image unused(ctx, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "unused"});
            modern::PathTracer pathTracer(ctx, ring, kW, kH, modern::tracerCapacity(s.statics.capacity));
            cmd = ring.begin();
            bool threw = false;
            try {
                pathTracer.trace(cmd, gpu, slices, std::nullopt, {.spp = spp}, &unused);
            } catch (const std::invalid_argument& error) {
                std::printf("  refused: %s\n", error.what());
                threw = true;
            }
            ring.wait(ring.submit());
            idle(ctx);
            return threw;
        };
        CHECK(refuses({}, 8));
        CHECK(refuses(nine, 64));
        CHECK(refuses(std::span<const modern::SceneFrame>(nine.data(), 2), 4));
    }
}

// --- (g): the modern stage ---------------------------------------------------------------------------------

constexpr std::uint32_t kStageW = 64, kStageH = 36;

// A bright quad sliding sideways with the chapter-local time (`pixelsPerFrame` px per 1/60 s from x0 at
// localSeconds `anchor`), in front of a dark wall. It records every time frame() was asked for.
class SlidingScene final : public modern::ModernScene {
public:
    SlidingScene(double pixelsPerFrame, double anchor) : scene_(brightQuadScene()), anchor_(anchor) {
        perSecond_ = pixelsPerFrame * pixelSize(scene_.frame.camera, kH, -kQuadZ) * 60.0;
    }
    const modern::SceneStatic& staticScene() const override { return scene_.statics; }
    modern::SceneFrame frame(const show::TimelineFrame& timeline) const override {
        asked_.push_back({timeline.chapter, timeline.localSeconds, timeline.chapterDuration});
        modern::SceneFrame frame = scene_.frame;
        frame.instances[1].transform = test::place(
            {float(perSecond_ * (timeline.localSeconds - anchor_)), 0, kQuadZ}, test::kY, 0, {1, 1, 1});
        return frame;
    }
    modern::Camera camera(const show::TimelineFrame&) const override { return scene_.frame.camera; }
    bool samplesRetro() const override { return false; }

    struct Asked {
        int chapter;
        double localSeconds, chapterDuration;
    };
    const std::vector<Asked>& asked() const { return asked_; }
    void forget() { asked_.clear(); }

private:
    TestScene scene_;
    double anchor_, perSecond_ = 0;
    mutable std::vector<Asked> asked_;
};

show::TimelineFrame forced2026(double seconds) {
    show::TimelineFrame frame = show::sampleTimeline(seconds);
    frame.era = 1.f;
    frame.wipe = 1.f;
    frame.eraCut = 0.f;
    return frame;
}

// A renderer showing a SlidingScene in the FLYBY chapters, which keeps the scene to read back what it was
// asked.
class StageHarness {
public:
    StageHarness(gfx::Context& ctx, render::RenderMode mode, std::uint32_t spp, double shutterSeconds,
                 double pixelsPerFrame, double anchor)
        : ctx_(ctx), readback_(ctx, kStageW, kStageH), ring_(ctx),
          renderer_(ctx, ring_, settings(mode, spp, shutterSeconds, pixelsPerFrame, anchor)) {}

    std::vector<std::uint8_t> render(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return readback_.rgb();
    }
    // The sum of the HDR picture's red channel (the stage's light before the tonemap).
    double renderEnergy(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        PendingRead read = recordRead(ctx_, cmd, renderer_.modern().hdr());
        ring_.wait(ring_.submit());
        idle(ctx_);
        const Plane hdr = finishRead(read);
        double sum = 0;
        for (std::uint32_t y = 0; y < hdr.height; ++y)
            for (std::uint32_t x = 0; x < hdr.width; ++x)
                sum += hdr.at(x, y, 0);
        return sum;
    }
    // Renders a frame and reads the tracer's motion guide of it (the modern stage's last trace).
    Plane renderMotion(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        PendingRead read = recordRead(ctx_, cmd, renderer_.modern().tracer()->gbuffer().motion);
        ring_.wait(ring_.submit());
        idle(ctx_);
        return finishRead(read);
    }
    SlidingScene& scene() { return *scene_; }
    render::Renderer& renderer() { return renderer_; }

private:
    render::RenderSettings settings(render::RenderMode mode, std::uint32_t spp, double shutterSeconds,
                                    double pixelsPerFrame, double anchor) {
        render::RenderSettings s{kStageW, kStageH};
        s.hud = false;
        s.mode = mode;
        s.spp = spp;
        s.shutterSeconds = shutterSeconds;
        s.sceneFactory = [this, pixelsPerFrame,
                          anchor](modern::SceneId id,
                                  modern::SceneDetail detail) -> std::unique_ptr<modern::ModernScene> {
            if (id == modern::SceneId::Ship) {
                auto scene = std::make_unique<SlidingScene>(pixelsPerFrame, anchor);
                scene_ = scene.get();
                return scene;
            }
            return modern::makeScene(id, detail);
        };
        s.sceneMap = [](show::Effect effect) {
            return effect == show::Effect::Flyby ? modern::SceneId::Ship : modern::sceneFor(effect);
        };
        return s;
    }
    gfx::Context& ctx_;
    gfx::Readback readback_;
    gfx::FrameRing ring_;
    SlidingScene* scene_ = nullptr;
    render::Renderer renderer_;
};

int flybyChapter() {
    for (int chapter = 0; chapter < show::chapterCount(); ++chapter)
        if (show::sampleTimeline(show::chapterStart(chapter) + 0.01).effect == show::Effect::Flyby)
            return chapter;
    return -1;
}

void testStage(gfx::Context& ctx) {
    constexpr double kShutter = 1.0 / 120.0;
    const int chapter = flybyChapter();
    CHECK(chapter >= 0);
    const double start = show::chapterStart(chapter), end = show::chapterStart(chapter + 1);
    const double middle = start + 0.5 * (end - start);
    const show::TimelineFrame frame = forced2026(middle);

    // The scene is asked for the slices' times: spp 32 is 8 slices of the 180 degree shutter around the
    // frame.
    {
        StageHarness h(ctx, render::RenderMode::Export, 32, kShutter, 8, frame.localSeconds);
        h.render(frame);
        const auto& asked = h.scene().asked();
        // camera() is not frame(): one frame() call per slice (and maybe one for the stage's own use).
        std::vector<double> locals;
        for (const auto& a : asked)
            locals.push_back(a.localSeconds);
        std::vector<double> want;
        for (std::uint32_t k = 0; k < 8; ++k)
            want.push_back(frame.localSeconds + modern::sliceOffset(k, 8, kShutter));
        CHECK(locals.size() == 8);
        for (std::size_t k = 0; k < 8; ++k)
            CHECK(std::abs(locals[k] - want[k]) < 1e-9 && asked[k].chapter == chapter);
        std::printf("(g) the scene is asked for 8 slices around the frame, %.5f to %.5f s\n", locals.front(),
                    locals.back());
    }
    // Chapter edges: a shutter of a whole frame at the chapter's first and last frames asks for nothing
    // outside.
    {
        StageHarness h(ctx, render::RenderMode::Export, 32, 1.0 / 60.0, 8, 0.0);
        h.render(forced2026(middle)); // makes the scene
        for (const double time : {start, end - 1.0 / 60.0, end - 1.0 / 240.0}) {
            h.scene().forget();
            h.render(forced2026(time));
            CHECK(h.scene().asked().size() == 8);
            for (const auto& a : h.scene().asked()) {
                CHECK(a.chapter == chapter);
                CHECK(a.localSeconds >= 0.0 && a.localSeconds <= a.chapterDuration - 1e-6 + 1e-12);
            }
        }
        std::printf("(g) the first and last frames of the chapter never ask for another chapter\n");
    }
    // Purity and seeking in an export: frame T alone, after T - 1/60, and in a second run are the same bytes.
    {
        const show::TimelineFrame before = forced2026(middle - 1.0 / 60.0);
        std::vector<std::uint8_t> alone, after, again;
        {
            StageHarness h(ctx, render::RenderMode::Export, 8, kShutter, 8, frame.localSeconds);
            alone = h.render(frame);
        }
        idle(ctx);
        {
            StageHarness h(ctx, render::RenderMode::Export, 8, kShutter, 8, frame.localSeconds);
            h.render(before);
            after = h.render(frame);
        }
        idle(ctx);
        {
            StageHarness h(ctx, render::RenderMode::Export, 8, kShutter, 8, frame.localSeconds);
            again = h.render(frame);
        }
        idle(ctx);
        CHECK(alone == after);
        CHECK(alone == again);
        std::printf("(g) export frame T equals itself after T - 1/60 and in a second run\n");
    }
    // The streak holds the still picture's energy (the denoiser and tonemap included).
    {
        StageHarness still(ctx, render::RenderMode::Export, 32, kShutter, 0, frame.localSeconds);
        StageHarness moving(ctx, render::RenderMode::Export, 32, kShutter, 8, frame.localSeconds);
        const double a = still.renderEnergy(frame), b = moving.renderEnergy(frame);
        std::printf("(g) stage streak energy / still energy %.4f (%.1f / %.1f)\n", b / a, b, a);
        CHECK(a > 10.0 && std::abs(b / a - 1.0) < 0.02);
        // Without the shutter the same moving scene is sharp: the pictures differ.
        StageHarness sharp(ctx, render::RenderMode::Export, 32, 0.0, 8, frame.localSeconds);
        CHECK(sharp.render(frame) != moving.render(frame));
    }
    idle(ctx);
    // The window has no shutter: the setting changes nothing, and the scene is asked once per frame; a seek
    // (a, b, a) equals a fresh renderer's a with the moving scene's previous transforms in play.
    {
        const show::TimelineFrame a = frame, b = forced2026(middle + 1.0 / 60.0);
        std::vector<std::uint8_t> withShutter, without, seek, fresh;
        {
            StageHarness h(ctx, render::RenderMode::Interactive, 8, kShutter, 8, a.localSeconds);
            h.render(a);
            CHECK(h.scene().asked().size() == 1);
            h.render(b);
            withShutter = h.render(a);
            CHECK(h.scene().asked().size() == 3);
            seek = withShutter;
        }
        idle(ctx);
        {
            StageHarness h(ctx, render::RenderMode::Interactive, 8, 0.0, 8, a.localSeconds);
            h.render(a);
            h.render(b);
            without = h.render(a);
        }
        idle(ctx);
        // The motion guide of the window's second frame holds the quad's own motion (it slid 8 px), and the
        // first frame after a seek has none to offer (the previous frame is another time).
        {
            StageHarness h(ctx, render::RenderMode::Interactive, 8, 0.0, 8, a.localSeconds);
            const Plane first = h.renderMotion(a);
            CHECK(first.at(kStageW / 2 + 8, kStageH / 2, 2) == 0.f);
            const Plane second = h.renderMotion(b);
            CHECK(second.at(kStageW / 2 + 8, kStageH / 2, 2) == 1.f);
            CHECK(std::abs(second.at(kStageW / 2 + 8, kStageH / 2, 0) + 8.f) < 0.05f);
            CHECK(std::abs(second.at(kStageW / 2 + 8, kStageH / 2, 1)) < 0.05f);
            CHECK(std::abs(second.at(2, 2, 0)) < 0.05f); // the wall stays
            const Plane seek = h.renderMotion(a);
            CHECK(seek.at(kStageW / 2 + 8, kStageH / 2, 2) == 0.f);
        }
        idle(ctx);
        {
            StageHarness h(ctx, render::RenderMode::Interactive, 8, kShutter, 8, a.localSeconds);
            fresh = h.render(a);
        }
        idle(ctx);
        CHECK(withShutter == without);
        CHECK(seek == fresh);
        std::printf("(g) the window ignores the shutter (same bytes, one scene frame per frame) and its seek "
                    "equals a fresh renderer\n");
    }
    idle(ctx);
}

// --- Look --------------------------------------------------------------------------------------------------

std::uint8_t encode(float linear) {
    const float v = post::srgbOetf(linear / (1.f + linear)); // Reinhard, for looking only
    return static_cast<std::uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
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

// A checker floor under a dusk sky and a lamp, a panning camera, and three spheres: a glowing one crossing
// the picture fast, a chrome one rolling diagonally and a still gold one. `seconds` is the time within a
// second of the show; the speeds are in world units per second.
modern::SceneFrame lookFrame(const TestScene& base, double seconds) {
    modern::SceneFrame frame = base.frame;
    const float t = float(seconds - 1.0); // 0 at the frame's own time
    frame.camera.eye.x += 9.f * t;
    frame.camera.target.x += 9.f * t;
    frame.instances[3].transform = modern::translate({-0.9f + 48.f * t, 0.35f, -2.6f});
    frame.instances[4].transform = modern::translate({1.3f + 9.f * t, 0.45f, -3.6f + 12.f * t});
    return frame;
}

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp) {
    constexpr std::uint32_t w = 480, h = 270;
    constexpr double kLookShutter = 1.0 / 60.0; // a whole frame: exaggerated, to see it
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {checkerMaterial({0.85f, 0.85f, 0.85f}, {0.06f, 0.06f, 0.08f}, 0.5f),
                           test::emitter({7, 6.5f, 6}), test::emitter({14, 9, 3}),
                           test::metal({0.95f, 0.95f, 0.95f}, 0.f), test::metal({1.f, 0.78f, 0.34f}, 0.08f)};
    s.statics.capacity.spheres = 3;
    s.statics.spheres = {sphere({0, 0, 0}, 0.35f, 2), sphere({0, 0, 0}, 0.45f, 3),
                         sphere({0, 0, 0}, 0.5f, 4)};
    s.statics.sphereRanges = {{0, 1}, {1, 1}, {2, 1}};
    auto& in = s.frame.instances;
    in.push_back({test::kQuadMesh, 0, test::place({0, 0, -3}, test::kX, -90, {30, 30, 1})});
    in.push_back({test::kQuadMesh, 0, test::place({0, 3, -9}, test::kY, 0, {30, 6, 1})});
    const rt::Mat34 lamp = test::place({0, 3, -2.5f}, test::kX, 90, {1.5f, 1.5f, 1});
    in.push_back({test::kQuadMesh, 1, lamp, 0});
    s.frame.lights.push_back(modern::rectLightForQuad(lamp, 1, 1, rt::LightKind::Emitter, {7, 6.5f, 6}, 1));
    in.insert(in.begin() + 3, sphereInstance(0, 1, 2));
    in.insert(in.begin() + 4, sphereInstance(1, 1, 3));
    in.push_back(sphereInstance(2, 1, 4, modern::translate({0.2f, 0.5f, -3.6f})));
    s.frame.camera = {{-0.6f, 1.2f, 1.6f}, {-0.2f, 0.5f, -3.8f}, {0, 1, 0}, 50.f};
    s.frame.environment = modern::environmentDefaults(modern::EnvironmentKind::Dusk);
    s.frame.environment.select = 0.3f;

    Tracer tracer(ctx, s, w, h);
    std::filesystem::create_directories(dir);
    for (const bool shutter : {false, true}) {
        const auto start = std::chrono::steady_clock::now();
        const std::uint32_t slices = modern::shutterSlices(spp, shutter ? kLookShutter : 0.0);
        std::vector<modern::SceneFrame> frames;
        for (std::uint32_t k = 0; k < slices; ++k)
            frames.push_back(
                lookFrame(s, 1.0 + modern::sliceOffset(k, slices, kLookShutter * (shutter ? 1 : 0))));
        const Frame f = tracer.traceSlices(frames, {.spp = spp, .maxBounces = 5, .frameSeed = 1});
        std::vector<std::uint8_t> rgb(std::size_t(w) * h * 3);
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x)
                for (std::uint32_t c = 0; c < 3; ++c)
                    rgb[(std::size_t(y) * w + x) * 3 + c] =
                        encode(f.lighting.at(x, y, c) + f.emission.at(x, y, c));
        const std::filesystem::path path = dir / (shutter ? "motion_look_blur.png" : "motion_look_sharp.png");
        image::writePng(path, int(w), int(h), rgb.data());
        std::printf("look: %s (%u slices, %u spp) in %.1f s\n", path.string().c_str(), slices, spp,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    }
}

void testShutterMath() {
    constexpr double s = 1.0 / 120.0;
    CHECK(modern::kMaxShutterSlices == 8);
    // 4 samples per dispatch, one slice per dispatch, at most 8; no shutter, one slice.
    CHECK(modern::shutterSlices(8, s) == 2);
    CHECK(modern::shutterSlices(64, s) == 8);
    CHECK(modern::shutterSlices(4, s) == 1);
    CHECK(modern::shutterSlices(1, s) == 1);
    CHECK(modern::shutterSlices(5, s) == 2);
    CHECK(modern::shutterSlices(32, s) == 8);
    CHECK(modern::shutterSlices(33, s) == 8);
    CHECK(modern::shutterSlices(12, s) == 3);
    CHECK(modern::shutterSlices(64, 0.0) == 1);
    // Offsets are the slices' midpoints within [-shutter / 2, shutter / 2].
    CHECK(modern::sliceOffset(0, 1, s) == 0.0);
    CHECK(std::abs(modern::sliceOffset(0, 2, s) + 0.25 * s) < 1e-15);
    CHECK(std::abs(modern::sliceOffset(1, 2, s) - 0.25 * s) < 1e-15);
    double sum = 0;
    for (std::uint32_t k = 0; k < 8; ++k)
        sum += modern::sliceOffset(k, 8, s);
    CHECK(std::abs(sum) < 1e-15);
    CHECK(std::abs(modern::sliceOffset(0, 8, s) + (7.0 / 16.0) * s) < 1e-15);
    // Dispatches are split evenly over the slices, every slice getting at least one.
    for (std::uint32_t slices = 1; slices <= 8; ++slices)
        for (std::uint32_t dispatches = slices; dispatches <= 16; ++dispatches) {
            std::uint32_t last = 0, perSlice[8] = {};
            for (std::uint32_t d = 0; d < dispatches; ++d) {
                const std::uint32_t k = modern::sliceOfDispatch(d, dispatches, slices);
                CHECK(k >= last && k < slices);
                last = k;
                ++perSlice[k];
            }
            for (std::uint32_t k = 0; k < slices; ++k) {
                CHECK(perSlice[k] >= 1);
                CHECK(perSlice[k] <= (dispatches + slices - 1) / slices);
            }
        }
    std::printf("(f) shutter: slice counts, offsets and the split of dispatches\n");
}

// Every slice of a frame at either edge of every chapter, with a shutter of a whole frame (360 degrees) and
// 8 slices, shows the frame's own chapter.
void testSlicesStayInChapter() {
    constexpr double fps = 60.0;
    int frames = 0;
    for (int chapter = 0; chapter < show::chapterCount(); ++chapter) {
        const double start = show::chapterStart(chapter);
        const double end =
            chapter + 1 < show::chapterCount() ? show::chapterStart(chapter + 1) : show::showDuration();
        for (const double time :
             {start, start + 1.0 / 240.0, end - 1.0 / fps, end - 1.0 / 240.0, end - 1e-7}) {
            if (!(time >= start && time < end))
                continue;
            const show::TimelineFrame frame = show::sampleTimeline(time);
            CHECK(frame.chapter == chapter);
            for (std::uint32_t k = 0; k < 8; ++k) {
                const double local =
                    modern::sliceSeconds(frame.localSeconds, frame.chapterDuration, k, 8, 1.0 / fps);
                CHECK(local >= 0.0 && local <= frame.chapterDuration - 1e-6 + 1e-12);
                const show::TimelineFrame slice = modern::sliceFrame(frame, local);
                CHECK(slice.chapter == chapter);
                CHECK(slice.effect == frame.effect);
                CHECK(std::abs(slice.localSeconds - local) < 1e-9);
                CHECK(std::abs(slice.showSeconds - (frame.showSeconds + (local - frame.localSeconds))) <
                      1e-9);
                ++frames;
            }
        }
    }
    // FLYBY's last frame: no slice reaches the chapter after it.
    int flyby = -1;
    for (int chapter = 0; chapter < show::chapterCount(); ++chapter)
        if (show::sampleTimeline(show::chapterStart(chapter) + 0.01).effect == show::Effect::Flyby)
            flyby = chapter;
    CHECK(flyby >= 0 && flyby + 1 < show::chapterCount());
    const show::TimelineFrame last = show::sampleTimeline(show::chapterStart(flyby + 1) - 1.0 / fps);
    CHECK(last.effect == show::Effect::Flyby);
    for (std::uint32_t k = 0; k < 8; ++k) {
        const show::TimelineFrame slice = modern::sliceFrame(
            last, modern::sliceSeconds(last.localSeconds, last.chapterDuration, k, 8, 1.0 / fps));
        CHECK(slice.effect == show::Effect::Flyby && slice.chapter == flyby);
    }
    // Where nothing is clamped the offset is applied as it is.
    const show::TimelineFrame middle = show::sampleTimeline(show::chapterStart(flyby) + 5.0);
    CHECK(std::abs(modern::sliceSeconds(middle.localSeconds, middle.chapterDuration, 0, 2, 0.5) -
                   (middle.localSeconds - 0.125)) < 1e-12);
    std::printf("(f) slices stay in their chapter: %d slice frames over every chapter's edges\n", frames);
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
    try {
        if (argc >= 3 && std::string(argv[1]) == "--look") { // a picture to look at instead of the checks
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 32u);
            return 0;
        }
        testShutterMath();
        testSlicesStayInChapter();
        auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
        if (!ctx.syncValidationEnabled()) {
            std::printf("SKIP sync validation (the layer lacks it)\n");
            return 0;
        }
        testQuadMotion(ctx);
        testSphereMotion(ctx);
        testParticleMotion(ctx);
        testDenoiserFollows(ctx);
        testShutter(ctx);
        testStage(ctx);
        CHECK(ctx.validationMessageCount() == 0);
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("motion_test: %d checks passed in %.1f s\n", checksPassed, seconds);
    return 0;
}
