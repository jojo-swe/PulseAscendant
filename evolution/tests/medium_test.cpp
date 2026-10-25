// Participating media (step 5, Task 11) on lavapipe, with the validation layer and its synchronization
// validation on; fails on any message. The path tracer runs at 64x64 with grids of 32^3 or less, through a
// MediumVolume baked by a test-only shader (shaders/test/medium_constant.comp).
//
// (0) the CPU side: mediumFlags, packMedium's fields and refusals, majorantSize, mediumBox; Henyey-Greenstein
//     integrates to 1, its sampling follows it (Kolmogorov-Smirnov) with mean cosine g; the fog's closed-form
//     optical depth equals a quadrature; checkSceneFrame's medium refusals;
// (a) Beer-Lambert on the primary segment: homogeneous absorbing fog of sigma 1 over 1 m in front of an
//     emitter gives volume.a = e^-1 (exact, per pixel), a constant grid slab of thickness 1 too (2 %), and a
//     noisy grid matches a CPU quadrature through the baked voxels read back (residual ratio tracking: 1 %
//     on the mean, 2 % per pixel at 8 spp); the surface stays unattenuated (emission 2);
// (b) delta tracking on a secondary segment: a mirror sees an emitter through an absorbing grid slab (and
//     through fog, which also attenuates the primary segment): lighting = Le e^-tau (3 %);
// (c) a constant grid equals homogeneous fog of the same density in a closed room lit by a ceiling light
//     (multiple scattering, NEE through the media): mean L_v within 4 % (independent seeds), T within 3 %;
// (d) an emissive absorbing slab: L_v = Le (1 - e^-sigma d) (3 %);
// (e) a furnace: albedo 1 inside walls emitting 1 gives T + L_v = 1 (3 % on the mean);
// (f) NEE through an absorbing slab onto a diffuse floor equals the slab-free floor times e^-sigma d (3 %);
//     under a big light through a noisy slab, NEE (ratio tracking) equals BSDF sampling alone (delta
//     tracking) (3 %);
// (g) exponential height fog: volume.a equals the closed form (each row's mean, 1 %), for rays that escape
//     upwards and rays that end on a floor;
// (h) two bakes give identical bytes (another time does not), and every majorant cell bounds the densities
//     of its cell and apron, tightly (fp16 rounding up);
// (i) density 1e4 and |g| 0.99: everything finite, T ~ 0, L_v >= 0;
// (j) without a medium, and with an empty MediumFrame, the G-buffer's bytes are identical and the volume
//     layer is (0, 0, 0, 1) exactly (the byte-identity baseline, tests/baseline, covers the show);
// (k) single scattering from a rect light in homogeneous fog (maxBounces 1) equals a numeric double
//     integral over the segment and the light (3 %);
// (l) light shafts: a sun through a slot in the ceiling lights a band of fog that the camera sees as a
//     bright column (3x the edges, and 3x the closed room);
// (m) the modern stage: one medium volume measured by the allocator; an export after another time, and a
//     window frame after a seek, equal a fresh renderer's; a time-varying medium changes the picture;
// (n) refusals: a grid without its volume, a grid larger than the volume, bakes beyond capacity.
// The layout of MediumUniforms is material_test's probe. With --look <dir> [spp] it only renders stills to
// look at (fog shafts, a nebula).

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/paths.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/environment.hpp"
#include "modern/medium.hpp"
#include "modern/medium_volume.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene_gpu.hpp"
#include "post/tonemap_reference.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "rt/gpu_types.hpp"
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
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;
using modern::Camera;
using modern::FogDesc;
using modern::GridMediumDesc;
using modern::MediumFrame;
using modern::TraceSettings;
using modern::Vec3;
using test::TestScene;

constexpr double kPi = std::numbers::pi;
constexpr std::uint32_t kW = 64, kH = 64;

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

bool near(double a, double b, double relative) {
    return std::abs(a - b) <= relative * std::abs(b);
}

std::filesystem::path bakeShader() {
    return std::filesystem::path(EVO_TEST_SPIRV_DIR) / "test_medium_constant.comp.spv";
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

// --- Readback ----------------------------------------------------------------------------------------------

// One image read back as floats, `channels` per texel, plus its raw bytes.
struct Plane {
    std::uint32_t width = 0, height = 0, depth = 1, channels = 4;
    std::vector<std::uint8_t> bytes;
    std::vector<float> values;
    float at(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return values[(std::size_t(y) * width + x) * channels + c];
    }
    float at3(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t c) const {
        return values[((std::size_t(z) * height + y) * width + x) * channels + c];
    }
    double mean(std::uint32_t c) const {
        double sum = 0;
        const std::size_t n = std::size_t(width) * height * depth;
        for (std::size_t i = 0; i < n; ++i)
            sum += values[i * channels + c];
        return sum / double(n);
    }
    // The mean over the pixel rectangle [x0, x1) x [y0, y1).
    double mean(std::uint32_t c, std::uint32_t x0, std::uint32_t x1, std::uint32_t y0,
                std::uint32_t y1) const {
        double sum = 0;
        for (std::uint32_t y = y0; y < y1; ++y)
            for (std::uint32_t x = x0; x < x1; ++x)
                sum += at(x, y, c);
        return sum / double((x1 - x0) * (y1 - y0));
    }
    bool allFinite() const {
        return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
    }
};

struct PendingRead {
    gfx::Buffer buffer;
    VkFormat format;
    std::uint32_t width, height, depth;
};

std::uint32_t channelsOf(VkFormat format) {
    return format == VK_FORMAT_R16_SFLOAT || format == VK_FORMAT_R32_SFLOAT ? 1 : 4;
}
std::uint32_t bytesPerTexel(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8;
    case VK_FORMAT_R32_SFLOAT:
        return 4;
    case VK_FORMAT_R16_SFLOAT:
        return 2;
    default:
        throw std::logic_error("medium_test: no readback for this format");
    }
}

PendingRead recordRead(gfx::Context& ctx, VkCommandBuffer cmd, gfx::Image& image) {
    const VkFormat format = image.format();
    const VkExtent2D extent = image.extent();
    const std::uint32_t depth = image.viewType() == VK_IMAGE_VIEW_TYPE_3D ? image.depth() : 1;
    PendingRead read{
        gfx::Buffer(ctx, {VkDeviceSize(extent.width) * extent.height * depth * bytesPerTexel(format),
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "medium test readback"}),
        format, extent.width, extent.height, depth};
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, depth};
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
    plane.depth = read.depth;
    plane.channels = channelsOf(read.format);
    const auto* bytes = static_cast<const std::uint8_t*>(read.buffer.mapped());
    plane.bytes.assign(bytes, bytes + read.buffer.size());
    const std::size_t floats = std::size_t(read.width) * read.height * read.depth * plane.channels;
    plane.values.resize(floats);
    if (read.format == VK_FORMAT_R16G16B16A16_SFLOAT || read.format == VK_FORMAT_R16_SFLOAT) {
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
    Plane lighting, emission, albedo, volume;
    Plane density, majorant; // when the trace baked a grid and asked for them
    // The picture the denoiser's combine pass would make without filtering: T * surface + L_v, channel c.
    double hdr(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        const double surface = lighting.at(x, y, c) + emission.at(x, y, c);
        return volume.at(x, y, 3) * surface + volume.at(x, y, c);
    }
};

// A path tracer for one scene with its own ring and, for a scene with a grid medium, a MediumVolume of
// `grid` voxels baking the test shader.
class Tracer {
public:
    Tracer(gfx::Context& ctx, const TestScene& scene, std::array<std::uint32_t, 3> grid = {0, 0, 0},
           std::uint32_t width = kW, std::uint32_t height = kH)
        : ctx_(ctx), gpu_(ctx, scene.statics), ring_(ctx),
          volume_(grid[0] > 0 ? std::make_unique<modern::MediumVolume>(ctx, ring_, grid, bakeShader())
                              : nullptr),
          tracer_(ctx, ring_, width, height, modern::tracerCapacity(scene.statics.capacity)) {}

