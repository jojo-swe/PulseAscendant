// Caustic photon maps on lavapipe, with the validation layer and its synchronization
// validation on; fails on any message. The scene: a smooth glass sphere (an analytic sphere, IOR 1.5) over a
// white Lambert floor that is the receiver, lit by an overhead disc of light (the environment's; nothing else
// in the sky) or by a small rect light.
//
// (a) energy: a disc of 0.01 rad: the map's peak irradiance is >= 10x the direct irradiance, and its total
//     flux is <= the flux on the sphere's cross-section x (1 - R0) + 5 % (and >= 60 % of it); the same from a
//     rect light;
// (b) dispersion: with Abbe 30 the red light's 50 %-flux radius exceeds the blue light's (a plane before the
//     foci); with Abbe 0 the channels are equal;
// (c) determinism: the same seed gives the same map, bit for bit, whatever order the photons run in; another
//     seed gives another map;
// (d) exclusion: with an empty map (photons 0) the floor under the sphere shows no BSDF-found caustic (the
//     focus is no brighter than the shadow ring + 10 %), though plain path tracing finds one there (>= 3x);
//     with the map the focus is >= 5x the shadow ring;
// (f) the photon-mapped caustic under the sphere equals the converged path-traced caustic within 10 % (not
//     twice it: the path tracer does not add the same paths again);
// (e) a receiver-flagged material without caustics in the frame traces the same bytes as without the flag;
// (r) refusals (checkCausticFrame, a trace without the maps, prepare and record out of order);
// (s) the modern stage: one CausticMaps measured by the allocator; an export frame after another time equals
//     a fresh renderer's (nothing carried over).
//
// `caustics_test --look <dir> [spp]` renders a still to look at: a glass sphere and a flint prism on a lit
// checker floor.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/caustics.hpp"
#include "modern/mesh_builder.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scene_gpu.hpp"
#include "post/tonemap_reference.hpp"
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
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
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

constexpr float kPi = std::numbers::pi_v<float>;

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

// Copies one layer of an image (RGBA32F or RGBA16F) into a host buffer within the frame being recorded.
PendingRead recordRead(gfx::Context& ctx, VkCommandBuffer cmd, gfx::Image& image, std::uint32_t layer = 0) {
    const VkFormat format = image.format();
    const std::uint32_t bytesPerPixel = format == VK_FORMAT_R32G32B32A32_SFLOAT   ? 16
                                        : format == VK_FORMAT_R16G16B16A16_SFLOAT ? 8
                                                                                  : 0;
    if (bytesPerPixel == 0)
        throw std::logic_error("recordRead: unexpected format");
    const VkExtent2D extent = image.extent();
    PendingRead read{gfx::Buffer(ctx, {VkDeviceSize(extent.width) * extent.height * bytesPerPixel,
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "caustics test readback"}),
                     format, extent.width, extent.height};
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
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
    plane.channels = 4;
    const auto* bytes = static_cast<const std::uint8_t*>(read.buffer.mapped());
    const std::size_t values = std::size_t(read.width) * read.height * 4;
    plane.values.resize(values);
    if (read.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
        for (std::size_t i = 0; i < values; ++i) {
            std::uint16_t h;
            std::memcpy(&h, bytes + 2 * i, 2);
            plane.values[i] = halfToFloat(h);
        }
    } else {
        std::memcpy(plane.values.data(), bytes, values * sizeof(float));
    }
    return plane;
}

// --- The scenes --------------------------------------------------------------------------------------------

rt::SphereRecord sphereRecord(Vec3 center, float radius, std::uint32_t material) {
    rt::SphereRecord s{};
    s.center[0] = s.prevCenter[0] = center.x;
    s.center[1] = s.prevCenter[1] = center.y;
    s.center[2] = s.prevCenter[2] = center.z;
    s.radius = radius;
    s.color[0] = s.color[1] = s.color[2] = 1.f;
    s.material = material;
    return s;
}

SceneInstance sphereInstance(std::uint32_t first, std::uint32_t count, std::uint32_t material) {
    SceneInstance in{0, material, modern::identity()};
    in.kind = rt::GeometryKind::Spheres;
    in.firstSphere = first;
    in.sphereCount = count;
    return in;
}

// The receiver's square of the floor: [-mapHalf, mapHalf] in x and z.
rt::Mat34 floorPatch(float half) {
    return test::place({0, 0, 0}, test::kX, -90, {2 * half, 2 * half, 1});
}

