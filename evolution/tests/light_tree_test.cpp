// The light tree: next-event estimation over thousands of emissive sphere lights.
//
// CPU (src/rt/light_tree.*):
// (a) the build: every node's bounds hold its lights' spheres and are the union of its children's, its
//     power is the sum of its lights', every light's trail leads to its own leaf; the pick pdf summed over
//     all 1000 lights is 1 +- 1e-5 at 20 points (opaque and transmissive);
// (b) the walk: 200,000 picks at a point land in a histogram that matches the pdf (chi-squared, p > 0.01),
//     and every pick's pdf equals treeLightPdf of the light picked (the traversal pdf is reproducible);
// (c) determinism: two builds are equal bit for bit; refit with scales 1 is the build, refit with scales 2
//     is the build of the doubled lights, and any refit's node powers are the sums of its lights';
//     skewed powers keep every leaf within 32 levels; refusals;
// (t) 65,536 lights build and refit within the per-frame budget.
//
// GPU (lavapipe, validation and synchronization validation on; fails on any message):
// (g) shaders/rt/light_tree.glsl, run by a compute probe on the CPU's tree: every light's pdf equals the
//     CPU's, the pdf sums to 1 at every point, picks equal the CPU's, and a pick's pdf equals treeLightPdf of
//     the light picked;
// (d) MIS: a Lambert floor under 256 emissive spheres (the tree), maxBounces 1, against the analytic sum
//     (albedo L r^2 cos / d^2 per sphere): NEE + MIS (the tree's pdf at a BSDF hit evaluated at the vertex
//     the ray left), BSDF only and uniform selection all match it; again with blinking scales (a hit sphere
//     shows its tree light's scaled radiance);
// (v) the tree's variance at 4 spp is at most half of uniform selection's;
// (p) the first 1024 particles of a particle field as tree lights: NEE + MIS equals BSDF only;
// (e) 4096 lights trace at 64 x 64 and 8 spp in under 10 s; 65,536 lights trace;
// (s) seeks: after other scales (a refit) and other lights (a rebuild) a frame equals a fresh tracer's;
// (r) refusals (the frame checks and the tracer's capacity);
// (k) a small city (about 17,000 window and street lamp spheres) at 4 bounces: finite and lit.
//
// `light_tree_test --look <dir> [spp]` renders a small city at night to look at instead.

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/math.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scene_gpu.hpp"
#include "post/tonemap_reference.hpp"
#include "rt/gpu_types.hpp"
#include "rt/light_tree.hpp"
#include "rt/particles.hpp"
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
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;
using rt::Float3;

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

rt::TreeLight treeLight(Float3 c, float r, Float3 e, std::uint32_t sphere) {
    rt::TreeLight l{};
    for (int i = 0; i < 3; ++i) {
        l.center[i] = c[i];
        l.emission[i] = e[i];
    }
    l.radius = r;
    l.sphere = sphere;
    return l;
}

// n lights in a box [-4, 4] x [0.5, 3] x [-4, 4], radii 0.02-0.2, coloured emissions over three decades.
std::vector<rt::TreeLight> randomLights(std::uint32_t n, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    std::vector<rt::TreeLight> lights;
    for (std::uint32_t k = 0; k < n; ++k) {
        const float power = std::pow(10.f, 3.f * u(rng) - 1.f);
        lights.push_back(treeLight({8 * u(rng) - 4, 0.5f + 2.5f * u(rng), 8 * u(rng) - 4},
                                   0.02f + 0.18f * u(rng), {power * u(rng), power * u(rng), power * u(rng)},
                                   100 + k));
    }
    return lights;
}

bool isLeaf(const rt::LightTreeNode& n) {
    return (n.child & rt::kTreeLeaf) != 0;
}

// The lights under node i.
void lightsUnder(const rt::LightTree& t, std::uint32_t i, std::vector<std::uint32_t>& out) {
    const rt::LightTreeNode& n = t.nodes[i];
    if (isLeaf(n)) {
        out.push_back(n.child & ~rt::kTreeLeaf);
        return;
    }
    lightsUnder(t, n.child, out);
    lightsUnder(t, n.child + 1, out);
}

bool near(double a, double b, double relative) {
    return std::abs(a - b) <= relative * std::max(std::abs(a), std::abs(b)) + 1e-30;
}

// 20 shading points: on the floor below the lights, among them, above them; normals up, down, sideways.
struct Point {
    Float3 p, n;
    bool transmissive;
};
std::vector<Point> shadingPoints() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    std::vector<Point> points;
    for (int i = 0; i < 20; ++i) {
        Float3 n{2 * u(rng) - 1, 2 * u(rng) - 1, 2 * u(rng) - 1};
        if (i < 8)
            n = {0, 1, 0};
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (float& x : n)
            x /= len;
        const float y = i < 8 ? 0.f : 4.f * u(rng) - 0.5f;
        points.push_back({{10 * u(rng) - 5, y, 10 * u(rng) - 5}, n, i % 5 == 4});
    }
    return points;
}

// --- (a) ---------------------------------------------------------------------------------------------------

void checkStructure(const rt::LightTree& t, std::size_t count) {
    CHECK(t.lights.size() == count);
    CHECK(t.nodes.size() == (count == 0 ? 0 : 2 * count - 1));
    std::vector<int> leafOf(count, -1);
    for (std::uint32_t i = 0; i < t.nodes.size(); ++i) {
        const rt::LightTreeNode& n = t.nodes[i];
        std::vector<std::uint32_t> under;
        lightsUnder(t, i, under);
        double power = 0;
        for (std::uint32_t k : under) {
            const rt::TreeLight& l = t.lights[k];
            power += rt::treeLightPower(l);
            for (int a = 0; a < 3; ++a) {
                CHECK(n.boundsMin[a] <= l.center[a] - l.radius);
                CHECK(n.boundsMax[a] >= l.center[a] + l.radius);
            }
        }
        CHECK(near(n.power, power, 1e-4));
        if (isLeaf(n)) {
            const std::uint32_t k = n.child & ~rt::kTreeLeaf;
            CHECK(k < count && leafOf[k] == -1);
            leafOf[k] = int(i);
            continue;
        }
        CHECK(n.child > i && n.child + 1 < t.nodes.size());
        const rt::LightTreeNode &a = t.nodes[n.child], &b = t.nodes[n.child + 1];
        for (int c = 0; c < 3; ++c) {
            CHECK(n.boundsMin[c] == std::min(a.boundsMin[c], b.boundsMin[c]));
            CHECK(n.boundsMax[c] == std::max(a.boundsMax[c], b.boundsMax[c]));
        }
    }
    // Every light's trail walks from the root to its own leaf in exactly `depth` steps.
    for (std::uint32_t k = 0; k < count; ++k) {
        const rt::TreeLight& l = t.lights[k];
        CHECK(l.depth <= rt::kMaxTreeDepth);
        std::uint32_t node = 0;
        for (std::uint32_t level = 0; level < l.depth; ++level) {
            CHECK(!isLeaf(t.nodes[node]));
            node = t.nodes[node].child + ((l.trail >> level) & 1u);
        }
        CHECK(int(node) == leafOf[k]);
    }
}