    Frame trace(const modern::SceneFrame& frame, const TraceSettings& settings, double seconds = 0,
                bool readGrid = false) {
        VkCommandBuffer cmd = ring_.begin();
        modern::TraceExtras extras;
        if (frame.medium && frame.medium->grid && volume_) {
            volume_->record(cmd, *frame.medium->grid, seconds);
            extras.density = &volume_->density();
            extras.majorant = &volume_->majorant();
        }
        tracer_.trace(cmd, gpu_, frame, std::nullopt, settings, nullptr, extras);
        modern::GBuffer& g = tracer_.gbuffer();
        const PendingRead lighting = recordRead(ctx_, cmd, g.lighting);
        const PendingRead emission = recordRead(ctx_, cmd, g.emission);
        const PendingRead albedo = recordRead(ctx_, cmd, g.albedo);
        const PendingRead volume = recordRead(ctx_, cmd, g.volume);
        std::optional<PendingRead> density, majorant;
        if (readGrid && volume_) {
            density.emplace(recordRead(ctx_, cmd, volume_->density()));
            majorant.emplace(recordRead(ctx_, cmd, volume_->majorant()));
        }
        ring_.wait(ring_.submit());
        idle(ctx_);
        Frame f{finishRead(lighting), finishRead(emission), finishRead(albedo), finishRead(volume), {}, {}};
        if (density) {
            f.density = finishRead(*density);
            f.majorant = finishRead(*majorant);
        }
        return f;
    }
    // Records a trace without a volume binding (for the refusals).
    void traceBare(const modern::SceneFrame& frame, const TraceSettings& settings, bool bindVolume) {
        VkCommandBuffer cmd = ring_.begin();
        modern::TraceExtras extras;
        if (bindVolume && volume_) {
            extras.density = &volume_->density();
            extras.majorant = &volume_->majorant();
        }
        try {
            tracer_.trace(cmd, gpu_, frame, std::nullopt, settings, nullptr, extras);
        } catch (...) {
            ring_.wait(ring_.submit());
            throw;
        }
        ring_.wait(ring_.submit());
    }
    modern::MediumVolume& volume() { return *volume_; }
    gfx::FrameRing& ring() { return ring_; }

private:
    gfx::Context& ctx_;
    modern::SceneGpu gpu_;
    gfx::FrameRing ring_;
    std::unique_ptr<modern::MediumVolume> volume_;
    modern::PathTracer tracer_;
};

TraceSettings settings(std::uint32_t spp, std::uint32_t maxBounces = 4, std::uint32_t seed = 7) {
    TraceSettings s;
    s.spp = spp;
    s.maxBounces = maxBounces;
    s.frameSeed = seed;
    s.clampIndirect = 1e6f;
    return s;
}

// --- CPU references ----------------------------------------------------------------------------------------

// The direction of the ray through a point of the picture (pixel units), as pathtrace.rgen makes it.
Vec3 pixelRay(const Camera& camera, double px, double py, std::uint32_t width = kW,
              std::uint32_t height = kH) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const double t = std::tan(camera.fovYDegrees * kPi / 360.0);
    const double aspect = double(width) / double(height);
    const double nx = 2.0 * px / width - 1.0, ny = 1.0 - 2.0 * py / height;
    return modern::normalize(b.forward + b.right * float(nx * t * aspect) + b.up * float(ny * t));
}

// The constant (mode 0) and noise (mode 1) voxels of medium_constant.comp.
std::array<float, 16> constantParams(float density, Vec3 emission = {0, 0, 0}) {
    std::array<float, 16> p{};
    p[0] = density;
    p[1] = emission.x;
    p[2] = emission.y;
    p[3] = emission.z;
    return p;
}
std::array<float, 16> noiseParams(float amplitude) {
    std::array<float, 16> p{};
    p[4] = amplitude;
    p[15] = 1;
    return p;
}

// A grid over the box lo..hi of n^3 voxels.
GridMediumDesc gridDesc(Vec3 lo, Vec3 hi, std::uint32_t n, std::array<float, 16> params, Vec3 albedo,
                        float g = 0.f) {
    GridMediumDesc d;
    d.size = {n, n, n};
    d.worldToMedium = modern::mediumBox(lo, hi);
    d.densityScale = 1;
    d.albedo = albedo;
    d.phaseG = g;
    d.emissionScale = 1;
    d.params = params;
    return d;
}

// The trilinear density of a read-back density image at a world point (the sampler's clamp to the baked
// voxels' centres included), times densityScale.
double sampleDensity(const Plane& density, const GridMediumDesc& d, Vec3 p) {
    const Vec3 m = modern::transformPoint(d.worldToMedium, p);
    if (m.x < 0 || m.y < 0 || m.z < 0 || m.x > 1 || m.y > 1 || m.z > 1)
        return 0;
    const double coords[3] = {m.x * d.size[0], m.y * d.size[1], m.z * d.size[2]};
    int i0[3], i1[3];
    double f[3];
    for (int a = 0; a < 3; ++a) {
        const double u = std::clamp(coords[a], 0.5, d.size[a] - 0.5) - 0.5;
        i0[a] = int(std::floor(u));
        f[a] = u - i0[a];
        i1[a] = std::min(i0[a] + 1, int(d.size[a]) - 1);
    }
    double value = 0;
    for (int c = 0; c < 8; ++c) {
        const int x = c & 1 ? i1[0] : i0[0], y = c & 2 ? i1[1] : i0[1], z = c & 4 ? i1[2] : i0[2];
        const double w = (c & 1 ? f[0] : 1 - f[0]) * (c & 2 ? f[1] : 1 - f[1]) * (c & 4 ? f[2] : 1 - f[2]);
        value += w * density.at3(std::uint32_t(x), std::uint32_t(y), std::uint32_t(z), 0);
    }
    return value * d.densityScale;
}

// The grid's optical depth along a ray from 0 to tMax (midpoint rule).
double gridDepth(const Plane& density, const GridMediumDesc& d, Vec3 o, Vec3 dir, double tMax, int steps) {
    double sum = 0;
    const double dt = tMax / steps;
    for (int i = 0; i < steps; ++i)
        sum += sampleDensity(density, d, o + dir * float((i + 0.5) * dt));
    return sum * dt;
}

// --- Scenes ------------------------------------------------------------------------------------------------

modern::EnvironmentDesc blackSky() {
    modern::EnvironmentDesc e;
    e.intensity = 0;
    return e;
}

// A quad of w x h centred at `centre` whose front faces along the rotation's image of +z.
void addQuad(TestScene& s, std::uint32_t material, Vec3 centre, Vec3 axis, float degrees, float w, float h,
             std::optional<Vec3> light = std::nullopt) {
    const rt::Mat34 t = test::place(centre, axis, degrees, {w, h, 1});
    if (light) {
        s.frame.instances.push_back({test::kQuadMesh, material, t, std::uint32_t(s.frame.lights.size())});
        s.frame.lights.push_back(modern::rectLightForQuad(t, 1, 1, rt::LightKind::Emitter, *light, 1.f));
    } else {
        s.frame.instances.push_back({test::kQuadMesh, material, t});
    }
}

// A closed room x, z in [-r, r], y in [0, 2r] of one material, walls facing in; the camera inside.
void addRoom(TestScene& s, std::uint32_t material, float r) {
    const float size = 2 * r;
    addQuad(s, material, {0, 0, 0}, test::kX, -90, size, size);   // floor
    addQuad(s, material, {0, size, 0}, test::kX, 90, size, size); // ceiling
    addQuad(s, material, {0, r, -r}, test::kY, 0, size, size);    // back
    addQuad(s, material, {0, r, r}, test::kY, 180, size, size);   // front
    addQuad(s, material, {-r, r, 0}, test::kY, 90, size, size);   // left
    addQuad(s, material, {r, r, 0}, test::kY, -90, size, size);   // right
}