struct Setup {
    float sphereY = 0.75f, radius = 0.5f; // the paraxial focus of IOR 1.5 lies on the floor
    float abbe = 0.f;
    float discRadius = 0.01f; // radians; the light is overhead
    float irradiance = 1.f;   // the disc's (or the rect's, at the sphere's centre) on the floor
    bool rectLight = false;   // a square rect light overhead instead of the disc
    float rectSize = 0.2f, rectHeight = 6.f;
    float mapHalf = 0.6f; // the receiver's square
    std::uint32_t resolution = 128;
    std::uint32_t photons = 1u << 16;
    bool receiverFlag = true; // the floor's material is the receiver
    bool caustics = true;     // the frame has caustics
};

constexpr float kFloorAlbedo = 0.8f;

// Material 0 the floor (white Lambert, the receiver), 1 the glass; instance 0 the floor (8 x 8), 1 the
// sphere (the caster), 2 the rect light's quad when there is one.
TestScene causticScene(const Setup& setup) {
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    rt::Material floor = test::lambert({kFloorAlbedo, kFloorAlbedo, kFloorAlbedo});
    if (setup.receiverFlag) {
        floor.flags |= rt::kMaterialCausticReceiver;
        floor.receiver = 0;
    }
    rt::Material glass = test::glass(1.5f, 0.f);
    glass.abbe = setup.abbe;
    s.statics.materials = {floor, glass};
    s.statics.capacity.spheres = 1;
    s.statics.capacity.causticResolution = setup.resolution;
    s.statics.spheres = {sphereRecord({0, setup.sphereY, 0}, setup.radius, 1)};
    s.statics.sphereRanges = {{0, 1}};
    s.frame.instances = {{test::kQuadMesh, 0, test::place({0, 0, 0}, test::kX, -90, {8, 8, 1})},
                         sphereInstance(0, 1, 1)};
    modern::EnvironmentDesc& env = s.frame.environment;
    env = modern::environmentDefaults(modern::EnvironmentKind::Space);
    env.zenith = env.horizon = env.ground = {0, 0, 0};
    env.starBrightness = 0;
    env.glow = 0;
    if (setup.rectLight) {
        // E at the sphere's centre = L * A / d^2 (a small light straight above).
        const float d = setup.rectHeight - setup.sphereY;
        const float l = setup.irradiance * d * d / (setup.rectSize * setup.rectSize);
        s.statics.materials.push_back(test::emitter({l, l, l}));
        const rt::Mat34 lamp =
            test::place({0, setup.rectHeight, 0}, test::kX, 90, {setup.rectSize, setup.rectSize, 1});
        s.frame.instances.push_back({test::kQuadMesh, 2, lamp, 0});
        s.frame.lights = {modern::rectLightForQuad(lamp, 1, 1, rt::LightKind::Emitter, {l, l, l}, 1)};
        env.select = 0;
    } else {
        env.discDirection = {0, 1, 0};
        env.discAngularRadius = setup.discRadius;
        const float sin = std::sin(setup.discRadius);
        const float l = setup.irradiance / (kPi * sin * sin); // a uniform disc overhead: E = L pi sin^2
        env.discRadiance = {l, l, l};
        env.select = 1;
    }
    if (setup.caustics) {
        modern::CausticFrame c;
        c.receivers = {modern::receiverForQuad(floorPatch(setup.mapHalf), 1, 1, setup.resolution)};
        c.photons = setup.photons;
        c.light = setup.rectLight ? 0 : -1;
        c.casterInstanceFirst = 1;
        c.casterInstanceCount = 1;
        s.frame.caustics = c;
    }
    // Low over the floor, looking under the sphere at the focus.
    s.frame.camera = {{0, 0.9f, 2.0f}, {0, 0, 0}, {0, 1, 0}, 24.f};
    return s;
}

struct Result {
    Plane lighting, emission, map;
};

// The ring, the caustic maps and one path tracer every subtest shares (a ray tracing pipeline is slow to make
// on lavapipe), declared in the order they must outlive each other: the tracer's TLAS goes first and waits
// for the ring, then the maps, which wait too. Each run makes the scene's GPU side and waits for the frame.
constexpr std::uint32_t kImage = 64, kMaxResolution = 256;

class Harness {
public:
    explicit Harness(gfx::Context& ctx, std::uint32_t width = kImage, std::uint32_t height = kImage,
                     std::uint32_t maxResolution = kMaxResolution)
        : ctx_(ctx), ring_(ctx), maps_(ctx, ring_, maxResolution),
          tracer_(ctx, ring_, width, height, modern::tracerCapacity({})) {}