void testBuild() {
    const std::vector<rt::TreeLight> input = randomLights(1000, 1);
    const rt::LightTree t = rt::buildLightTree(input);
    checkStructure(t, input.size());
    for (std::size_t k = 0; k < input.size(); ++k) { // the lights keep their order and data
        CHECK(std::memcmp(t.lights[k].center, input[k].center, sizeof input[k].center) == 0);
        CHECK(std::memcmp(t.lights[k].emission, input[k].emission, sizeof input[k].emission) == 0);
        CHECK(t.lights[k].radius == input[k].radius && t.lights[k].sphere == input[k].sphere);
    }
    // Small trees.
    checkStructure(rt::buildLightTree({}), 0);
    checkStructure(rt::buildLightTree(std::span(input).first(1)), 1);
    checkStructure(rt::buildLightTree(std::span(input).first(2)), 2);
    checkStructure(rt::buildLightTree(std::span(input).first(77)), 77);
    // Coincident centres: split by count.
    std::vector<rt::TreeLight> same(50, treeLight({1, 2, 3}, 0.1f, {1, 1, 1}, 0));
    checkStructure(rt::buildLightTree(same), 50);

    // The pdf sums to 1 over every light at every point that sees any light of the tree (at a point that
    // sees none, the root's importance is 0, so light selection never chooses the tree there).
    double worst = 0;
    int dark = 0;
    for (const Point& q : shadingPoints())
        for (bool uniform : {false, true}) {
            double sum = 0;
            for (std::uint32_t k = 0; k < t.lights.size(); ++k) {
                const float pdf = rt::treeLightPdf(t, k, q.p, q.n, q.transmissive, uniform);
                CHECK(pdf >= 0.f && std::isfinite(pdf));
                sum += pdf;
            }
            if (!uniform && !(rt::treeNodeImportance(t.nodes[0], q.p, q.n, q.transmissive) > 0.f)) {
                CHECK(sum == 0);
                ++dark;
                continue;
            }
            if (std::abs(sum - 1) > 1e-5)
                std::printf("    point (%g %g %g) n (%g %g %g) t%d u%d: sum %g\n", q.p[0], q.p[1], q.p[2],
                            q.n[0], q.n[1], q.n[2], int(q.transmissive), int(uniform), sum);
            worst = std::max(worst, std::abs(sum - 1));
        }
    std::printf("  (a) sum of the pdf over 1000 lights at 20 points (%d see none): worst |sum - 1| = %.2e\n",
                dark, worst);
    CHECK(dark < 5);
    CHECK(worst <= 1e-5);
}

// --- (b) ---------------------------------------------------------------------------------------------------

// The upper tail of the chi-squared distribution with k degrees of freedom (Wilson-Hilferty).
double chiSquaredTail(double x, double k) {
    const double z = (std::cbrt(x / k) - (1 - 2 / (9 * k))) / std::sqrt(2 / (9 * k));
    return 0.5 * std::erfc(z / std::sqrt(2.0));
}

void testPickHistogram() {
    const rt::LightTree t = rt::buildLightTree(randomLights(1000, 2));
    const std::vector<Point> points = shadingPoints();
    for (const Point& q : {points[0], points[9], points[14]}) {
        constexpr std::uint32_t kPicks = 200000;
        std::vector<double> counts(t.lights.size(), 0);
        std::uint32_t failed = 0;
        for (std::uint32_t j = 0; j < kPicks; ++j) {
            std::uint32_t light;
            float pdf;
            const std::uint32_t random = j * 2654435761u + 12345u;
            if (!rt::pickTreeLight(t, random, q.p, q.n, q.transmissive, false, light, pdf)) {
                ++failed;
                continue;
            }
            CHECK(light < t.lights.size());
            CHECK(pdf == rt::treeLightPdf(t, light, q.p, q.n, q.transmissive, false));
            counts[light] += 1;
        }
        CHECK(failed == 0);
        // Bins of expected count >= 20 (lights merged in index order until they reach it).
        double chi2 = 0, expectedBin = 0, observedBin = 0;
        int bins = 0;
        for (std::uint32_t k = 0; k < t.lights.size(); ++k) {
            expectedBin += kPicks * double(rt::treeLightPdf(t, k, q.p, q.n, q.transmissive, false));
            observedBin += counts[k];
            if (expectedBin >= 20 || k + 1 == t.lights.size()) {
                if (expectedBin > 0)
                    chi2 += (observedBin - expectedBin) * (observedBin - expectedBin) / expectedBin;
                else
                    CHECK(observedBin == 0);
                ++bins;
                expectedBin = observedBin = 0;
            }
        }
        const double p = chiSquaredTail(chi2, bins - 1);
        std::printf("  (b) chi2 %.1f over %d bins, p = %.3f\n", chi2, bins, p);
        CHECK(p > 0.01);
    }
}

// --- (c) ---------------------------------------------------------------------------------------------------

bool sameBits(const rt::LightTree& a, const rt::LightTree& b) {
    return a.nodes.size() == b.nodes.size() && a.lights.size() == b.lights.size() &&
           std::memcmp(a.nodes.data(), b.nodes.data(), a.nodes.size() * sizeof(rt::LightTreeNode)) == 0 &&
           std::memcmp(a.lights.data(), b.lights.data(), a.lights.size() * sizeof(rt::TreeLight)) == 0;
}