TestScene baseScene(std::vector<rt::Material> materials) {
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = std::move(materials);
    s.statics.mediumShader = bakeShader();
    s.statics.capacity.mediumGrid = {32, 32, 32};
    s.frame.environment = blackSky();
    return s;
}

// A narrow camera at the origin looking down -z.
Camera narrowCamera() {
    return {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 4.f};
}

// --- (0) ---------------------------------------------------------------------------------------------------

double numericFogDepth(const FogDesc& fog, Vec3 o, Vec3 d, double t0, double t1, int steps) {
    double sum = 0;
    const double dt = (t1 - t0) / steps;
    for (int i = 0; i < steps; ++i) {
        const double y = o.y + d.y * (t0 + (i + 0.5) * dt);
        sum += fog.density * std::exp(-fog.falloff * (y - fog.baseHeight));
    }
    return sum * dt;
}

void testCpu() {
    MediumFrame none;
    CHECK(modern::mediumFlags(none) == 0);
    MediumFrame fogOnly;
    fogOnly.fog = FogDesc{0.5f, 1.f, 0.25f, {0.9f, 0.8f, 0.7f}, 0.4f};
    CHECK(modern::mediumFlags(fogOnly) == modern::kMediumFog);
    MediumFrame both = fogOnly;
    both.grid = gridDesc({-1, 0, -2}, {1, 2, 2}, 37, constantParams(1), {0.5f, 0.6f, 0.7f}, -0.2f);
    both.grid->size = {37, 29, 21};
    both.grid->densityScale = 3;
    both.grid->emissionScale = 2;
    CHECK(modern::mediumFlags(both) == (modern::kMediumFog | modern::kMediumGrid));

    const rt::MediumUniforms u = modern::packMedium(both);
    CHECK(u.fogDensity == 0.5f && u.fogBaseHeight == 1.f && u.fogFalloff == 0.25f && u.fogPhaseG == 0.4f);
    CHECK(u.fogAlbedo[0] == 0.9f && u.fogAlbedo[1] == 0.8f && u.fogAlbedo[2] == 0.7f);
    CHECK(u.gridSize[0] == 37 && u.gridSize[1] == 29 && u.gridSize[2] == 21);
    CHECK(u.majorantSize[0] == 5 && u.majorantSize[1] == 4 && u.majorantSize[2] == 3);
    CHECK(u.densityScale == 3.f && u.emissionScale == 2.f && u.gridPhaseG == -0.2f);
    CHECK(u.gridAlbedo[0] == 0.5f && u.gridAlbedo[1] == 0.6f && u.gridAlbedo[2] == 0.7f);
    CHECK(std::memcmp(u.worldToMedium, both.grid->worldToMedium.m, sizeof u.worldToMedium) == 0);
    const rt::MediumUniforms empty = modern::packMedium(none);
    const rt::MediumUniforms zero{};
    CHECK(std::memcmp(&empty, &zero, sizeof zero) == 0);
    const rt::MediumUniforms fogPacked = modern::packMedium(fogOnly);
    CHECK(fogPacked.gridSize[0] == 0 && fogPacked.densityScale == 0.f && fogPacked.fogDensity == 0.5f);

    CHECK(modern::majorantSize({37, 29, 21}) == (std::array<std::uint32_t, 3>{5, 4, 3}));
    CHECK(modern::majorantSize({8, 16, 1}) == (std::array<std::uint32_t, 3>{1, 2, 1}));
    const rt::Mat34 box = modern::mediumBox({-1, 0, -2}, {1, 2, 2});
    const Vec3 lo = modern::transformPoint(box, {-1, 0, -2}), hi = modern::transformPoint(box, {1, 2, 2}),
               mid = modern::transformPoint(box, {0, 1, 0});
    CHECK(lo.x == 0 && lo.y == 0 && lo.z == 0 && hi.x == 1 && hi.y == 1 && hi.z == 1);
    CHECK(mid.x == 0.5f && mid.y == 0.5f && mid.z == 0.5f);

    // Refusals.
    auto refused = [](auto change) {
        MediumFrame m;
        m.fog = FogDesc{0.5f, 0, 0.1f};
        m.grid = gridDesc({-1, -1, -1}, {1, 1, 1}, 8, constantParams(1), {0.5f, 0.5f, 0.5f});
        change(m);
        return throws<std::invalid_argument>([&] { modern::packMedium(m); });
    };
    CHECK(refused([](MediumFrame& m) { m.fog->density = -1; }));
    CHECK(refused([](MediumFrame& m) { m.fog->falloff = -0.1f; }));
    CHECK(refused([](MediumFrame& m) { m.fog->baseHeight = std::numeric_limits<float>::quiet_NaN(); }));
    CHECK(refused([](MediumFrame& m) { m.fog->albedo.y = 1.5f; }));
    CHECK(refused([](MediumFrame& m) { m.fog->phaseG = 1.f; }));
    CHECK(refused([](MediumFrame& m) { m.grid->phaseG = -1.f; }));
    CHECK(refused([](MediumFrame& m) { m.grid->albedo.x = -0.1f; }));
    CHECK(refused([](MediumFrame& m) { m.grid->densityScale = -1; }));
    CHECK(refused([](MediumFrame& m) { m.grid->emissionScale = std::numeric_limits<float>::infinity(); }));
    CHECK(refused([](MediumFrame& m) { m.grid->size = {8, 0, 8}; }));
    CHECK(refused([](MediumFrame& m) { m.grid->size = {8, modern::kMaxMediumGrid + 1, 8}; }));
    CHECK(refused([](MediumFrame& m) { m.grid->worldToMedium = modern::scale({1, 0, 1}); }));
    CHECK(refused([](MediumFrame& m) { m.grid->params[3] = std::numeric_limits<float>::quiet_NaN(); }));

    // Henyey-Greenstein: integrates to 1 over the sphere, its samples follow it, and its mean cosine is g.
    for (double g : {-0.9, -0.3, 0.0, 0.5, 0.95}) {
        constexpr int kSteps = 400000;
        double integral = 0;
        for (int i = 0; i < kSteps; ++i)
            integral += modern::henyeyGreenstein(g, -1.0 + (i + 0.5) * 2.0 / kSteps);
        integral *= 2 * kPi * 2.0 / kSteps;
        CHECK(std::abs(integral - 1) < 1e-3);
        constexpr int kSamples = 100000;
        std::vector<double> cosines(kSamples);
        std::mt19937 rng(42);
        std::uniform_real_distribution<double> uniform(0, 1);
        double meanCos = 0;
        for (double& c : cosines) {
            c = modern::sampleHenyeyGreenstein(g, uniform(rng));
            CHECK(c >= -1 && c <= 1);
            meanCos += c / kSamples;
        }
        std::sort(cosines.begin(), cosines.end());
        // Kolmogorov-Smirnov against the CDF of the pdf (2 pi HG over the cosine).
        double cdf = 0, worst = 0;
        std::size_t index = 0;
        for (int i = 0; i < kSteps; ++i) {
            const double mu = -1.0 + (i + 1.0) * 2.0 / kSteps;
            cdf += 2 * kPi * modern::henyeyGreenstein(g, mu - 1.0 / kSteps) * 2.0 / kSteps;
            while (index < cosines.size() && cosines[index] <= mu)
                ++index;
            worst = std::max(worst, std::abs(double(index) / kSamples - cdf));
        }
        CHECK(worst < 0.01);
        CHECK(std::abs(meanCos - g) < 0.01);
    }

    // The fog's closed form against a quadrature: rising, falling, level and homogeneous rays.
    const FogDesc fog{0.8f, 0.5f, 1.5f};
    const FogDesc flat{0.3f, 0, 0};
    struct Case {
        FogDesc fog;
        Vec3 o, d;
        double t0, t1;
    };
    const Case cases[] = {{fog, {0, 0.2f, 0}, modern::normalize({1, 0.5f, 0}), 0, 6},
                          {fog, {0, 3, 0}, modern::normalize({0.3f, -1, 0.2f}), 0.5, 2.5},
                          {fog, {1, 0.7f, 0}, {1, 0, 0}, 0, 4},
                          {fog, {0, 0.2f, 0}, modern::normalize({1, 1e-7f, 0}), 0, 3},
                          {flat, {0, 5, 0}, modern::normalize({1, -1, 1}), 1, 9}};
    for (const Case& c : cases) {
        const double closed = modern::fogOpticalDepth(c.fog, c.o, c.d, c.t0, c.t1);
        CHECK(near(closed, numericFogDepth(c.fog, c.o, c.d, c.t0, c.t1, 200000), 1e-5));
    }
    // To infinity: upwards finite (the quadrature to where the rest is negligible), level and downwards
    // infinite, homogeneous infinite.
    const Vec3 up = modern::normalize({0.4f, 1, 0});
    CHECK(near(modern::fogOpticalDepth(fog, {0, 0.2f, 0}, up, 0, std::numeric_limits<double>::infinity()),
               numericFogDepth(fog, {0, 0.2f, 0}, up, 0, 60, 400000), 1e-5));
    CHECK(std::isinf(
        modern::fogOpticalDepth(fog, {0, 0.2f, 0}, {1, 0, 0}, 0, std::numeric_limits<double>::infinity())));
    CHECK(std::isinf(
        modern::fogOpticalDepth(flat, {0, 0.2f, 0}, up, 0, std::numeric_limits<double>::infinity())));
    CHECK(modern::fogOpticalDepth(FogDesc{}, {0, 0.2f, 0}, up, 0, std::numeric_limits<double>::infinity()) ==
          0);

    // checkSceneFrame: a grid needs the scene's medium shader and fits its capacity; the fog must be valid.
    TestScene s = baseScene({test::lambert({0.5f, 0.5f, 0.5f})});
    addQuad(s, 0, {0, 0, -2}, test::kY, 0, 1, 1);
    s.frame.camera = narrowCamera();
    s.frame.medium = both;
    s.frame.medium->grid->size = {32, 16, 8};
    modern::checkSceneFrame(s.statics, s.frame);
    s.frame.medium->grid->size = {33, 16, 8};
    CHECK(throws<std::length_error>([&] { modern::checkSceneFrame(s.statics, s.frame); }));
    s.frame.medium->grid->size = {32, 16, 8};
    s.frame.medium->fog->density = -1;
    CHECK(throws<std::invalid_argument>([&] { modern::checkSceneFrame(s.statics, s.frame); }));
    s.frame.medium->fog->density = 1;
    s.statics.mediumShader.clear();
    CHECK(throws<std::invalid_argument>([&] { modern::checkSceneFrame(s.statics, s.frame); }));
    s.frame.medium->grid.reset();
    modern::checkSceneFrame(s.statics, s.frame); // fog needs no shader
    std::printf("(0) CPU: flags, packing, refusals, HG (integral, sampling, mean cosine), fog depth\n");
}