    Result run(const TestScene& scene, const TraceSettings& settings, std::uint32_t seed) {
        const modern::SceneGpu gpu(ctx_, scene.statics);
        VkCommandBuffer cmd = ring_.begin();
        modern::TraceExtras extras;
        if (scene.frame.caustics) {
            maps_.prepare(scene.statics, scene.frame, scene.frame.caustics->photons, seed);
            extras.causticMaps = &maps_.maps();
            extras.causticReceivers = &maps_.receiverTable();
            extras.photons = [this](VkCommandBuffer c, const modern::PhotonScene& p) { maps_.record(c, p); };
        }
        tracer_.trace(cmd, gpu, scene.frame, std::nullopt, settings, &unused_, extras);
        modern::GBuffer& g = tracer_.gbuffer();
        PendingRead lighting = recordRead(ctx_, cmd, g.lighting);
        PendingRead emission = recordRead(ctx_, cmd, g.emission);
        std::optional<PendingRead> map;
        if (scene.frame.caustics)
            map = recordRead(ctx_, cmd, maps_.maps(), 0);
        ring_.wait(ring_.submit());
        idle(ctx_);
        Result r{finishRead(lighting), finishRead(emission), {}};
        if (map)
            r.map = finishRead(*map);
        return r;
    }

    gfx::FrameRing& ring() { return ring_; }
    modern::CausticMaps& maps() { return maps_; }
    modern::PathTracer& tracer() { return tracer_; }

private:
    gfx::Context& ctx_;
    gfx::Image unused_{ctx_, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "unused retro"}};
    gfx::FrameRing ring_;
    modern::CausticMaps maps_;
    modern::PathTracer tracer_;
};

// A map texel's floor point (x, z) for the receiver floorPatch(half): u along +x, v along -z.
Vec3 texelPoint(std::uint32_t i, std::uint32_t j, std::uint32_t resolution, float half) {
    const float u = (float(i) + 0.5f) / float(resolution), v = (float(j) + 0.5f) / float(resolution);
    return {(u - 0.5f) * 2 * half, 0, -(v - 0.5f) * 2 * half};
}

double texelArea(std::uint32_t resolution, float half) {
    const double side = 2.0 * half / resolution;
    return side * side;
}

double meanRgb(const Plane& p, std::uint32_t x, std::uint32_t y) {
    return (double(p.at(x, y, 0)) + p.at(x, y, 1) + p.at(x, y, 2)) / 3.0;
}

// --- (a) ---------------------------------------------------------------------------------------------------

void testEnergy(Harness& h) {
    const TraceSettings settings{.spp = 1, .maxBounces = 6, .frameSeed = 3};
    for (const bool rect : {false, true}) {
        Setup setup;
        setup.rectLight = rect;
        const TestScene s = causticScene(setup);
        const Result r = h.run(s, settings, 77);
        double peak = 0, flux = 0;
        for (std::uint32_t j = 0; j < setup.resolution; ++j)
            for (std::uint32_t i = 0; i < setup.resolution; ++i) {
                const double e = meanRgb(r.map, i, j);
                CHECK(std::isfinite(e) && e >= 0);
                peak = std::max(peak, e);
                flux += e * texelArea(setup.resolution, setup.mapHalf);
            }
        // The flux on the sphere's cross-section, less what its first surface reflects at normal incidence.
        const double r0 = 0.04;
        const double crossSection = kPi * setup.radius * setup.radius * setup.irradiance;
        std::printf(
            "(a) %s: peak irradiance %.1f x the direct; caustic flux %.4f = %.3f of the cross-section's "
            "%.4f\n",
            rect ? "rect light" : "disc", peak / setup.irradiance, flux, flux / crossSection, crossSection);
        CHECK(peak >= 10 * setup.irradiance);
        CHECK(flux <= crossSection * (1 - r0) * 1.05);
        CHECK(flux >= crossSection * 0.6);
    }
}

// --- (b) ---------------------------------------------------------------------------------------------------

// The radius within which half of a channel's flux inside `window` lies.
double halfFluxRadius(const Plane& map, const Setup& setup, std::uint32_t channel, double window) {
    std::vector<std::pair<double, double>> rings;
    double total = 0;
    for (std::uint32_t j = 0; j < setup.resolution; ++j)
        for (std::uint32_t i = 0; i < setup.resolution; ++i) {
            const Vec3 p = texelPoint(i, j, setup.resolution, setup.mapHalf);
            const double radius = std::sqrt(double(p.x) * p.x + double(p.z) * p.z);
            if (radius >= window)
                continue;
            rings.push_back({radius, map.at(i, j, channel)});
            total += map.at(i, j, channel);
        }
    std::sort(rings.begin(), rings.end());
    double sum = 0;
    for (const auto& [radius, e] : rings) {
        sum += e;
        if (sum >= 0.5 * total)
            return radius;
    }
    return window;
}