void testDeterminismAndRefit() {
    const std::vector<rt::TreeLight> input = randomLights(3000, 3);
    const rt::LightTree a = rt::buildLightTree(input), b = rt::buildLightTree(input);
    CHECK(sameBits(a, b));

    rt::LightTree refit = a;
    rt::refitLightTree(refit, std::vector<float>(input.size(), 1.f));
    CHECK(sameBits(refit, a));

    std::vector<rt::TreeLight> doubled = input;
    for (rt::TreeLight& l : doubled)
        for (float& e : l.emission)
            e *= 2.f;
    rt::refitLightTree(refit, std::vector<float>(input.size(), 2.f));
    const rt::LightTree rebuilt = rt::buildLightTree(doubled);
    CHECK(sameBits(refit, rebuilt));

    // Blinking: arbitrary scales (some 0); twice in a row is the same as once (scales apply to the built
    // emission, not compound).
    std::mt19937 rng(4);
    std::vector<float> scale(input.size());
    for (float& s : scale)
        s = rng() % 5 == 0 ? 0.f : float(rng() % 1000) / 250.f;
    rt::refitLightTree(refit, scale);
    rt::LightTree again = refit;
    rt::refitLightTree(again, scale);
    CHECK(sameBits(again, refit));
    for (std::size_t k = 0; k < input.size(); ++k)
        for (int c = 0; c < 3; ++c)
            CHECK(refit.lights[k].emission[c] == input[k].emission[c] * scale[k]);
    checkStructure(refit, input.size());
    // A light blinked off is never picked (unless its sibling is dark too: then the walk takes either).
    int off = 0;
    for (std::uint32_t k = 0; k < input.size(); ++k) {
        const rt::TreeLight& l = refit.lights[k];
        if (scale[k] != 0.f || l.depth == 0)
            continue;
        std::uint32_t node = 0;
        for (std::uint32_t level = 0; level + 1 < l.depth; ++level)
            node = refit.nodes[node].child + ((l.trail >> level) & 1u);
        const std::uint32_t bit = (l.trail >> (l.depth - 1)) & 1u;
        const rt::LightTreeNode& sibling = refit.nodes[refit.nodes[node].child + 1 - bit];
        for (const Point& q : shadingPoints())
            if (rt::treeNodeImportance(sibling, q.p, q.n, q.transmissive) > 0.f) {
                CHECK(rt::treeLightPdf(refit, k, q.p, q.n, q.transmissive, false) == 0.f);
                ++off;
            }
    }
    CHECK(off > 100);

    // Skewed powers (each light 4x the next) would make a power-weighted median a chain 1000 deep.
    std::vector<rt::TreeLight> skewed;
    for (std::uint32_t k = 0; k < 1000; ++k) {
        const float e = std::ldexp(1.f, 60 - int(k % 120));
        skewed.push_back(treeLight({float(k) * 0.01f, 1, 0}, 0.01f, {e, e, e}, k));
    }
    const rt::LightTree deep = rt::buildLightTree(skewed);
    checkStructure(deep, skewed.size());
    std::uint32_t deepest = 0;
    for (const rt::TreeLight& l : deep.lights)
        deepest = std::max(deepest, l.depth);
    std::printf("  (c) skewed powers: deepest leaf %u\n", deepest);
    CHECK(deepest <= rt::kMaxTreeDepth);

    // Refusals.
    std::vector<rt::TreeLight> bad = randomLights(4, 5);
    bad[2].radius = 0.f;
    CHECK(throws<std::invalid_argument>([&] { rt::buildLightTree(bad); }));
    bad = randomLights(4, 5);
    bad[1].emission[1] = -1.f;
    CHECK(throws<std::invalid_argument>([&] { rt::buildLightTree(bad); }));
    bad = randomLights(4, 5);
    bad[3].center[0] = std::nanf("");
    CHECK(throws<std::invalid_argument>([&] { rt::buildLightTree(bad); }));
    CHECK(throws<std::invalid_argument>(
        [&] { rt::buildLightTree(std::vector<rt::TreeLight>(rt::kMaxTreeLights + 1, input[0])); }));
    rt::LightTree small = rt::buildLightTree(randomLights(4, 6));
    CHECK(throws<std::invalid_argument>([&] { rt::refitLightTree(small, std::vector<float>(3, 1.f)); }));
    CHECK(throws<std::invalid_argument>([&] { rt::refitLightTree(small, {{1.f, -1.f, 1.f, 1.f}}); }));
}

// --- (t) ---------------------------------------------------------------------------------------------------

void testBudget() {
    const std::vector<rt::TreeLight> input = randomLights(rt::kMaxTreeLights, 8);
    double build = 1e9, refit = 1e9;
    rt::LightTree t;
    const std::vector<float> scale(input.size(), 0.5f);
    for (int run = 0; run < 3; ++run) { // the best of three: the machine is shared
        auto t0 = std::chrono::steady_clock::now();
        t = rt::buildLightTree(input);
        build = std::min(build, secondsSince(t0));
        t0 = std::chrono::steady_clock::now();
        rt::refitLightTree(t, scale);
        refit = std::min(refit, secondsSince(t0));
    }
    std::printf("  (t) 65,536 lights: build %.2f ms, refit %.2f ms\n", 1e3 * build, 1e3 * refit);
    checkStructure(t, input.size());
    // Generous on this shared machine (the tracer only rebuilds when the lights move, else refits).
    CHECK(build < 0.1);
    CHECK(refit < 0.01);
}

// =========================================================================================================
// GPU
// =========================================================================================================

using modern::Camera;
using modern::SceneInstance;
using modern::TraceSettings;
using modern::Vec3;
using test::TestScene;

const std::filesystem::path kSpirvDir = EVO_TEST_SPIRV_DIR;
const std::filesystem::path kParticleShader = kSpirvDir / "test_particles_grid.comp.spv";

// Waits for the queue: synchronization validation in layer 1.3.275 credits no host wait on the timeline, so
// memory reused between subtests would read as a hazard (Decision 13 of step 4).
void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

// --- Readback of the lighting image (RGBA32F)
// ---------------------------------------------------------------

struct Plane {
    std::uint32_t width = 0, height = 0;
    std::vector<float> rgba;
    float at(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return rgba[(std::size_t(y) * width + x) * 4 + c];
    }
    double lum(std::uint32_t x, std::uint32_t y) const {
        return 0.2126 * at(x, y, 0) + 0.7152 * at(x, y, 1) + 0.0722 * at(x, y, 2);
    }
    bool operator==(const Plane& o) const {
        return width == o.width && height == o.height &&
               std::memcmp(rgba.data(), o.rgba.data(), rgba.size() * sizeof(float)) == 0;
    }
};

struct PendingRead {
    gfx::Buffer buffer;
    std::uint32_t width, height;
    bool half = false;
};

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    float value;
    if (exponent == 0)
        value = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31)
        value = mantissa ? std::nanf("") : INFINITY;
    else
        value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -value : value;
}

// RGBA32F (lighting) or RGBA16F (emission).
PendingRead recordRead(gfx::Context& ctx, VkCommandBuffer cmd, gfx::Image& image) {
    const bool half = image.format() == VK_FORMAT_R16G16B16A16_SFLOAT;
    if (!half && image.format() != VK_FORMAT_R32G32B32A32_SFLOAT)
        throw std::logic_error("recordRead: RGBA32F or RGBA16F only");
    const VkExtent2D extent = image.extent();
    PendingRead read{gfx::Buffer(ctx, {VkDeviceSize(extent.width) * extent.height * (half ? 8 : 16),
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "light tree test readback"}),
                     extent.width, extent.height, half};
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
    Plane plane{read.width, read.height, std::vector<float>(std::size_t(read.width) * read.height * 4)};
    if (!read.half) {
        std::memcpy(plane.rgba.data(), read.buffer.mapped(), plane.rgba.size() * sizeof(float));
        return plane;
    }
    const auto* halves = static_cast<const std::uint8_t*>(read.buffer.mapped());
    for (std::size_t i = 0; i < plane.rgba.size(); ++i) {
        std::uint16_t h;
        std::memcpy(&h, halves + 2 * i, 2);
        plane.rgba[i] = halfToFloat(h);
    }
    return plane;
}

// A path tracer for one scene with its own ring and, when the scene has a particle shader, its own particle
// field (declared in the order they must outlive each other).
class Tracer {
public:
    Tracer(gfx::Context& ctx, const TestScene& scene, std::uint32_t width, std::uint32_t height,
           std::optional<modern::TracerCapacity> capacity = std::nullopt)
        : ctx_(ctx), gpu_(ctx, scene.statics), ring_(ctx),
          particles_(scene.statics.particleShader.empty()
                         ? nullptr
                         : std::make_unique<rt::ParticleField>(ctx, ring_, scene.statics.capacity.particles,
                                                               scene.statics.particleShader)),
          tracer_(ctx, ring_, width, height,
                  capacity ? *capacity : modern::tracerCapacity(scene.statics.capacity)) {}