// --- (a) ---------------------------------------------------------------------------------------------------

void testBeerLambert(gfx::Context& ctx) {
    // An emitter of 2 filling the narrow view, 1 m away, in absorbing homogeneous fog of sigma 1.
    {
        TestScene s = baseScene({test::emitter({2, 2, 2})});
        addQuad(s, 0, {0, 0, -1}, test::kY, 0, 4, 4);
        s.frame.camera = narrowCamera();
        s.frame.medium = MediumFrame{FogDesc{1, 0, 0, {0, 0, 0}, 0}, std::nullopt};
        Tracer tracer(ctx, s);
        const Frame f = tracer.trace(s.frame, settings(4));
        CHECK(f.volume.allFinite());
        double worst = 0;
        for (std::uint32_t y = 0; y < kH; ++y)
            for (std::uint32_t x = 0; x < kW; ++x) {
                const Vec3 d = pixelRay(s.frame.camera, x + 0.5, y + 0.5);
                const double expected = std::exp(-1.0 / -d.z);
                worst = std::max(worst, std::abs(f.volume.at(x, y, 3) - expected) / expected);
                CHECK(f.volume.at(x, y, 0) == 0.f); // nothing scatters
            }
        CHECK(worst < 2e-3);
        CHECK(near(f.volume.mean(3), std::exp(-1.0), 0.02));
        CHECK(near(f.emission.mean(0), 2.0, 1e-3)); // the surface is unattenuated: T multiplies it later
        std::printf("(a) fog: volume.a = e^-1/cos per pixel (worst %.2e)\n", worst);
    }
    // A constant grid slab z in [-1.5, -0.5] of sigma 1 in front of an emitter at 2 m.
    TestScene s = baseScene({test::emitter({2, 2, 2})});
    addQuad(s, 0, {0, 0, -2}, test::kY, 0, 4, 4);
    s.frame.camera = narrowCamera();
    MediumFrame m;
    m.grid = gridDesc({-5, -5, -1.5f}, {5, 5, -0.5f}, 16, constantParams(1), {0, 0, 0});
    s.frame.medium = m;
    Tracer tracer(ctx, s, {32, 32, 32});
    const Frame f = tracer.trace(s.frame, settings(4));
    CHECK(f.volume.allFinite());
    std::printf("(a) constant slab: mean T %.5f (e^-1 = %.5f)\n", f.volume.mean(3), std::exp(-1.0));
    CHECK(near(f.volume.mean(3), std::exp(-1.0), 0.02));
    CHECK(f.volume.mean(0) == 0.0);

    // A noisy grid (residual ratio tracking against a varying density) against a quadrature through the
    // baked voxels.
    m.grid = gridDesc({-5, -5, -1.5f}, {5, 5, -0.5f}, 24, noiseParams(1.5f), {0, 0, 0});
    m.grid->size = {24, 20, 28};
    s.frame.medium = m;
    const Frame n = tracer.trace(s.frame, settings(8), 0.7, true);
    double sumGpu = 0, sumCpu = 0, worst = 0;
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const Vec3 d = pixelRay(s.frame.camera, x + 0.5, y + 0.5);
            const double expected = std::exp(-gridDepth(n.density, *m.grid, {0, 0, 0}, d, 2.0 / -d.z, 2000));
            const double got = n.volume.at(x, y, 3);
            sumGpu += got;
            sumCpu += expected;
            worst = std::max(worst, std::abs(got - expected) / expected);
        }
    std::printf("(a) noisy grid: mean T %.5f vs quadrature %.5f, worst pixel %.3f\n", sumGpu / (kW * kH),
                sumCpu / (kW * kH), worst);
    CHECK(near(sumGpu, sumCpu, 0.01));
    CHECK(worst < 0.02);
}

// --- (b) ---------------------------------------------------------------------------------------------------

void testDeltaTracking(gfx::Context& ctx) {
    // A mirror at 1 m turns the view to +x, onto an emitter of 2 at x = 3; an absorbing slab x in [1, 2].
    TestScene s = baseScene({test::metal({1, 1, 1}, 0.f), test::emitter({2, 2, 2})});
    addQuad(s, 0, {0, 0, -1}, test::kY, 45, 1, 1);
    addQuad(s, 1, {3, 0, -1}, test::kY, -90, 6, 6);
    s.frame.camera = narrowCamera();
    MediumFrame m;
    m.grid = gridDesc({1, -5, -6}, {2, 5, 4}, 16, constantParams(1), {0, 0, 0});
    s.frame.medium = m;
    Tracer tracer(ctx, s, {32, 32, 32});
    const Frame f = tracer.trace(s.frame, settings(32));
    const double expected = 2 * std::exp(-1.0);
    std::printf("(b) grid slab after a mirror: lighting %.4f, expected %.4f\n", f.lighting.mean(0), expected);
    CHECK(near(f.lighting.mean(0), expected, 0.03));
    CHECK(near(f.volume.mean(3), 1.0, 1e-6)); // the primary segment misses the slab

    // Absorbing fog of 0.5 everywhere: the primary segment (1 m) in T, the 3 m after the mirror in lighting.
    s.frame.medium = MediumFrame{FogDesc{0.5f, 0, 0, {0, 0, 0}, 0}, std::nullopt};
    const Frame g = tracer.trace(s.frame, settings(32));
    std::printf("(b) fog after a mirror: lighting %.4f, expected %.4f; T %.4f\n", g.lighting.mean(0),
                2 * std::exp(-1.5), g.volume.mean(3));
    CHECK(near(g.lighting.mean(0), 2 * std::exp(-1.5), 0.03));
    CHECK(near(g.volume.mean(3), std::exp(-0.5), 0.005));
}