void testDispersion(Harness& h) {
    const TraceSettings settings{.spp = 1, .maxBounces = 6, .frameSeed = 3};
    std::array<double, 2> ratio{};
    for (const float abbe : {30.f, 0.f}) {
        Setup setup;
        setup.abbe = abbe;
        setup.sphereY = 0.55f; // the floor 0.05 below the sphere: before every colour's focus
        setup.discRadius = 0.003f;
        setup.mapHalf = 0.3f;
        setup.resolution = 256;
        const TestScene s = causticScene(setup);
        const Result r = h.run(s, settings, 5);
        const double red = halfFluxRadius(r.map, setup, 0, 0.25),
                     green = halfFluxRadius(r.map, setup, 1, 0.25),
                     blue = halfFluxRadius(r.map, setup, 2, 0.25);
        double fluxR = 0, fluxB = 0;
        for (std::uint32_t j = 0; j < setup.resolution; ++j)
            for (std::uint32_t i = 0; i < setup.resolution; ++i) {
                fluxR += r.map.at(i, j, 0);
                fluxB += r.map.at(i, j, 2);
            }
        std::printf("(b) Abbe %.0f: half-flux radius red %.4f, green %.4f, blue %.4f; flux blue / red %.3f\n",
                    abbe, red, green, blue, fluxB / fluxR);
        ratio[abbe > 0 ? 0 : 1] = red / blue;
        CHECK(std::abs(fluxB / fluxR - 1) < 0.1); // spectralRgb keeps white white on average
    }
    CHECK(ratio[0] > 1.03); // red focuses further away: at this plane its ring is the wider one
    CHECK(std::abs(ratio[1] - 1) < 1e-6);
}

// --- (c) ---------------------------------------------------------------------------------------------------

void testDeterminism(Harness& h) {
    const TraceSettings settings{.spp = 1, .maxBounces = 6, .frameSeed = 3};
    Setup setup;
    setup.abbe = 30; // hero wavelengths too
    const TestScene s = causticScene(setup);
    const Plane a = h.run(s, settings, 11).map;
    const Plane again = h.run(s, settings, 11).map;
    h.maps().setPhotonOrder(2654435761u, 12345u);
    const Plane permuted = h.run(s, settings, 11).map;
    h.maps().setPhotonOrder(1, 0);
    const Plane other = h.run(s, settings, 12).map;
    CHECK(a.values == again.values);
    CHECK(a.values == permuted.values);
    CHECK(a.values != other.values);
    double sum = 0;
    for (const float v : a.values)
        sum += v;
    CHECK(sum > 0);
    std::printf("(c) the same seed: the same map, in any photon order; another seed: another map\n");
}

// --- (d), (f) ----------------------------------------------------------------------------------------------

// The camera ray through a pixel centre (pinhole), as pathtrace.rgen's cameraDirection.
Vec3 pixelDirection(const Camera& camera, std::uint32_t x, std::uint32_t y) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const float tanHalf = std::tan(camera.fovYDegrees * kPi / 360.f);
    const float sx = (2.f * (float(x) + 0.5f) / float(kImage) - 1.f) * tanHalf;
    const float sy = (1.f - 2.f * (float(y) + 0.5f) / float(kImage)) * tanHalf;
    return modern::normalize(b.forward + b.right * sx + b.up * sy);
}

// The floor radius |(x, z)| a pixel sees, or -1 where the pixel's rays may meet the sphere first.
double floorRadius(const Setup& setup, const Camera& camera, std::uint32_t x, std::uint32_t y) {
    const Vec3 d = pixelDirection(camera, x, y);
    if (!(d.y < 0))
        return -1;
    const float t = -camera.eye.y / d.y;
    const Vec3 c{0, setup.sphereY, 0};
    const Vec3 f = camera.eye - c;
    const float along = modern::dot(f, d);
    const float dist2 = modern::dot(f, f) - along * along;
    if (std::sqrt(std::max(dist2, 0.f)) < setup.radius * 1.1f && -along < t)
        return -1; // the line passes near the sphere before the floor
    const Vec3 p = camera.eye + d * t;
    return std::sqrt(double(p.x) * p.x + double(p.z) * p.z);
}