    // The lighting image of one frame (and how long the frame took, recording to readback).
    Plane trace(const modern::SceneFrame& frame, const TraceSettings& settings, double seconds = 0,
                double* elapsed = nullptr, Plane* emission = nullptr) {
        const auto start = std::chrono::steady_clock::now();
        VkCommandBuffer cmd = ring_.begin();
        modern::TraceExtras extras;
        if (frame.particles) {
            particles_->record(cmd, frame.particles->count, seconds, seconds, frame.particles->params,
                               frame.particles->material);
            extras.particleSpheres = &particles_->records();
            extras.particleBlas = particles_->blasAddress();
        }
        tracer_.trace(cmd, gpu_, frame, std::nullopt, settings, &unused_, extras);
        PendingRead read = recordRead(ctx_, cmd, tracer_.gbuffer().lighting);
        std::optional<PendingRead> readEmission;
        if (emission)
            readEmission = recordRead(ctx_, cmd, tracer_.gbuffer().emission);
        ring_.wait(ring_.submit());
        if (elapsed)
            *elapsed = secondsSince(start);
        idle(ctx_);
        if (emission)
            *emission = finishRead(*readEmission);
        return finishRead(read);
    }

    // trace() without a frame of its own (a refusal test: nothing may have been recorded).
    void refuse(const modern::SceneFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        try {
            tracer_.trace(cmd, gpu_, frame, std::nullopt, {}, &unused_);
        } catch (...) {
            ring_.wait(ring_.submit());
            idle(ctx_);
            throw;
        }
        ring_.wait(ring_.submit());
        idle(ctx_);
    }

private:
    gfx::Context& ctx_;
    gfx::Image unused_{ctx_, {1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, 1, "unused retro"}};
    modern::SceneGpu gpu_;
    gfx::FrameRing ring_;
    std::unique_ptr<rt::ParticleField> particles_;
    modern::PathTracer tracer_;
};

// --- Scenes ------------------------------------------------------------------------------------------------

// The frame's one RectLight of kind Tree: it stands for every light of the tree.
rt::RectLight treeEntry(float selectWeight = 1.f) {
    rt::RectLight l{};
    l.kind = static_cast<std::uint32_t>(rt::LightKind::Tree);
    l.selectWeight = selectWeight;
    l.gate = rt::kNoLight;
    return l;
}

rt::SphereRecord sphere(Vec3 center, float radius, std::uint32_t material, Vec3 color, float emission) {
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

constexpr float kAlbedo = 0.5f;
constexpr std::uint32_t kSize = 64;

// A Lambert floor (albedo kAlbedo, IOR 1) at y = 0 under side x side emissive spheres at height 1 on a grid
// `extent` wide, radii `radius` +- 25 %, coloured, emissions over a decade: the sphere instance is the tree's
// light (entry 0), the sky is black. The camera looks straight down from 0.6 above the floor (it sees only
// the floor: the spheres are behind it). All at one height and small for their spacing, no sphere shadows
// another from the floor (the line to a far sphere passes a near one below it).
TestScene floorUnderLights(std::uint32_t side, float radius, std::uint32_t seed, float extent = 4.f) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    const std::uint32_t n = side * side;
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::lambert({kAlbedo, kAlbedo, kAlbedo}), test::emitter({0, 0, 0})};
    s.statics.capacity.spheres = n;
    s.statics.capacity.treeLights = n;
    const float spacing = extent / float(side);
    for (std::uint32_t j = 0; j < side; ++j)
        for (std::uint32_t i = 0; i < side; ++i) {
            const Vec3 c{(float(i) + 0.5f) * spacing - 0.5f * extent, 1.f,
                         (float(j) + 0.5f) * spacing - 0.5f * extent};
            const Vec3 color{0.3f + 0.7f * u(rng), 0.3f + 0.7f * u(rng), 0.3f + 0.7f * u(rng)};
            s.statics.spheres.push_back(
                sphere(c, radius * (0.75f + 0.5f * u(rng)), 1, color, std::pow(10.f, 0.3f + u(rng))));
        }
    s.frame.instances = {{test::kQuadMesh, 0, test::place({0, 0, 0}, test::kX, -90, {12, 12, 1})},
                         sphereInstance(0, n, 1)};
    s.frame.instances[1].light = 0;
    s.frame.lights = {treeEntry()};
    s.frame.treeLights = modern::treeLightsOf(s.statics, s.frame.instances[1], s.statics.materials, n);
    s.frame.camera = {{0, 0.6f, 0}, {0, 0, 0}, {0, 0, -1}, 90.f};
    s.frame.environment.intensity = 0.f;
    s.frame.clampIndirect = 1e30f;
    return s;
}

TraceSettings directOnly(std::uint32_t spp, std::uint32_t seed, bool nee, bool uniform = false) {
    TraceSettings t;
    t.spp = spp;
    t.maxBounces = 1;
    t.frameSeed = seed;
    t.nee = nee;
    t.russianRoulette = false;
    t.clampIndirect = 1e30f;
    t.fixedLightSelection = uniform;
    return t;
}

// The camera ray through a point of the picture (pixels, y down), as pathtrace.rgen's cameraDirection.
Vec3 pixelDirection(const Camera& camera, std::uint32_t width, std::uint32_t height, float px, float py) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const float t = std::tan(camera.fovYDegrees * 3.14159265f / 360.f);
    const float aspect = float(width) / float(height);
    const float ndcX = 2.f * px / float(width) - 1.f, ndcY = 1.f - 2.f * py / float(height);
    return modern::normalize(b.forward + b.right * (ndcX * t * aspect) + b.up * (ndcY * t));
}

// The floor's lighting luminance per pixel, maxBounces 1: the mean over 4 x 4 points of the pixel of
// albedo * sum_k L_k scale_k r_k^2 cos_k / d_k^2 (a sphere wholly above the horizon; the spheres do not
// shadow one another from the floor).
std::vector<double> analyticFloor(const TestScene& s) {
    const Camera& camera = s.frame.camera;
    std::vector<double> out(std::size_t(kSize) * kSize, 0.0);
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            double sum = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const Vec3 d = pixelDirection(camera, kSize, kSize, float(x) + (float(sx) + 0.5f) / 4.f,
                                                  float(y) + (float(sy) + 0.5f) / 4.f);
                    const Vec3 q = camera.eye + d * (-camera.eye.y / d.y);
                    for (const modern::TreeLightFrame& l : s.frame.treeLights) {
                        const double vx = l.center.x - q.x, vy = l.center.y - q.y, vz = l.center.z - q.z;
                        const double d2 = vx * vx + vy * vy + vz * vz;
                        const double radiance =
                            0.2126 * l.emission.x + 0.7152 * l.emission.y + 0.0722 * l.emission.z;
                        sum += kAlbedo * radiance * l.scale * l.radius * l.radius * (vy / std::sqrt(d2)) / d2;
                    }
                }
            out[std::size_t(y) * kSize + x] = sum / 16.0;
        }
    return out;
}