// --- (c) ---------------------------------------------------------------------------------------------------

TestScene litRoom() {
    TestScene s = baseScene({test::lambert({0, 0, 0}), test::emitter({12, 12, 12})});
    addRoom(s, 0, 1);
    addQuad(s, 1, {0, 1.995f, 0}, test::kX, 90, 0.5f, 0.5f, Vec3{12, 12, 12});
    s.frame.camera = {{0, 1, 0.95f}, {0, 1, -1}, {0, 1, 0}, 70.f};
    return s;
}

void testGridEqualsFog(gfx::Context& ctx) {
    TestScene s = litRoom();
    Tracer tracer(ctx, s, {32, 32, 32});
    s.frame.medium = MediumFrame{FogDesc{0.6f, 0, 0, {0.8f, 0.8f, 0.8f}, 0.3f}, std::nullopt};
    const Frame fog = tracer.trace(s.frame, settings(32));
    MediumFrame m;
    m.grid = gridDesc({-1, 0, -1}, {1, 2, 1}, 16, constantParams(0.6f), {0.8f, 0.8f, 0.8f}, 0.3f);
    s.frame.medium = m;
    const Frame grid = tracer.trace(s.frame, settings(32, 4, 8));
    CHECK(fog.volume.allFinite() && grid.volume.allFinite());
    std::printf("(c) fog vs constant grid: L_v %.5f vs %.5f, T %.4f vs %.4f\n", fog.volume.mean(1),
                grid.volume.mean(1), fog.volume.mean(3), grid.volume.mean(3));
    CHECK(fog.volume.mean(1) > 0.05);
    CHECK(near(grid.volume.mean(1), fog.volume.mean(1), 0.04)); // each about 1 % noise at 32 spp
    CHECK(near(grid.volume.mean(3), fog.volume.mean(3), 0.03));
}

// --- (d) ---------------------------------------------------------------------------------------------------

void testEmissiveSlab(gfx::Context& ctx) {
    TestScene s = baseScene({test::lambert({0, 0, 0})});
    addQuad(s, 0, {0, 0, -3}, test::kY, 0, 4, 4);
    s.frame.camera = narrowCamera();
    MediumFrame m;
    m.grid = gridDesc({-5, -5, -1.5f}, {5, 5, -0.5f}, 16, constantParams(1, {1, 0.5f, 0.25f}), {0, 0, 0});
    s.frame.medium = m;
    Tracer tracer(ctx, s, {32, 32, 32});
    const Frame f = tracer.trace(s.frame, settings(8));
    const double k = 1 - std::exp(-1.0);
    std::printf("(d) emissive slab: L_v %.4f %.4f %.4f, expected %.4f %.4f %.4f\n", f.volume.mean(0),
                f.volume.mean(1), f.volume.mean(2), k, 0.5 * k, 0.25 * k);
    CHECK(near(f.volume.mean(0), k, 0.03) && near(f.volume.mean(1), 0.5 * k, 0.03) &&
          near(f.volume.mean(2), 0.25 * k, 0.03));
    CHECK(near(f.volume.mean(3), std::exp(-1.0), 0.02));
}

// --- (e) ---------------------------------------------------------------------------------------------------

void testFurnace(gfx::Context& ctx) {
    TestScene s = baseScene({test::emitter({1, 1, 1})});
    addRoom(s, 0, 1);
    s.frame.camera = {{0.1f, 1, 0.5f}, {0, 1, -1}, {0, 1, 0}, 70.f};
    MediumFrame m;
    m.grid = gridDesc({-1, 0, -1}, {1, 2, 1}, 16, constantParams(1), {1, 1, 1}, 0.3f);
    s.frame.medium = m;
    Tracer tracer(ctx, s, {32, 32, 32});
    const Frame f = tracer.trace(s.frame, settings(8, 64));
    double sum = 0, worst = 0;
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double v = f.hdr(x, y, 1);
            sum += v;
            worst = std::max(worst, std::abs(v - 1));
        }
    std::printf("(e) furnace: mean %.4f (T %.3f), worst pixel off by %.3f\n", sum / (kW * kH),
                f.volume.mean(3), worst);
    CHECK(f.volume.mean(3) < 0.6); // the medium matters
    CHECK(near(sum / (kW * kH), 1.0, 0.03));
}

// --- (f) ---------------------------------------------------------------------------------------------------

void testNeeThroughSlab(gfx::Context& ctx) {
    // A small light 4 m above a diffuse floor; an absorbing slab of sigma 1, 0.5 thick, at y in [2, 2.5]; the
    // camera below the slab.
    TestScene s = baseScene({test::lambert({0.5f, 0.5f, 0.5f}), test::emitter({40, 40, 40})});
    addQuad(s, 0, {0, 0, 0}, test::kX, -90, 20, 20);
    addQuad(s, 1, {0, 4, 0}, test::kX, 90, 0.4f, 0.4f, Vec3{40, 40, 40});
    s.frame.camera = {{0, 1, 0.6f}, {0, 0, 0}, {0, 1, 0}, 20.f};
    Tracer tracer(ctx, s, {32, 32, 32});
    const Frame open = tracer.trace(s.frame, settings(8, 1));
    MediumFrame m;
    m.grid = gridDesc({-5, 2, -5}, {5, 2.5f, 5}, 16, constantParams(1), {0, 0, 0});
    s.frame.medium = m;
    const Frame slab = tracer.trace(s.frame, settings(8, 1));
    const double ratio = slab.lighting.mean(1) / open.lighting.mean(1);
    std::printf("(f) NEE through a slab: %.5f / %.5f = %.4f, e^-0.5 = %.4f\n", slab.lighting.mean(1),
                open.lighting.mean(1), ratio, std::exp(-0.5));
    CHECK(open.lighting.mean(1) > 0.01);
    CHECK(near(ratio, std::exp(-0.5), 0.03));

    // A big light through a noisy slab: NEE with ratio-tracked shadow rays equals BSDF sampling alone, whose
    // rays cross the slab by delta tracking.
    TestScene big = baseScene({test::lambert({0.5f, 0.5f, 0.5f}), test::emitter({5, 5, 5})});
    addQuad(big, 0, {0, 0, 0}, test::kX, -90, 20, 20);
    addQuad(big, 1, {0, 4, 0}, test::kX, 90, 3, 3, Vec3{5, 5, 5});
    big.frame.camera = {{0, 1, 0.6f}, {0, 0, 0}, {0, 1, 0}, 20.f};
    m.grid = gridDesc({-5, 2, -5}, {5, 2.5f, 5}, 24, noiseParams(1.5f), {0, 0, 0});
    big.frame.medium = m;
    Tracer bigTracer(ctx, big, {32, 32, 32});
    TraceSettings neeOn = settings(16, 1), neeOff = settings(32, 1, 99);
    neeOff.nee = false;
    const Frame withNee = bigTracer.trace(big.frame, neeOn, 0.3);
    const Frame bsdfOnly = bigTracer.trace(big.frame, neeOff, 0.3);
    std::printf("(f) noisy slab, big light: NEE %.5f, BSDF only %.5f\n", withNee.lighting.mean(1),
                bsdfOnly.lighting.mean(1));
    CHECK(near(bsdfOnly.lighting.mean(1), withNee.lighting.mean(1), 0.03));
}