struct Regions {
    double focus = 0, ring = 0, under = 0; // mean radiance (lighting + emission, mean of rgb)
    int focusPixels = 0, ringPixels = 0, underPixels = 0;
};

constexpr double kFocusRadius = 0.05, kRingInner = 0.15, kRingOuter = 0.35, kUnder = 0.4;

Regions regions(const Setup& setup, const Camera& camera, const Result& r) {
    Regions g;
    for (std::uint32_t y = 0; y < kImage; ++y)
        for (std::uint32_t x = 0; x < kImage; ++x) {
            const double radius = floorRadius(setup, camera, x, y);
            if (radius < 0)
                continue;
            const double v = meanRgb(r.lighting, x, y) + meanRgb(r.emission, x, y);
            if (radius < kFocusRadius) {
                g.focus += v;
                ++g.focusPixels;
            }
            if (radius > kRingInner && radius < kRingOuter) {
                g.ring += v;
                ++g.ringPixels;
            }
            if (radius < kUnder) {
                g.under += v;
                ++g.underPixels;
            }
        }
    g.focus /= std::max(g.focusPixels, 1);
    g.ring /= std::max(g.ringPixels, 1);
    g.under /= std::max(g.underPixels, 1);
    return g;
}

// Lights wide enough for the path tracer to converge on its caustic: a disc of 0.1 rad, or a 1 x 1 rect at 3.
Setup pathSetup(bool rect) {
    Setup setup;
    setup.discRadius = 0.1f;
    setup.rectLight = rect;
    setup.rectSize = 1.f;
    setup.rectHeight = 3.f;
    return setup;
}

TraceSettings pathSettings(std::uint32_t spp) {
    TraceSettings t{.spp = spp, .maxBounces = 6, .frameSeed = 21};
    t.russianRoulette = false;
    t.clampIndirect = 1e9f;
    return t;
}

Regions renderRegions(Harness& h, const Setup& setup, std::uint32_t spp) {
    const TestScene s = causticScene(setup);
    return regions(setup, s.frame.camera, h.run(s, pathSettings(spp), 99));
}

void testExclusion(Harness& h, bool rect, Regions& traced, Regions& empty, Regions& mapped) {
    Setup setup = pathSetup(rect);
    setup.caustics = false;
    traced = renderRegions(h, setup, 256); // plain path tracing: its own caustic
    setup.caustics = true;
    setup.photons = 0;
    empty = renderRegions(h, setup, 16); // the exclusion with an empty map
    setup.photons = 1u << 16;
    mapped = renderRegions(h, setup, 16);
    std::printf(
        "(d) %s: focus / shadow ring (%d / %d pixels): path traced %.4f / %.4f, empty map %.5f / %.5f, "
        "photon map %.4f / %.4f\n",
        rect ? "rect" : "disc", traced.focusPixels, traced.ringPixels, traced.focus, traced.ring, empty.focus,
        empty.ring, mapped.focus, mapped.ring);
    CHECK(traced.focusPixels >= 8 && traced.ringPixels >= 30);
    CHECK(traced.focus >= 3 * traced.ring);        // the path tracer does find the caustic
    CHECK(empty.focus <= 1.1 * empty.ring + 1e-3); // and excludes all of it
    CHECK(mapped.focus >= 5 * mapped.ring);        // the map draws it
}

void testAgainstPathTracing(bool rect, const Regions& traced, const Regions& empty, const Regions& mapped) {
    const double pathCaustic = traced.under - empty.under, photonCaustic = mapped.under - empty.under;
    std::printf("(f) %s: the caustic under the sphere (%d pixels): path traced %.4f, photon map %.4f (ratio "
                "%.3f); all light there %.4f against %.4f\n",
                rect ? "rect" : "disc", traced.underPixels, pathCaustic, photonCaustic,
                photonCaustic / pathCaustic, mapped.under, traced.under);
    CHECK(pathCaustic > 0.05);
    CHECK(std::abs(photonCaustic / pathCaustic - 1) < 0.1);
    CHECK(std::abs(mapped.under / traced.under - 1) < 0.1); // not twice the light
}

// --- (e) ---------------------------------------------------------------------------------------------------

void testBaseline(Harness& h) {
    const TraceSettings settings{.spp = 2, .maxBounces = 4, .frameSeed = 8};
    TestScene plain = test::cornellBox();
    TestScene flagged = plain;
    flagged.statics.materials[0].flags |= rt::kMaterialCausticReceiver;
    flagged.statics.materials[0].receiver = 0;
    const Result a = h.run(plain, settings, 1), b = h.run(flagged, settings, 1);
    CHECK(a.lighting.values == b.lighting.values);
    CHECK(a.emission.values == b.emission.values);
    std::printf("(e) a receiver without caustics in the frame traces the same bytes\n");
}