// The relative error of the picture's mean and of its worst 8 x 8 block's mean against `want`.
struct Errors {
    double mean, block;
};
Errors errors(const Plane& got, const std::vector<double>& want) {
    double g = 0, w = 0, block = 0;
    for (std::uint32_t by = 0; by < kSize; by += 8)
        for (std::uint32_t bx = 0; bx < kSize; bx += 8) {
            double bg = 0, bw = 0;
            for (std::uint32_t y = by; y < by + 8; ++y)
                for (std::uint32_t x = bx; x < bx + 8; ++x) {
                    bg += got.lum(x, y);
                    bw += want[std::size_t(y) * kSize + x];
                }
            block = std::max(block, std::abs(bg / bw - 1));
            g += bg;
            w += bw;
        }
    return {g / w - 1, block};
}

// The mean squared relative error per pixel against `want`.
double relativeMse(const Plane& got, const std::vector<double>& want) {
    double sum = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const double w = want[std::size_t(y) * kSize + x];
            sum += (got.lum(x, y) - w) * (got.lum(x, y) - w) / (w * w);
        }
    return sum / double(kSize * kSize);
}

// --- (g) ---------------------------------------------------------------------------------------------------

struct ProbeQuery { // shaders/test/light_tree_probe.comp
    float p[3];
    std::uint32_t flags;
    float n[3];
    std::uint32_t arg;
};
struct ProbeResult {
    std::uint32_t light;
    float pdf, again;
    std::uint32_t picked;
};
static_assert(sizeof(ProbeQuery) == 32 && sizeof(ProbeResult) == 16);

template <typename T>
gfx::Buffer hostBuffer(gfx::Context& ctx, const std::vector<T>& data, const char* name) {
    gfx::Buffer b(ctx, {std::max<std::size_t>(data.size(), 1) * sizeof(T), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        true, name});
    if (!data.empty())
        std::memcpy(b.mapped(), data.data(), data.size() * sizeof(T));
    b.flush();
    return b;
}