// --- (g) ---------------------------------------------------------------------------------------------------

void testExponentialFog(gfx::Context& ctx) {
    const FogDesc fog{0.8f, 0.5f, 1.5f, {0, 0, 0}, 0};
    TestScene s = baseScene({test::lambert({0.5f, 0.5f, 0.5f})});
    addQuad(s, 0, {0, 0, 0}, test::kX, -90, 200, 200);
    s.frame.medium = MediumFrame{fog, std::nullopt};
    Tracer tracer(ctx, s);
    const double inf = std::numeric_limits<double>::infinity();
    double worstUp = 0, worstDown = 0;
    // Pitched up: every ray escapes. Pitched down: every ray ends on the floor y = 0. Each row's mean against
    // the closed form averaged over 4 x 4 points in each of its pixels (T changes fast across a pixel near
    // the horizon, so single jittered samples scatter around the pixel centre's value).
    for (int pitch : {1, -1}) {
        const Vec3 eye{0, 0.3f, 0};
        s.frame.camera = {eye, eye + Vec3{0, pitch * 0.6f, -1}, {0, 1, 0}, 30.f};
        const Frame f = tracer.trace(s.frame, settings(4, 1));
        for (std::uint32_t y = 0; y < kH; ++y) {
            double got = 0, expected = 0;
            for (std::uint32_t x = 0; x < kW; ++x) {
                got += f.volume.at(x, y, 3);
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) {
                        const Vec3 d = pixelRay(s.frame.camera, x + (sx + 0.5) / 4, y + (sy + 0.5) / 4);
                        const double tEnd = pitch > 0 ? inf : -eye.y / d.y;
                        expected += std::exp(-modern::fogOpticalDepth(fog, eye, d, 0, tEnd)) / 16;
                    }
            }
            const double error = std::abs(got - expected) / expected;
            (pitch > 0 ? worstUp : worstDown) = std::max(pitch > 0 ? worstUp : worstDown, error);
        }
    }
    std::printf("(g) exponential fog: worst row %.4f (up), %.4f (down)\n", worstUp, worstDown);
    CHECK(worstUp < 0.01 && worstDown < 0.01);
}

// --- (h) ---------------------------------------------------------------------------------------------------

void testBake(gfx::Context& ctx) {
    TestScene s = baseScene({test::lambert({0.5f, 0.5f, 0.5f})});
    addQuad(s, 0, {0, 0, -3}, test::kY, 0, 4, 4);
    s.frame.camera = narrowCamera();
    MediumFrame m;
    m.grid = gridDesc({-1, -1, -2}, {1, 1, -1}, 8, noiseParams(2.f), {0.5f, 0.5f, 0.5f});
    m.grid->size = {37, 29, 21};
    s.frame.medium = m;
    s.statics.capacity.mediumGrid = {40, 32, 24};
    Tracer tracer(ctx, s, {40, 32, 24});
    const Frame a = tracer.trace(s.frame, settings(1), 1.25, true);
    const Frame b = tracer.trace(s.frame, settings(1), 1.25, true);
    const Frame c = tracer.trace(s.frame, settings(1), 2.0, true);
    CHECK(a.density.bytes == b.density.bytes && a.majorant.bytes == b.majorant.bytes);
    CHECK(a.volume.bytes == b.volume.bytes && a.lighting.bytes == b.lighting.bytes);
    CHECK(a.density.bytes != c.density.bytes);
    CHECK(tracer.volume().bakes() == 3);
    const std::array<std::uint32_t, 3> size = m.grid->size, cells = modern::majorantSize(size);
    CHECK(a.majorant.width == 5 && a.majorant.height == 4 && a.majorant.depth == 3);
    std::uint32_t checked = 0;
    double loosest = 0;
    for (std::uint32_t cz = 0; cz < cells[2]; ++cz)
        for (std::uint32_t cy = 0; cy < cells[1]; ++cy)
            for (std::uint32_t cx = 0; cx < cells[0]; ++cx) {
                const std::uint32_t cell[3] = {cx, cy, cz};
                std::uint32_t lo[3], hi[3];
                for (int axis = 0; axis < 3; ++axis) {
                    lo[axis] = cell[axis] * 8 == 0 ? 0 : cell[axis] * 8 - 1;
                    hi[axis] = std::min(cell[axis] * 8 + 8, size[axis] - 1);
                }
                float most = 0;
                for (std::uint32_t z = lo[2]; z <= hi[2]; ++z)
                    for (std::uint32_t y = lo[1]; y <= hi[1]; ++y)
                        for (std::uint32_t x = lo[0]; x <= hi[0]; ++x)
                            most = std::max(most, a.density.at3(x, y, z, 0));
                const float majorant = a.majorant.at3(cx, cy, cz, 0);
                CHECK(majorant >= most);
                CHECK(majorant <= most * (1 + 1.0 / 512));
                loosest = std::max(loosest, double(majorant) / most - 1);
                ++checked;
            }
    // The voxels: the noise at their centres (fp16).
    for (std::uint32_t z = 0; z < size[2]; z += 5)
        for (std::uint32_t y = 0; y < size[1]; y += 4)
            for (std::uint32_t x = 0; x < size[0]; x += 3) {
                const double u = (x + 0.5) / size[0], v = (y + 0.5) / size[1], w = (z + 0.5) / size[2];
                const double expected =
                    2.0 * (0.55 + 0.45 * std::sin(6 * u + 2 * 1.25) * std::cos(5 * v) * std::sin(4 * w + 1));
                CHECK(std::abs(a.density.at3(x, y, z, 0) - expected) < 2e-3 * expected + 1e-3);
            }
    std::printf(
        "(h) bakes: identical bytes twice, another time differs; %u majorant cells bound their voxels "
        "(at most %.2e above)\n",
        checked, loosest);
}

// --- (i) ---------------------------------------------------------------------------------------------------

void testExtremes(gfx::Context& ctx) {
    TestScene s = litRoom();
    Tracer tracer(ctx, s, {32, 32, 32});
    MediumFrame m;
    m.grid = gridDesc({-1, 0, -1}, {1, 2, 1}, 16, constantParams(1, {1, 1, 1}), {0.8f, 0.8f, 0.8f}, 0.99f);
    m.grid->densityScale = 1e4f;
    m.fog = FogDesc{1e4f, 0, 0, {0.9f, 0.9f, 0.9f}, -0.99f};
    for (int variant = 0; variant < 3; ++variant) {
        MediumFrame v = m;
        if (variant == 1)
            v.fog.reset();
        if (variant == 2)
            v.grid.reset();
        s.frame.medium = v;
        const Frame f = tracer.trace(s.frame, settings(2));
        CHECK(f.volume.allFinite() && f.lighting.allFinite() && f.emission.allFinite());
        CHECK(f.volume.mean(3) < 1e-3);
        for (float value : f.volume.values)
            CHECK(value >= 0.f);
    }
    // The camera outside a dense ball, looking at it (thin edges, a black core).
    TestScene outside = baseScene({test::lambert({0.5f, 0.5f, 0.5f}), test::emitter({4, 4, 4})});
    addQuad(outside, 0, {0, 0, -4}, test::kY, 0, 8, 8);
    addQuad(outside, 1, {0, 2, -2}, test::kX, 90, 1, 1, Vec3{4, 4, 4});
    outside.frame.camera = {{0, 0, 1}, {0, 0, -2}, {0, 1, 0}, 50.f};
    MediumFrame ball;
    ball.grid = gridDesc({-1, -1, -3}, {1, 1, -1}, 16, {}, {0.9f, 0.9f, 0.9f}, 0.5f);
    ball.grid->params[8] = 1e4f; // mode 2: the ball's density
    ball.grid->params[9] = 0.45f;
    ball.grid->params[15] = 2;
    outside.frame.medium = ball;
    Tracer ballTracer(ctx, outside, {32, 32, 32});
    const Frame f = ballTracer.trace(outside.frame, settings(2));
    CHECK(f.volume.allFinite() && f.lighting.allFinite());
    CHECK(f.volume.at(kW / 2, kH / 2, 3) < 1e-3 && f.volume.at(1, 1, 3) > 0.99f);
    std::printf("(i) density 1e4, |g| 0.99: finite, T ~ 0 inside, L_v >= 0\n");
}