// --- (r) ---------------------------------------------------------------------------------------------------

void testRefusals(gfx::Context& ctx, Harness& h) {
    const Setup setup;
    const TestScene good = causticScene(setup);
    auto check = [&](const TestScene& s) { modern::checkCausticFrame(s.statics, s.frame, 128); };
    check(good);
    auto refusedArg = [&](auto&& change) {
        TestScene s = good;
        change(s);
        return throws<std::invalid_argument>([&] { check(s); });
    };
    auto refusedLength = [&](auto&& change) {
        TestScene s = good;
        change(s);
        return throws<std::length_error>([&] { check(s); });
    };
    CHECK(refusedLength(
        [](TestScene& s) { s.frame.caustics->receivers.resize(3, s.frame.caustics->receivers[0]); }));
    CHECK(refusedLength([](TestScene& s) { s.frame.caustics->receivers[0].resolution = 129; }));
    CHECK(refusedArg([](TestScene& s) { s.frame.caustics->receivers[0].resolution = 0; }));
    CHECK(refusedArg([](TestScene& s) { s.frame.caustics->receivers[0].worldToUv = rt::Mat34{}; }));
    CHECK(refusedArg([](TestScene& s) { s.frame.caustics->light = 0; })); // no rect lights
    CHECK(refusedArg([](TestScene& s) { s.frame.environment.discAngularRadius = 0; }));
    CHECK(refusedArg([](TestScene& s) { s.frame.caustics->casterInstanceCount = 0; }));
    CHECK(refusedArg([](TestScene& s) { s.frame.caustics->casterInstanceFirst = 2; }));
    CHECK(refusedArg([](TestScene& s) { s.statics.materials[0].receiver = 1; }));
    CHECK(refusedArg([](TestScene& s) { s.statics.materials[1].flags |= rt::kMaterialCausticReceiver; }));
    {
        Setup portal;
        portal.rectLight = true;
        TestScene s = causticScene(portal);
        s.frame.lights[0].kind = static_cast<std::uint32_t>(rt::LightKind::Portal);
        CHECK(throws<std::invalid_argument>([&] { check(s); }));
    }
    CHECK(throws<std::invalid_argument>([] { modern::receiverForQuad(rt::Mat34{}, 1, 1, 64); }));

    // The tracer refuses a frame with caustics but without the maps; the maps refuse prepare() outside a
    // frame and record() without a prepare().
    {
        const modern::SceneGpu gpu(ctx, good.statics);
        CHECK(throws<std::logic_error>([&] { h.maps().prepare(good.statics, good.frame, 64, 1); }));
        VkCommandBuffer cmd = h.ring().begin();
        gfx::Image retro(ctx, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "retro"});
        CHECK(throws<std::invalid_argument>(
            [&] { h.tracer().trace(cmd, gpu, good.frame, std::nullopt, {}, &retro, {}); }));
        CHECK(throws<std::logic_error>([&] { h.maps().record(cmd, {}); }));
        TestScene none = good;
        none.frame.caustics.reset();
        CHECK(throws<std::invalid_argument>([&] { h.maps().prepare(none.statics, none.frame, 64, 1); }));
        h.ring().wait(h.ring().submit());
        idle(ctx);
    }
    std::printf("(r) refusals\n");
}

// --- (s) ---------------------------------------------------------------------------------------------------

constexpr std::uint32_t kStageW = 48, kStageH = 32;