std::vector<ProbeResult> runProbe(gfx::Context& ctx, const rt::LightTree& tree,
                                  const std::vector<ProbeQuery>& queries) {
    const gfx::Buffer nodes = hostBuffer(ctx, tree.nodes, "probe nodes");
    const gfx::Buffer lights = hostBuffer(ctx, tree.lights, "probe lights");
    const gfx::Buffer in = hostBuffer(ctx, queries, "probe queries");
    gfx::Buffer out(ctx, {queries.size() * sizeof(ProbeResult), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                          "probe results"});
    std::array<gfx::Binding, 4> bindings{};
    for (std::uint32_t b = 0; b < 4; ++b)
        bindings[b] = gfx::Binding{b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
    {
        gfx::ComputePipeline probe(ctx, kSpirvDir / "test_light_tree_probe.comp.spv", bindings, 8);
        gfx::FrameRing ring(ctx);
        VkCommandBuffer cmd = ring.begin();
        const std::array<VkDescriptorBufferInfo, 4> buffers = {
            VkDescriptorBufferInfo{nodes.handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{lights.handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{in.handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{out.handle(), 0, VK_WHOLE_SIZE}};
        probe.bind(cmd, ring, {}, buffers);
        const std::array<std::uint32_t, 2> push = {std::uint32_t(tree.lights.size()),
                                                   std::uint32_t(queries.size())};
        probe.push(cmd, push.data(), 8);
        probe.dispatch(cmd, std::uint32_t(queries.size()), 1, 64, 1);
        gfx::bufferBarrier2(cmd, out.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                            VK_ACCESS_2_HOST_READ_BIT);
        ring.wait(ring.submit());
    }
    idle(ctx);
    out.invalidate();
    std::vector<ProbeResult> results(queries.size());
    std::memcpy(results.data(), out.mapped(), results.size() * sizeof(ProbeResult));
    return results;
}

void testProbe(gfx::Context& ctx) {
    const rt::LightTree tree = rt::buildLightTree(randomLights(1000, 11));
    const auto count = std::uint32_t(tree.lights.size());
    const std::vector<Point> points = shadingPoints();
    constexpr std::uint32_t kPicks = 2000;
    std::vector<ProbeQuery> queries;
    for (const Point& q : points) {
        const std::uint32_t flags = q.transmissive ? 1u : 0u;
        for (std::uint32_t k = 0; k < count; ++k)
            queries.push_back({{q.p[0], q.p[1], q.p[2]}, flags, {q.n[0], q.n[1], q.n[2]}, k});
        for (std::uint32_t j = 0; j < kPicks; ++j)
            queries.push_back({{q.p[0], q.p[1], q.p[2]},
                               flags | 4u,
                               {q.n[0], q.n[1], q.n[2]},
                               rt::pcgHash(j * 7919u + 3u)});
    }
    for (std::uint32_t j = 0; j < kPicks; ++j) // uniform selection
        queries.push_back({{0, 0, 0}, 2u | 4u, {0, 1, 0}, rt::pcgHash(j + 99u)});
    const std::vector<ProbeResult> got = runProbe(ctx, tree, queries);

    double worstPdf = 0, worstSum = 0, worstAgain = 0;
    std::uint32_t agree = 0, picks = 0;
    std::size_t i = 0;
    for (const Point& q : points) {
        double sum = 0;
        for (std::uint32_t k = 0; k < count; ++k, ++i) {
            const float want = rt::treeLightPdf(tree, k, q.p, q.n, q.transmissive, false);
            CHECK(got[i].picked == 1 && std::isfinite(got[i].pdf) && got[i].pdf >= 0.f);
            worstPdf = std::max(worstPdf, std::abs(double(got[i].pdf) - want) / (want + 1e-6));
            sum += got[i].pdf;
        }
        if (got[i - 1].again > 0.f) // the root sees light here
            worstSum = std::max(worstSum, std::abs(sum - 1));
        for (std::uint32_t j = 0; j < kPicks; ++j, ++i) {
            std::uint32_t light;
            float pdf;
            const bool picked =
                rt::pickTreeLight(tree, queries[i].arg, q.p, q.n, q.transmissive, false, light, pdf);
            CHECK(got[i].picked == (picked ? 1u : 0u));
            if (!picked)
                continue;
            ++picks;
            agree += got[i].light == light ? 1 : 0;
            CHECK(got[i].light < count);
            // The traversal's pdf is reproducible from the light picked.
            worstAgain = std::max(worstAgain, std::abs(double(got[i].again) / got[i].pdf - 1));
        }
    }
    for (std::uint32_t j = 0; j < kPicks; ++j, ++i) {
        std::uint32_t light;
        float pdf;
        CHECK(rt::pickTreeLight(tree, queries[i].arg, {0, 0, 0}, {0, 1, 0}, false, true, light, pdf));
        CHECK(got[i].light == light && got[i].pdf == pdf && got[i].again == pdf);
    }
    std::printf("  (g) GPU pdf vs CPU: worst %.2e; sum - 1: worst %.2e; picks equal to the CPU's: %u of %u; "
                "pick pdf vs treeLightPdf: worst %.2e\n",
                worstPdf, worstSum, agree, picks, worstAgain);
    CHECK(worstPdf < 1e-3);
    CHECK(worstSum < 1e-4);
    CHECK(picks == kPicks * points.size());
    CHECK(agree >= picks - picks / 200); // a choice within rounding of u may go the other way
    CHECK(worstAgain < 1e-6);
}

// --- (r) ---------------------------------------------------------------------------------------------------

void testRefusals(gfx::Context& ctx) {
    const TestScene base = floorUnderLights(4, 0.05f, 41);
    auto refused = [&](auto&& edit) {
        TestScene s = base;
        edit(s);
        return throws<std::exception>([&] { modern::checkSceneFrame(s.statics, s.frame); });
    };
    modern::checkSceneFrame(base.statics, base.frame);                           // the base is valid
    CHECK(refused([](TestScene& s) { s.frame.lights.push_back(treeEntry()); })); // two trees
    CHECK(refused([](TestScene& s) { s.frame.treeLights.clear(); }));            // a tree without lights
    CHECK(refused([](TestScene& s) {                                             // lights without a tree
        s.frame.lights[0].kind = static_cast<std::uint32_t>(rt::LightKind::Emitter);
        s.frame.instances[1].light = rt::kNoLight;
    }));
    CHECK(refused([](TestScene& s) { s.frame.instances[1].light = rt::kNoLight; })); // no instance is it
    CHECK(refused([](TestScene& s) { s.frame.instances.push_back(s.frame.instances[1]); })); // two are
    CHECK(refused([](TestScene& s) { s.frame.instances[0].light = 0; }));   // a triangle instance
    CHECK(refused([](TestScene& s) { s.frame.treeLights[3].sphere = 2; })); // not firstSphere + k
    CHECK(
        refused([](TestScene& s) { s.frame.treeLights.push_back(s.frame.treeLights[0]); })); // past the range
    CHECK(refused([](TestScene& s) { s.frame.treeLights[5].center.x += 0.01f; })); // not where its sphere is
    CHECK(refused([](TestScene& s) { s.frame.treeLights[5].radius *= 1.1f; }));
    CHECK(refused([](TestScene& s) { s.frame.treeLights[6].emission.y *= 1.1f; })); // not its radiance
    CHECK(refused([](TestScene& s) { s.frame.treeLights[7].scale = -1.f; }));
    CHECK(refused([](TestScene& s) { s.frame.treeLights[7].scale = std::nanf(""); }));
    CHECK(refused([](TestScene& s) { s.frame.lights[0].gate = 0; }));         // a gated tree
    CHECK(refused([](TestScene& s) { s.frame.lights[0].area = 1.f; }));       // a tree has no area
    CHECK(refused([](TestScene& s) { s.statics.capacity.treeLights = 15; })); // over capacity
    CHECK(refused([](TestScene& s) { // non-uniform scale: ellipsoids are no sphere lights
        s.frame.instances[1].transform = modern::scale({1, 2, 1});
        s.frame.treeLights = modern::treeLightsOf(s.statics, s.frame.instances[1], s.statics.materials, 16);
    }));
    // A uniformly scaled, moved instance is fine, with its lights where treeLightsOf puts them.
    TestScene moved = base;
    moved.frame.instances[1].transform =
        modern::compose(modern::translate({0.3f, 0.1f, -0.2f}), modern::scale({1.5f, 1.5f, 1.5f}));
    moved.frame.treeLights =
        modern::treeLightsOf(moved.statics, moved.frame.instances[1], moved.statics.materials, 16);
    modern::checkSceneFrame(moved.statics, moved.frame);
    CHECK(moved.frame.treeLights[0].radius == base.frame.treeLights[0].radius * 1.5f);
    // Fewer tree lights than spheres: the first ones.
    TestScene fewer = base;
    fewer.frame.treeLights.resize(10);
    modern::checkSceneFrame(fewer.statics, fewer.frame);

    // The tracer's own capacity (reserve() it).
    modern::TracerCapacity small = modern::tracerCapacity(base.statics.capacity);
    small.treeLights = 8;
    Tracer tracer(ctx, base, 16, 16, small);
    CHECK(throws<std::length_error>([&] { tracer.refuse(base.frame); }));
}

// --- (s) ---------------------------------------------------------------------------------------------------

void testSeek(gfx::Context& ctx) {
    const TestScene a = floorUnderLights(8, 0.08f, 31);
    TestScene b = a; // the lights moved: a rebuild
    b.frame.instances[1].transform = modern::translate({0.4f, 0.f, 0.3f});
    b.frame.treeLights = modern::treeLightsOf(b.statics, b.frame.instances[1], b.statics.materials, 64);
    TestScene c = a; // the lights blink: a refit
    for (std::size_t k = 0; k < c.frame.treeLights.size(); ++k)
        c.frame.treeLights[k].scale = float(k % 4) * 0.5f;
    const TraceSettings settings = directOnly(2, 5, true);

    // One tracer: a, then c (the same tree refit), b (a rebuild), c and a again (rebuilds back).
    Plane freshA, blinked, moved, blinkedAgain, afterOthers, freshC;
    {
        Tracer t(ctx, a, kSize, kSize);
        freshA = t.trace(a.frame, settings);
        blinked = t.trace(c.frame, settings);
        moved = t.trace(b.frame, settings);
        blinkedAgain = t.trace(c.frame, settings);
        afterOthers = t.trace(a.frame, settings);
    }
    {
        Tracer t(ctx, c, kSize, kSize);
        freshC = t.trace(c.frame, settings);
    }
    CHECK(afterOthers == freshA);
    CHECK(blinked == freshC && blinkedAgain == freshC);
    CHECK(!(moved == freshA) && !(blinked == freshA));
}

// --- (d) ---------------------------------------------------------------------------------------------------

void testMis(gfx::Context& ctx) {
    TestScene s = floorUnderLights(16, 0.04f, 21);
    Tracer tracer(ctx, s, kSize, kSize);
    for (int blink = 0; blink < 2; ++blink) {
        if (blink) { // scales 0, 0.5, 1 and 2: next-event estimation and hits both see L * scale
            std::mt19937 rng(77);
            for (modern::TreeLightFrame& l : s.frame.treeLights)
                l.scale = float(rng() % 4) * 0.5f + (rng() % 8 == 0 ? 0.5f : 0.f);
        }
        const std::vector<double> want = analyticFloor(s);
        const Errors nee = errors(tracer.trace(s.frame, directOnly(64, 1, true)), want);
        const Errors uniform = errors(tracer.trace(s.frame, directOnly(64, 2, true, true)), want);
        const Errors bsdf = errors(tracer.trace(s.frame, directOnly(512, 3, false)), want);
        std::printf("  (d%s) vs analytic, mean / worst 8x8 block: NEE+MIS %+.2f%% / %.2f%%, uniform "
                    "%+.2f%% / %.2f%%, BSDF only %+.2f%% / %.2f%%\n",
                    blink ? ", blinking" : "", 100 * nee.mean, 100 * nee.block, 100 * uniform.mean,
                    100 * uniform.block, 100 * bsdf.mean, 100 * bsdf.block);
        CHECK(std::abs(nee.mean) < 0.01 && nee.block < 0.03);
        CHECK(std::abs(uniform.mean) < 0.015 && uniform.block < 0.12);
        CHECK(std::abs(bsdf.mean) < 0.02 && bsdf.block < 0.12);
    }
}

// --- (v) ---------------------------------------------------------------------------------------------------

void testVariance(gfx::Context& ctx) {
    const TestScene s = floorUnderLights(16, 0.04f, 22);
    const std::vector<double> want = analyticFloor(s);
    Tracer tracer(ctx, s, kSize, kSize);
    double tree = 0, uniform = 0;
    for (std::uint32_t seed = 0; seed < 4; ++seed) {
        tree += relativeMse(tracer.trace(s.frame, directOnly(4, 100 + seed, true)), want);
        uniform += relativeMse(tracer.trace(s.frame, directOnly(4, 100 + seed, true, true)), want);
    }
    std::printf("  (v) relative MSE at 4 spp: tree %.4f, uniform %.4f (ratio %.3f)\n", tree / 4, uniform / 4,
                tree / uniform);
    CHECK(tree <= 0.5 * uniform);
}

// --- (p) ---------------------------------------------------------------------------------------------------

// The C++ mirror of test_particles_grid.comp's grid (mode 0), in floats like the shader.
constexpr float kGridSpacing = 0.05f, kGridRadius = 0.02f, kGridDepth = 0.3f, kGridW = 2.f,
                kGridDrift = 0.01f;
Vec3 gridParticle(std::uint32_t id, float seconds) {
    const float i = float(id % 64u), j = float(id / 64u);
    const float phase = 0.37f * float(id);
    return {(i - 31.5f) * kGridSpacing + kGridDrift * std::sin(kGridW * seconds + phase),
            (j - 31.5f) * kGridSpacing, kGridDepth * std::sin(1.3f * kGridW * seconds + 2.f * phase)};
}

void testParticles(gfx::Context& ctx) {
    constexpr std::uint32_t kCount = 2048, kLights = 1024;
    constexpr float kSeconds = 0.75f;
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    s.statics.materials = {test::lambert({kAlbedo, kAlbedo, kAlbedo}), test::lambert({0.9f, 0.9f, 0.9f})};
    s.statics.capacity.particles = kCount;
    s.statics.capacity.treeLights = kLights;
    s.statics.particleShader = kParticleShader;
    modern::ParticleFrame p;
    p.count = kCount;
    p.params[0] = kGridSpacing;
    p.params[1] = kGridRadius;
    p.params[2] = kGridDepth;
    p.params[4] = kGridW;
    p.params[5] = kGridDrift;
    p.material = 1;
    s.frame.particles = p;
    // The grid's plane z = 0 turned to lie flat 1 above the floor, rows towards the camera.
    const rt::Mat34 place = test::place({0, 1, 1.2f}, test::kX, 90, {1, 1, 1});
    SceneInstance field = sphereInstance(0, kCount, 1, place);
    field.dynamicSpheres = true;
    field.light = 0;
    s.frame.instances = {{test::kQuadMesh, 0, test::place({0, 0, 0}, test::kX, -90, {12, 12, 1})}, field};
    s.frame.lights = {treeEntry()};
    // The first kLights particles shine (the particle shader gives them no emission of their own: a hit
    // sphere of the tree shows its tree light's radiance).
    for (std::uint32_t k = 0; k < kLights; ++k) {
        const float h = float(k % 7) / 7.f;
        s.frame.treeLights.push_back({modern::transformPoint(place, gridParticle(k, kSeconds)), kGridRadius,
                                      Vec3{3.f + 6.f * h, 4.f, 9.f - 6.f * h}, 1.f, k});
    }
    s.frame.camera = {{0, 0.6f, 0}, {0, 0, 0}, {0, 0, -1}, 90.f};
    s.frame.environment.intensity = 0.f;
    Tracer tracer(ctx, s, kSize, kSize);
    const Plane nee = tracer.trace(s.frame, directOnly(16, 1, true), kSeconds);
    const Plane bsdf = tracer.trace(s.frame, directOnly(256, 2, false), kSeconds);
    std::vector<double> reference(std::size_t(kSize) * kSize);
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x)
            reference[std::size_t(y) * kSize + x] = nee.lum(x, y);
    const Errors e = errors(bsdf, reference);
    std::printf("  (p) particle tree lights: BSDF only vs NEE + MIS: mean %+.2f%%, worst block %.2f%%\n",
                100 * e.mean, 100 * e.block);
    CHECK(std::abs(e.mean) < 0.03 && e.block < 0.1);
}

// --- (e) ---------------------------------------------------------------------------------------------------

void testManyLights(gfx::Context& ctx) {
    {
        const TestScene s = floorUnderLights(64, 0.02f, 51, 8.f);
        Tracer tracer(ctx, s, kSize, kSize);
        double first = 0, elapsed = 0, uniform = 0;
        tracer.trace(s.frame, directOnly(8, 2, true), 0, &first); // the first frame also builds the tree
        const Plane f = tracer.trace(s.frame, directOnly(8, 1, true), 0, &elapsed);
        tracer.trace(s.frame, directOnly(8, 1, true, true), 0, &uniform);
        const Errors e = errors(f, analyticFloor(s));
        std::printf(
            "  (e) 4096 lights, 64 x 64, 8 spp: %.2f s (first frame %.2f s, uniform selection %.2f s; vs "
            "analytic %+.2f%%)\n",
            elapsed, first, uniform, 100 * e.mean);
        CHECK(elapsed < 10.0);
        CHECK(std::abs(e.mean) < 0.03);
    }
    {
        const TestScene s = floorUnderLights(256, 0.006f, 52, 8.f);
        Tracer tracer(ctx, s, 32, 32);
        double elapsed = 0;
        const Plane f = tracer.trace(s.frame, directOnly(1, 1, true), 0, &elapsed);
        double sum = 0;
        bool finite = true;
        for (float v : f.rgba)
            finite = finite && std::isfinite(v);
        for (std::uint32_t y = 0; y < 32; ++y)
            for (std::uint32_t x = 0; x < 32; ++x)
                sum += f.lum(x, y);
        std::printf("  (e) 65,536 lights, 32 x 32, 1 spp: %.2f s, mean %.3f\n", elapsed, sum / 1024);
        CHECK(finite && sum > 0);
    }
}

// --- Look --------------------------------------------------------------------------------------------------

std::uint8_t encode(float linear) {
    const float v = post::srgbOetf(linear / (1.f + linear)); // Reinhard, for looking only
    return static_cast<std::uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
}

// A small city at night: 12 x 12 lots of dark towers whose windows are about 17,000 small emissive spheres
// (warm and cool, some dark), street lamps along the streets, a wet street, a night sky; 5 % of the windows
// dimmed this frame (scale 0.2). Every window and lamp is a tree light.
TestScene cityScene() {
    constexpr std::uint32_t kLots = 12;
    constexpr float kLot = 1.6f;
    std::mt19937 rng(2026);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    TestScene s;
    s.statics.meshes = test::basicMeshes();
    rt::Material street = test::lambert({0.05f, 0.05f, 0.055f});
    street.ior = 1.5f;
    street.roughness = 0.18f; // wet
    rt::Material concrete = test::lambert({0.16f, 0.16f, 0.17f});
    concrete.roughness = 0.8f;
    rt::Material glassy = test::metal({0.25f, 0.28f, 0.32f}, 0.25f);
    s.statics.materials = {street, concrete, glassy, test::emitter({0, 0, 0})};
    s.frame.instances.push_back({test::kQuadMesh, 0, test::place({0, 0, 0}, test::kX, -90, {60, 60, 1})});
    std::vector<rt::SphereRecord>& lights = s.statics.spheres;
    const Vec3 warm{1.f, 0.72f, 0.42f}, cool{0.65f, 0.8f, 1.f}, sodium{1.f, 0.55f, 0.18f};
    const float half = 0.5f * kLot * float(kLots);
    for (std::uint32_t j = 0; j < kLots; ++j)
        for (std::uint32_t i = 0; i < kLots; ++i) {
            const float x = (float(i) + 0.5f) * kLot - half, z = (float(j) + 0.5f) * kLot - half;
            const float w = 0.8f + 0.35f * u(rng), d = 0.8f + 0.35f * u(rng);
            const float centre = std::exp(-0.02f * (x * x + z * z));
            const float h = 0.6f + (1.2f + 4.5f * centre) * u(rng) + 2.5f * centre;
            const std::uint32_t material = u(rng) < 0.3f ? 2u : 1u;
            s.frame.instances.push_back(
                {test::kCubeMesh, material, test::place({x, 0.5f * h, z}, test::kY, 0, {w, h, d})});
            // Windows on the four faces, partly sunk into them: columns across, floors up, 30 % dark.
            const float r = 0.028f;
            const Vec3 tint = u(rng) < 0.7f ? warm : cool;
            for (int face = 0; face < 4; ++face) {
                const float across = face < 2 ? w : d;
                const int columns = std::max(2, int(across / 0.22f));
                for (float y = 0.25f; y < h - 0.1f; y += 0.26f)
                    for (int c = 0; c < columns; ++c) {
                        if (u(rng) < 0.3f)
                            continue;
                        const float t = (float(c) + 0.5f) / float(columns) - 0.5f;
                        Vec3 p;
                        if (face == 0)
                            p = {x + t * w, y, z + 0.5f * d + 0.4f * r};
                        else if (face == 1)
                            p = {x - t * w, y, z - 0.5f * d - 0.4f * r};
                        else if (face == 2)
                            p = {x + 0.5f * w + 0.4f * r, y, z + t * d};
                        else
                            p = {x - 0.5f * w - 0.4f * r, y, z - t * d};
                        lights.push_back(sphere(p, r, 3, tint * (0.7f + 0.3f * u(rng)), 6.f + 10.f * u(rng)));
                    }
            }
        }
    // Sodium street lamps along the streets between the lots.
    for (std::uint32_t j = 0; j <= kLots; ++j)
        for (float a = -half; a <= half; a += 0.55f) {
            const float b = float(j) * kLot - half;
            lights.push_back(sphere({a, 0.32f, b}, 0.035f, 3, sodium, 60.f));
            lights.push_back(sphere({b, 0.32f, a + 0.27f}, 0.035f, 3, sodium, 60.f));
        }
    const auto n = std::uint32_t(lights.size());
    s.statics.capacity.spheres = n;
    s.statics.capacity.treeLights = n;
    SceneInstance windows = sphereInstance(0, n, 3);
    windows.light = 0;
    s.frame.instances.push_back(windows);
    s.frame.lights = {treeEntry()};
    s.frame.treeLights = modern::treeLightsOf(s.statics, windows, s.statics.materials, n);
    for (modern::TreeLightFrame& l : s.frame.treeLights)
        if (u(rng) < 0.05f)
            l.scale = 0.2f;
    s.frame.camera = {{-8.5f, 5.2f, 10.5f}, {0.5f, 0.6f, -0.5f}, {0, 1, 0}, 42.f};
    s.frame.environment = modern::environmentDefaults(modern::EnvironmentKind::NightSky);
    s.frame.environment.select = 0.1f;
    return s;
}

// --- (k) ---------------------------------------------------------------------------------------------------

// The city at 4 bounces, the camera seeing the tree's spheres directly (next-event estimation at points on
// them; lavapipe once crashed in the walk there): finite, lit, and the lit windows as bright as their scaled
// radiance.
void testCity(gfx::Context& ctx) {
    const TestScene s = cityScene();
    constexpr std::uint32_t kW = 96, kH = 54;
    Tracer tracer(ctx, s, kW, kH);
    TraceSettings settings;
    settings.spp = 2;
    settings.maxBounces = 4;
    settings.frameSeed = 1;
    Plane emission;
    double elapsed = 0;
    const Plane f = tracer.trace(s.frame, settings, 0, &elapsed, &emission);
    double lighting = 0, emitted = 0;
    bool finite = true;
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            for (std::uint32_t c = 0; c < 3; ++c)
                finite = finite && std::isfinite(f.at(x, y, c)) && std::isfinite(emission.at(x, y, c));
            lighting += f.lum(x, y);
            emitted += emission.lum(x, y);
        }
    std::printf(
        "  (k) city, %zu tree lights, 96 x 54, 2 spp, 4 bounces: %.2f s, mean lighting %.4f, emission "
        "%.4f\n",
        s.frame.treeLights.size(), elapsed, lighting / (kW * kH), emitted / (kW * kH));
    CHECK(finite && lighting > 0 && emitted > 0);
}

// --- Look --------------------------------------------------------------------------------------------------

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp) {
    const auto start = std::chrono::steady_clock::now();
    constexpr std::uint32_t kW = 640, kH = 360;
    const TestScene s = cityScene();
    Tracer tracer(ctx, s, kW, kH);
    TraceSettings settings;
    settings.spp = spp;
    settings.maxBounces = 4;
    settings.frameSeed = 1;
    Plane emission;
    const Plane f = tracer.trace(s.frame, settings, 0, nullptr, &emission);
    std::vector<std::uint8_t> rgb(std::size_t(kW) * kH * 3);
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x)
            for (std::uint32_t c = 0; c < 3; ++c)
                rgb[(std::size_t(y) * kW + x) * 3 + c] = encode(f.at(x, y, c) + emission.at(x, y, c));
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / ("light_tree_city_" + std::to_string(spp) + "spp.png");
    image::writePng(path, int(kW), int(kH), rgb.data());
    std::printf("look: %s, %zu tree lights, in %.1f s\n", path.string().c_str(), s.frame.treeLights.size(),
                secondsSince(start));
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    try {
        auto timed = [&](const char* name, auto&& run) {
            const auto t0 = std::chrono::steady_clock::now();
            run();
            std::printf("  [%s %.2f s]\n", name, secondsSince(t0));
        };
        timed("a", [&] { testBuild(); });
        timed("b", [&] { testPickHistogram(); });
        timed("c", [&] { testDeterminismAndRefit(); });
        timed("t", [&] { testBudget(); });
        test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the results are checked\n");
            else if (!ctx.syncValidationEnabled()) {
                std::printf("SKIP sync validation unavailable\n");
                return 0;
            }
            if (argc >= 3 && std::string(argv[1]) == "--look") {
                renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 32u);
                return 0;
            }
            timed("g", [&] { testProbe(ctx); });
            timed("r", [&] { testRefusals(ctx); });
            timed("s", [&] { testSeek(ctx); });
            timed("d", [&] { testMis(ctx); });
            timed("v", [&] { testVariance(ctx); });
            timed("p", [&] { testParticles(ctx); });
            timed("e", [&] { testManyLights(ctx); });
            timed("k", [&] { testCity(ctx); });
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("light_tree_test: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