// --- (j) ---------------------------------------------------------------------------------------------------

void testNoMedium(gfx::Context& ctx) {
    TestScene s = test::cornellBox();
    s.statics.capacity.mediumGrid = {8, 8, 8};
    s.statics.mediumShader = bakeShader();
    Tracer tracer(ctx, s, {8, 8, 8});
    const Frame none = tracer.trace(s.frame, settings(5));
    s.frame.medium = MediumFrame{};
    const Frame empty = tracer.trace(s.frame, settings(5));
    CHECK(none.lighting.bytes == empty.lighting.bytes && none.emission.bytes == empty.emission.bytes &&
          none.albedo.bytes == empty.albedo.bytes && none.volume.bytes == empty.volume.bytes);
    for (std::size_t i = 0; i < none.volume.values.size(); ++i)
        CHECK(none.volume.values[i] == (i % 4 == 3 ? 1.f : 0.f));
    std::printf("(j) no medium and an empty MediumFrame: the same bytes, volume (0, 0, 0, 1)\n");
}

// --- (k) ---------------------------------------------------------------------------------------------------

// Single scattering in homogeneous fog: the camera ray to the back wall at z = -3, the light a w x w quad at
// height ly centred at (0, ly, lz), emitting le downwards.
double singleScatter(const FogDesc& fog, Vec3 d, double tWall, double ly, double lz, double w, double le) {
    constexpr int kT = 300, kA = 24;
    const double sigma = fog.density, sigmaS = fog.density * fog.albedo.y;
    double sum = 0;
    const double dt = tWall / kT, da = w / kA;
    for (int i = 0; i < kT; ++i) {
        const double t = (i + 0.5) * dt;
        const Vec3 x = d * float(t);
        double inner = 0;
        for (int a = 0; a < kA; ++a)
            for (int b = 0; b < kA; ++b) {
                const Vec3 q{float(-w / 2 + (a + 0.5) * da), float(ly), float(lz - w / 2 + (b + 0.5) * da)};
                const Vec3 toLight = q - x;
                const double r = modern::length(toLight);
                const Vec3 omega = toLight / float(r);
                const double cosLight = omega.y; // the light faces down
                if (cosLight <= 0)
                    continue;
                inner += modern::henyeyGreenstein(fog.phaseG, modern::dot(d, omega)) * le *
                         std::exp(-sigma * r) * cosLight / (r * r);
            }
        sum += std::exp(-sigma * t) * sigmaS * inner * da * da;
    }
    return sum * dt;
}

void testSingleScatter(gfx::Context& ctx) {
    const float ly = 0.8f, lz = -1.5f, w = 0.6f, le = 20;
    const FogDesc fog{0.4f, 0, 0, {0.8f, 0.8f, 0.8f}, 0.3f};
    TestScene s = baseScene({test::lambert({0, 0, 0}), test::emitter({le, le, le})});
    addQuad(s, 0, {0, 0, -3}, test::kY, 0, 20, 20);
    addQuad(s, 1, {0, ly, lz}, test::kX, 90, w, w, Vec3{le, le, le});
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 30.f};
    s.frame.medium = MediumFrame{fog, std::nullopt};
    Tracer tracer(ctx, s);
    const Frame f = tracer.trace(s.frame, settings(16, 1));
    double gpu = 0, cpu = 0;
    for (std::uint32_t y = 20; y < 44; y += 3)
        for (std::uint32_t x = 2; x < kW; x += 5) {
            const Vec3 d = pixelRay(s.frame.camera, x + 0.5, y + 0.5);
            gpu += f.volume.at(x, y, 1);
            cpu += singleScatter(fog, d, 3.0 / -d.z, ly, lz, w, le);
        }
    std::printf("(k) single scattering from a rect light: GPU %.5f, integral %.5f\n", gpu, cpu);
    CHECK(cpu > 0.5);
    CHECK(near(gpu, cpu, 0.03));
}

// --- (l) ---------------------------------------------------------------------------------------------------

// A 4 x 2.5 x 4 room with a 0.5 m slot along the ceiling (unless closed), fog inside, a sun above.
TestScene shaftRoom(bool open, float fogDensity = 0.15f) {
    TestScene s = baseScene({test::lambert({0.5f, 0.5f, 0.5f})});
    const float r = 2, h = 2.5f;
    addQuad(s, 0, {0, 0, 0}, test::kX, -90, 2 * r, 2 * r);
    addQuad(s, 0, {0, h / 2, -r}, test::kY, 0, 2 * r, h);
    addQuad(s, 0, {0, h / 2, r}, test::kY, 180, 2 * r, h);
    addQuad(s, 0, {-r, h / 2, 0}, test::kY, 90, 2 * r, h);
    addQuad(s, 0, {r, h / 2, 0}, test::kY, -90, 2 * r, h);
    if (open) {
        addQuad(s, 0, {-1.125f, h, 0}, test::kX, 90, 1.75f, 2 * r);
        addQuad(s, 0, {1.125f, h, 0}, test::kX, 90, 1.75f, 2 * r);
    } else {
        addQuad(s, 0, {0, h, 0}, test::kX, 90, 2 * r, 2 * r);
    }
    modern::EnvironmentDesc sky = modern::environmentDefaults(modern::EnvironmentKind::Day);
    sky.discDirection = modern::normalize({0, 1, 0.3f});
    sky.discAngularRadius = 0.02f;
    sky.discRadiance = {3000, 2800, 2500};
    sky.select = 1;
    s.frame.environment = sky;
    s.frame.camera = {{0, 1.2f, 1.8f}, {0, 1.2f, -2}, {0, 1, 0}, 60.f};
    // Height fog: homogeneous fog fills all space, so no light from the sky would get through it.
    s.frame.medium = MediumFrame{FogDesc{fogDensity, 0, 0.4f, {0.9f, 0.9f, 0.9f}, 0.6f}, std::nullopt};
    return s;
}

void testShafts(gfx::Context& ctx) {
    TestScene open = shaftRoom(true), closed = shaftRoom(false);
    Tracer openTracer(ctx, open), closedTracer(ctx, closed);
    const Frame a = openTracer.trace(open.frame, settings(8));
    const Frame b = closedTracer.trace(closed.frame, settings(8));
    CHECK(a.volume.allFinite() && b.volume.allFinite());
    const double centre = a.volume.mean(1, 28, 36, 16, 48);
    const double edges = 0.5 * (a.volume.mean(1, 0, 8, 16, 48) + a.volume.mean(1, 56, 64, 16, 48));
    const double shut = b.volume.mean(1, 28, 36, 16, 48);
    std::printf("(l) shafts: L_v in the beam %.4f, beside it %.4f, closed room %.4f\n", centre, edges, shut);
    CHECK(centre > 3 * edges);
    CHECK(centre > 3 * shut);
}

// --- (m) ---------------------------------------------------------------------------------------------------

constexpr std::uint32_t kStageW = 64, kStageH = 36;