// The caustic scene for the modern stage: the disc tilts with the chapter-local time (the camera does not).
class CausticModernScene final : public modern::ModernScene {
public:
    CausticModernScene() : scene_(causticScene(Setup{})) {}
    const modern::SceneStatic& staticScene() const override { return scene_.statics; }
    modern::SceneFrame frame(const show::TimelineFrame& t) const override {
        modern::SceneFrame f = scene_.frame;
        const float tilt = 0.02f * static_cast<float>(t.localSeconds);
        f.environment.discDirection = modern::normalize(Vec3{std::sin(tilt), 1, 0});
        return f;
    }
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
    explicit StageHarness(gfx::Context& ctx)
        : readback_(ctx, kStageW, kStageH), ring_(ctx), renderer_(ctx, ring_, settings()) {}
    std::vector<std::uint8_t> render(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return readback_.rgb();
    }
    render::Renderer& renderer() { return renderer_; }

private:
    static render::RenderSettings settings() {
        render::RenderSettings s{kStageW, kStageH};
        s.hud = false;
        s.mode = render::RenderMode::Export;
        s.spp = 1;
        s.sceneFactory = [](modern::SceneId id,
                            modern::SceneDetail detail) -> std::unique_ptr<modern::ModernScene> {
            if (id == modern::SceneId::Glass)
                return std::make_unique<CausticModernScene>();
            return modern::makeScene(id, detail);
        };
        s.sceneMap = [](show::Effect effect) {
            return effect == show::Effect::Flyby ? modern::SceneId::Glass : modern::sceneFor(effect);
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
    const show::TimelineFrame b = forced2026(start + 0.6 * (end - start));
    std::vector<std::uint8_t> exportA, exportB;
    {
        StageHarness h(ctx);
        render::ModernStage& stage = h.renderer().modern();
        CHECK(stage.caustics() == nullptr);
        const VkDeviceSize stageImages = stage.hdr().bytes() + stage.image().bytes();
        const VkDeviceSize before = allocatedBytes(ctx);
        stage.prepare(modern::SceneId::Glass);
        CHECK(stage.caustics() != nullptr && stage.caustics()->maxResolution() == 128);
        CHECK(allocatedBytes(ctx) - before == stage.gpuBytes() - stageImages);
        CHECK(stage.gpuBytes() == stage.imageBytes() + stage.tracer()->bufferBytes() +
                                      stage.sceneGpu(modern::SceneId::Glass)->bytes() +
                                      stage.caustics()->bytes());
        exportA = h.render(a);
        exportB = h.render(b);
    }
    idle(ctx);
    {
        StageHarness h(ctx);
        CHECK(h.render(b) == exportB);
    }
    idle(ctx);
    CHECK(exportA != exportB);
    std::printf("(s) modern stage: one CausticMaps measured by the allocator; an export frame after another "
                "time equals a fresh renderer's\n");
}

// --- Look --------------------------------------------------------------------------------------------------

std::uint8_t encode(float linear) {
    const float v = post::srgbOetf(linear / (1.f + linear)); // Reinhard, for looking only
    return static_cast<std::uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
}

// A triangular prism: a triangle of side `side` in xy (apex up), extruded `depth` along z, centred.
rt::Mesh prism(float side, float depth) {
    const float h = side * std::sqrt(3.f) / 2.f, z = depth / 2.f;
    const std::array<Vec3, 3> tri{Vec3{-side / 2, -h / 3, 0}, Vec3{side / 2, -h / 3, 0},
                                  Vec3{0, 2 * h / 3, 0}};
    rt::Mesh m;
    auto face = [&](std::array<Vec3, 4> q, Vec3 n) { // a quad (or a triangle with q[3] = q[2])
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        for (const Vec3& p : q)
            m.vertices.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, {0, 0}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    face({Vec3{tri[0].x, tri[0].y, z}, Vec3{tri[1].x, tri[1].y, z}, Vec3{tri[2].x, tri[2].y, z},
          Vec3{tri[2].x, tri[2].y, z}},
         {0, 0, 1});
    face({Vec3{tri[0].x, tri[0].y, -z}, Vec3{tri[2].x, tri[2].y, -z}, Vec3{tri[1].x, tri[1].y, -z},
          Vec3{tri[1].x, tri[1].y, -z}},
         {0, 0, -1});
    for (int e = 0; e < 3; ++e) {
        const Vec3 a = tri[e], b = tri[(e + 1) % 3];
        const Vec3 n = modern::normalize(Vec3{b.y - a.y, a.x - b.x, 0});
        face({Vec3{a.x, a.y, z}, Vec3{a.x, a.y, -z}, Vec3{b.x, b.y, -z}, Vec3{b.x, b.y, z}}, n);
    }
    // Degenerate triangles (the caps' fourth corner) have no area; drop them.
    std::vector<std::uint32_t> kept;
    for (std::size_t i = 0; i < m.indices.size(); i += 3) {
        const auto& v = m.vertices;
        const Vec3 p0{v[m.indices[i]].position[0], v[m.indices[i]].position[1], v[m.indices[i]].position[2]};
        const Vec3 p1{v[m.indices[i + 1]].position[0], v[m.indices[i + 1]].position[1],
                      v[m.indices[i + 1]].position[2]};
        const Vec3 p2{v[m.indices[i + 2]].position[0], v[m.indices[i + 2]].position[1],
                      v[m.indices[i + 2]].position[2]};
        if (modern::length(modern::cross(p1 - p0, p2 - p0)) > 1e-9f)
            kept.insert(kept.end(), {m.indices[i], m.indices[i + 1], m.indices[i + 2]});
    }
    m.indices = kept;
    return m;
}

constexpr std::uint32_t kExportPhotons = (1u << 20) * modern::kExportPhotonFactor;

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp) {
    const auto start = std::chrono::steady_clock::now();
    constexpr std::uint32_t kW = 640, kH = 360;
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.meshes.push_back(prism(0.5f, 0.8f)); // mesh 3: apex edge along z
    rt::Material floor = test::lambert({0.78f, 0.77f, 0.74f});
    floor.baseColor2[0] = 0.6f;
    floor.baseColor2[1] = 0.6f;
    floor.baseColor2[2] = 0.6f;
    floor.patternScale = 0.5f;
    floor.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Checker);
    floor.flags |= rt::kMaterialCausticReceiver;
    floor.receiver = 0;
    rt::Material crown = test::glass(1.5f, 0.f);
    crown.abbe = 40.f;
    rt::Material flint = test::glass(1.62f, 0.f);
    flint.abbe = 12.f; // more than any real flint, for the show
    s.statics.materials = {floor, crown, flint};
    s.statics.capacity.spheres = 1;
    s.statics.capacity.causticResolution = 512;
    s.statics.spheres = {sphereRecord({-0.9f, 0.5f, 0.3f}, 0.5f, 1)};
    s.statics.sphereRanges = {{0, 1}};
    // The prism floats, turned so that the sun passes it near minimum deviation and lays a spectrum below.
    s.frame.instances = {{test::kQuadMesh, 0, test::place({0, 0, 0}, test::kX, -90, {20, 20, 1})},
                         sphereInstance(0, 1, 1),
                         {3, 2, test::place({0.45f, 0.95f, -0.35f}, test::kZ, -54, {1, 1, 1})}};
    // An evening sun from the left, 30 degrees up (a disc of 0.012 rad, 2 on a surface facing it), the sky
    // dimmed.
    modern::EnvironmentDesc& env = s.frame.environment;
    env = modern::environmentDefaults(modern::EnvironmentKind::Dusk);
    env.zenith = env.zenith * 0.3f;
    env.horizon = env.horizon * 0.3f;
    env.ground = env.ground * 0.3f;
    env.discDirection = modern::normalize(Vec3{-0.866f, 0.5f, -0.12f});
    env.discAngularRadius = 0.012f;
    const float sun = 2.f / (kPi * std::sin(0.012f) * std::sin(0.012f));
    env.discRadiance = {sun, 0.93f * sun, 0.8f * sun};
    env.select = 0.7f;
    modern::CausticFrame c;
    c.receivers = {modern::receiverForQuad(floorPatch(2.f), 1, 1, 512)};
    c.photons = kExportPhotons; // an export's count
    c.light = -1;
    c.casterInstanceFirst = 1;
    c.casterInstanceCount = 2;
    s.frame.caustics = c;
    s.frame.camera = {{0.2f, 1.7f, 2.7f}, {-0.05f, 0.35f, -0.2f}, {0, 1, 0}, 42.f};

    Harness h(ctx, kW, kH, 512);
    TraceSettings settings{.spp = spp, .maxBounces = 6, .frameSeed = 1};
    const Result r = h.run(s, settings, 1);
    std::vector<std::uint8_t> rgb(std::size_t(kW) * kH * 3);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x)
            for (std::uint32_t ch = 0; ch < 3; ++ch)
                rgb[(std::size_t(y) * kW + x) * 3 + ch] =
                    encode(0.35f * (r.lighting.at(x, y, ch) + r.emission.at(x, y, ch)));
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / ("caustics_look_" + std::to_string(spp) + "spp.png");
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
            {
                Harness h(ctx);
                timed("r", [&] { testRefusals(ctx, h); });
                timed("e", [&] { testBaseline(h); });
                timed("a", [&] { testEnergy(h); });
                timed("b", [&] { testDispersion(h); });
                timed("c", [&] { testDeterminism(h); });
                for (const bool rect : {false, true}) {
                    Regions traced, empty, mapped;
                    timed("d", [&] { testExclusion(h, rect, traced, empty, mapped); });
                    timed("f", [&] { testAgainstPathTracing(rect, traced, empty, mapped); });
                }
            }
            idle(ctx);
            timed("s", [&] { testStage(ctx); });
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("caustics_test: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