// A lit room with fog and a noisy grid whose voxels follow the chapter-local time.
class MediumModernScene final : public modern::ModernScene {
public:
    MediumModernScene() : scene_(litRoom()) {
        scene_.statics.capacity.mediumGrid = {16, 16, 16};
        scene_.frame.camera = {{0, 1, 0.95f}, {0, 1, -1}, {0, 1, 0}, 60.f};
    }
    const modern::SceneStatic& staticScene() const override { return scene_.statics; }
    modern::SceneFrame frame(const show::TimelineFrame& t) const override {
        modern::SceneFrame f = scene_.frame;
        MediumFrame m;
        m.fog = FogDesc{0.1f, 0, 0.5f, {0.9f, 0.9f, 0.9f}, 0.2f};
        m.grid = gridDesc({-1, 0, -1}, {1, 2, 1}, 16, noiseParams(1.2f), {0.8f, 0.8f, 0.8f}, 0.4f);
        m.grid->size = {16, 12, 16};
        (void)t;
        f.medium = m;
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
                return std::make_unique<MediumModernScene>();
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
    const show::TimelineFrame b = forced2026(start + 0.6 * (end - start));
    {
        StageHarness h(ctx, render::RenderMode::Export);
        render::ModernStage& stage = h.renderer().modern();
        CHECK(stage.medium() == nullptr);
        const VkDeviceSize stageImages = stage.hdr().bytes() + stage.image().bytes();
        const VkDeviceSize before = allocatedBytes(ctx);
        stage.prepare(modern::SceneId::Ship);
        CHECK(stage.medium() != nullptr &&
              stage.medium()->capacity() == (std::array<std::uint32_t, 3>{16, 16, 16}));
        CHECK(allocatedBytes(ctx) - before == stage.gpuBytes() - stageImages);
        CHECK(stage.gpuBytes() == stage.imageBytes() + stage.tracer()->bufferBytes() +
                                      stage.sceneGpu(modern::SceneId::Ship)->bytes() +
                                      stage.medium()->bytes());
    }
    idle(ctx);
    std::vector<std::uint8_t> exportA, exportB, windowSeek;
    {
        StageHarness h(ctx, render::RenderMode::Export);
        exportA = h.render(a);
        exportB = h.render(b);
        CHECK(h.renderer().modern().medium()->bakes() == 2);
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
    std::printf("(m) modern stage: one medium volume measured by the allocator; an export after another time "
                "and a window frame after a seek equal a fresh renderer's\n");
}

// --- (n) ---------------------------------------------------------------------------------------------------

void testRefusals(gfx::Context& ctx) {
    TestScene s = baseScene({test::lambert({0.5f, 0.5f, 0.5f})});
    addQuad(s, 0, {0, 0, -3}, test::kY, 0, 4, 4);
    s.frame.camera = narrowCamera();
    MediumFrame m;
    m.grid = gridDesc({-1, -1, -2}, {1, 1, -1}, 16, constantParams(1), {0.5f, 0.5f, 0.5f});
    s.frame.medium = m;
    Tracer tracer(ctx, s, {16, 16, 16});
    CHECK(throws<std::invalid_argument>([&] { tracer.traceBare(s.frame, settings(1), false); }));
    s.frame.medium->grid->size = {16, 17, 16};
    s.statics.capacity.mediumGrid = {32, 32, 32};
    CHECK(throws<std::length_error>([&] { tracer.traceBare(s.frame, settings(1), true); }));
    gfx::FrameRing& ring = tracer.ring();
    VkCommandBuffer cmd = ring.begin();
    CHECK(throws<std::length_error>([&] { tracer.volume().record(cmd, *s.frame.medium->grid, 0); }));
    GridMediumDesc zero = *m.grid;
    zero.size = {0, 4, 4};
    CHECK(throws<std::invalid_argument>([&] { tracer.volume().record(cmd, zero, 0); }));
    ring.wait(ring.submit());
    CHECK(throws<std::logic_error>([&] { tracer.volume().record(cmd, *m.grid, 0); }));
    CHECK(throws<std::invalid_argument>([&] { modern::MediumVolume v(ctx, ring, {0, 8, 8}, bakeShader()); }));
    idle(ctx);
    std::printf("(n) refusals\n");
}

// --- Look --------------------------------------------------------------------------------------------------

std::uint8_t encode(float linear) {
    const float v = post::srgbOetf(linear / (1.f + linear)); // Reinhard, for looking only
    return static_cast<std::uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
}

void writeLook(const Frame& f, std::uint32_t width, std::uint32_t height, float exposure,
               const std::filesystem::path& path) {
    std::vector<std::uint8_t> rgb(std::size_t(width) * height * 3);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
            for (std::uint32_t c = 0; c < 3; ++c)
                rgb[(std::size_t(y) * width + x) * 3 + c] = encode(float(f.hdr(x, y, c)) * exposure);
    image::writePng(path, int(width), int(height), rgb.data());
    std::printf("wrote %s\n", gfx::pathToUtf8(path).c_str());
}

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp) {
    std::filesystem::create_directories(dir);
    constexpr std::uint32_t w = 320, h = 180;
    {
        TestScene s = shaftRoom(true, 0.12f);
        s.frame.camera = {{-1.6f, 0.9f, 1.8f}, {0.4f, 1.3f, -1.5f}, {0, 1, 0}, 70.f};
        Tracer tracer(ctx, s, {0, 0, 0}, w, h);
        writeLook(tracer.trace(s.frame, settings(spp, 6)), w, h, 0.6f, dir / "medium_shafts.png");
    }
    {
        TestScene s = baseScene({test::lambert({0.4f, 0.4f, 0.45f}), test::emitter({6, 5, 4})});
        addQuad(s, 0, {0, -1.2f, 0}, test::kX, -90, 20, 20);
        addQuad(s, 1, {1.5f, 2.5f, -1}, test::kX, 90, 1, 1, Vec3{6, 5, 4});
        s.frame.environment = modern::environmentDefaults(modern::EnvironmentKind::Space);
        s.frame.camera = {{0, 0.3f, 3.2f}, {0, 0, 0}, {0, 1, 0}, 45.f};
        MediumFrame m;
        m.grid = gridDesc({-1.5f, -1.2f, -1.5f}, {1.5f, 1.8f, 1.5f}, 32, {}, {0.85f, 0.8f, 0.9f}, 0.4f);
        m.grid->params[1] = 2.f; // emission colour
        m.grid->params[2] = 0.6f;
        m.grid->params[3] = 1.4f;
        m.grid->params[8] = 6.f; // the ball's density and radius
        m.grid->params[9] = 0.48f;
        m.grid->params[15] = 2;
        m.grid->emissionScale = 3;
        s.frame.medium = m;
        Tracer tracer(ctx, s, {32, 32, 32}, w, h);
        writeLook(tracer.trace(s.frame, settings(spp, 6), 0.5), w, h, 1.f, dir / "medium_nebula.png");
    }
}

} // namespace

// medium_test [cases]: runs the cases named by their letters ("0abcdefghijklmn", the default: all).
// medium_test --look <dir> [spp]: only writes the stills of renderLook.
int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
    const bool look = argc >= 3 && std::string(argv[1]) == "--look";
    const std::string cases = look ? "" : argc >= 2 ? argv[1] : "0abcdefghijklmn";
    auto runs = [&](char c) { return cases.find(c) != std::string::npos; };
    try {
        if (runs('0'))
            testCpu();
        auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the results are checked\n");
        else if (!ctx.syncValidationEnabled())
            std::printf("warning: synchronization validation unavailable\n");
        if (look)
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 64u);
        const std::pair<char, void (*)(gfx::Context&)> gpuCases[] = {
            {'a', testBeerLambert},    {'b', testDeltaTracking}, {'c', testGridEqualsFog},
            {'d', testEmissiveSlab},   {'e', testFurnace},       {'f', testNeeThroughSlab},
            {'g', testExponentialFog}, {'h', testBake},          {'i', testExtremes},
            {'j', testNoMedium},       {'k', testSingleScatter}, {'l', testShafts},
            {'m', testStage},          {'n', testRefusals}};
        for (const auto& [letter, run] : gpuCases)
            if (runs(letter)) {
                const auto caseStart = std::chrono::steady_clock::now();
                run(ctx);
                std::printf("  (%c) %.1f s\n", letter, secondsSince(caseStart));
            }
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("medium_test: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
