// The ray tracing pipeline and its shader binding table. computeSbtLayout is checked against hand-computed
// layouts for lavapipe's properties and for RTX's (a layout that is aligned on lavapipe can
// be misaligned on RTX), including the vkCmdTraceRaysKHR rules it must keep. A probe pipeline (one raygen,
// two misses, four closest-hit groups) then traces a 64x64 launch over a TLAS of four quads, one per quadrant
// with hit group 0-3, and a gap column: every pixel must hold its hit group + 1, or 100 + the miss index it
// traced with (one row uses miss 1). Runs with the validation layer on and fails on any message.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/descriptors.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/rt_pipeline.hpp"
#include "gfx/sbt.hpp"
#include "gfx/shader.hpp"
#include "gfx/sync.hpp"
#include "rt/accel.hpp"
#include "rt/geometry.hpp"
#include "rt/gpu_types.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;

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

const std::filesystem::path kSpirvDir = EVO_TEST_SPIRV_DIR;

// --- computeSbtLayout --------------------------------------------------------------------------------------

// handle size, handle alignment, base alignment, max recursion, scratch alignment, max group stride.
constexpr gfx::RayTracingProperties kLavapipe{32, 16, 32, 31, 8, 16384};
constexpr gfx::RayTracingProperties kRtx{32, 32, 64, 31, 128, 4096};
constexpr gfx::RayTracingProperties kBase128{32, 32, 128, 31, 128, 4096};

bool sameRegion(const gfx::SbtRegion& got, gfx::SbtRegion want, const char* what) {
    if (got.offset == want.offset && got.stride == want.stride && got.size == want.size)
        return true;
    std::printf("%s: got {%llu, %llu, %llu}, want {%llu, %llu, %llu}\n", what,
                static_cast<unsigned long long>(got.offset), static_cast<unsigned long long>(got.stride),
                static_cast<unsigned long long>(got.size), static_cast<unsigned long long>(want.offset),
                static_cast<unsigned long long>(want.stride), static_cast<unsigned long long>(want.size));
    return false;
}

// The rules vkCmdTraceRaysKHR puts on the regions, for an SBT whose start is aligned to the base alignment:
// every region starts on the base alignment (VUID-vkCmdTraceRaysKHR-pRayGenShaderBindingTable-03682 and the
// miss/hit equivalents), the raygen region's size equals its stride (-size-04023), the miss and hit strides
// are multiples of the handle alignment (-stride-03686, -03690) and at most maxShaderGroupStride
// (-stride-04029, -04035); each record holds a whole handle and the regions do not overlap and fit in the
// total.
void checkTraceRaysRules(const gfx::SbtLayout& layout, const gfx::RayTracingProperties& p,
                         std::uint32_t misses, std::uint32_t hits) {
    const gfx::SbtRegion* regions[3] = {&layout.raygen, &layout.miss, &layout.hit};
    const std::uint32_t counts[3] = {1, misses, hits};
    VkDeviceSize end = 0;
    for (int r = 0; r < 3; ++r) {
        const gfx::SbtRegion& region = *regions[r];
        CHECK(region.offset % p.shaderGroupBaseAlignment == 0);
        CHECK(region.offset >= end);
        CHECK(region.stride >= p.shaderGroupHandleSize);
        CHECK(region.size >= counts[r] * region.stride);
        end = region.offset + region.size;
    }
    CHECK(layout.raygen.size == layout.raygen.stride);
    for (const gfx::SbtRegion* region : {&layout.miss, &layout.hit}) {
        CHECK(region->stride % p.shaderGroupHandleAlignment == 0);
        CHECK(region->stride <= p.maxShaderGroupStride);
    }
    CHECK(layout.totalBytes == end);
}

void testSbtLayouts() {
    // lavapipe: a = alignUp(32, 16) = 32; raygen alignUp(32, 32) = 32.
    const gfx::SbtLayout lavapipe = gfx::computeSbtLayout(kLavapipe, 2, 4);
    CHECK(sameRegion(lavapipe.raygen, {0, 32, 32}, "lavapipe raygen"));
    CHECK(sameRegion(lavapipe.miss, {32, 32, 64}, "lavapipe miss"));
    CHECK(sameRegion(lavapipe.hit, {96, 32, 128}, "lavapipe hit"));
    CHECK(lavapipe.totalBytes == 224);
    checkTraceRaysRules(lavapipe, kLavapipe, 2, 4);

    // RTX: a = 32; raygen alignUp(32, 64) = 64; the hit region at 128, not 96 as lavapipe's layout has it.
    const gfx::SbtLayout rtx = gfx::computeSbtLayout(kRtx, 2, 4);
    CHECK(sameRegion(rtx.raygen, {0, 64, 64}, "rtx raygen"));
    CHECK(sameRegion(rtx.miss, {64, 32, 64}, "rtx miss"));
    CHECK(sameRegion(rtx.hit, {128, 32, 128}, "rtx hit"));
    CHECK(rtx.totalBytes == 256);
    checkTraceRaysRules(rtx, kRtx, 2, 4);

    // Step 5's path tracer (analytic spheres): 2 misses and 8 hit groups, hitGroupFor(class, kind) = the 4 triangle
    // groups then the 4 sphere groups. RTX: the hit region at 128, 8 records of 32 = 256 bytes; group 7 (the
    // spheres' Checker) is its last record. lavapipe: at 96, the same 256.
    static_assert(rt::hitGroupFor(rt::MaterialClass::Pbr, rt::GeometryKind::Spheres) == 4 &&
                  rt::hitGroupFor(rt::MaterialClass::Checker, rt::GeometryKind::Spheres) == 7 &&
                  rt::hitGroupFor(rt::MaterialClass::Checker, rt::GeometryKind::Triangles) == 3);
    const gfx::SbtLayout rtx8 = gfx::computeSbtLayout(kRtx, 2, 2 * rt::kHitGroupsPerKind);
    CHECK(sameRegion(rtx8.raygen, {0, 64, 64}, "rtx 8 groups raygen"));
    CHECK(sameRegion(rtx8.miss, {64, 32, 64}, "rtx 8 groups miss"));
    CHECK(sameRegion(rtx8.hit, {128, 32, 256}, "rtx 8 groups hit"));
    CHECK(rtx8.totalBytes == 384);
    CHECK(rtx8.hit.offset + 7 * rtx8.hit.stride + kRtx.shaderGroupHandleSize <=
          rtx8.hit.offset + rtx8.hit.size);
    checkTraceRaysRules(rtx8, kRtx, 2, 8);
    const gfx::SbtLayout lavapipe8 = gfx::computeSbtLayout(kLavapipe, 2, 8);
    CHECK(sameRegion(lavapipe8.hit, {96, 32, 256}, "lavapipe 8 groups hit"));
    CHECK(lavapipe8.totalBytes == 352);
    checkTraceRaysRules(lavapipe8, kLavapipe, 2, 8);

    // Base alignment 128: one miss and one hit group each get a whole 128-byte block.
    const gfx::SbtLayout base128 = gfx::computeSbtLayout(kBase128, 1, 1);
    CHECK(sameRegion(base128.raygen, {0, 128, 128}, "base128 raygen"));
    CHECK(sameRegion(base128.miss, {128, 32, 128}, "base128 miss"));
    CHECK(sameRegion(base128.hit, {256, 32, 128}, "base128 hit"));
    CHECK(base128.totalBytes == 384);
    checkTraceRaysRules(base128, kBase128, 1, 1);

    // A handle that is not a multiple of the handle alignment: records grow to the alignment.
    constexpr gfx::RayTracingProperties oddHandle{48, 32, 64, 1, 8, 4096};
    const gfx::SbtLayout odd = gfx::computeSbtLayout(oddHandle, 3, 5);
    CHECK(sameRegion(odd.raygen, {0, 64, 64}, "odd raygen"));
    CHECK(sameRegion(odd.miss, {64, 64, 192}, "odd miss"));
    CHECK(sameRegion(odd.hit, {256, 64, 320}, "odd hit"));
    CHECK(odd.totalBytes == 576);
    checkTraceRaysRules(odd, oddHandle, 3, 5);

    // Every count the path tracer may use keeps the rules on both property sets.
    for (const gfx::RayTracingProperties& p : {kLavapipe, kRtx, kBase128})
        for (std::uint32_t misses = 1; misses <= 4; ++misses)
            for (std::uint32_t hits = 1; hits <= 12; ++hits)
                checkTraceRaysRules(gfx::computeSbtLayout(p, misses, hits), p, misses, hits);

    std::printf("expecting four refused layouts:\n");
    constexpr gfx::RayTracingProperties narrowStride{32, 32, 64, 1, 8, 16}; // stride 32 > max 16
    CHECK(throws<std::invalid_argument>([&] { (void)gfx::computeSbtLayout(narrowStride, 1, 1); }));
    constexpr gfx::RayTracingProperties notPowerOfTwo{32, 24, 64, 1, 8, 4096};
    CHECK(throws<std::invalid_argument>([&] { (void)gfx::computeSbtLayout(notPowerOfTwo, 1, 1); }));
    constexpr gfx::RayTracingProperties baseBelowHandle{32, 64, 32, 1, 8, 4096};
    CHECK(throws<std::invalid_argument>([&] { (void)gfx::computeSbtLayout(baseBelowHandle, 1, 1); }));
    constexpr gfx::RayTracingProperties noHandle{0, 32, 64, 1, 8, 4096};
    CHECK(throws<std::invalid_argument>([&] { (void)gfx::computeSbtLayout(noHandle, 1, 1); }));
}

// --- The probe ---------------------------------------------------------------------------------------------

constexpr std::uint32_t kSize = 64;
constexpr std::uint32_t kMissRow = 40;

struct ProbePush {
    std::uint32_t width, height, missRow;
};

const std::array<gfx::Binding, 2> kProbeBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
};

gfx::RtShaders probeShaders() {
    gfx::RtShaders shaders;
    shaders.raygen = kSpirvDir / "test_sbt_probe.rgen.spv";
    for (int i = 0; i < 2; ++i)
        shaders.misses.push_back(kSpirvDir / ("test_sbt_probe_miss" + std::to_string(i) + ".rmiss.spv"));
    for (int k = 0; k < 4; ++k)
        shaders.closestHits.push_back(kSpirvDir / ("test_sbt_probe_hit" + std::to_string(k) + ".rchit.spv"));
    return shaders;
}

// A unit quad in the xy plane facing +z.
rt::Mesh unitQuad() {
    rt::Mesh mesh;
    const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    for (const auto& c : corners)
        mesh.vertices.push_back({{c[0], c[1], 0.0f}, {0.0f, 0.0f, 1.0f}, {c[0] + 0.5f, c[1] + 0.5f}});
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

// Quadrant k = (x > 0) + 2 * (y > 0) of the launch: a quad over x in [2, 32] (or [-32, -2]) and y in [0, 32]
// (or [-32, 0]) at z = 0, hit group k. The pixel centres sit at odd multiples of 0.5, so no ray grazes an
// edge, and the four columns with |x| < 2 miss.
std::vector<rt::Instance> quadrantQuads() {
    std::vector<rt::Instance> instances;
    for (std::uint32_t k = 0; k < 4; ++k) {
        rt::Mat34 m{};
        m.m[0][0] = 30.0f;
        m.m[1][1] = 32.0f;
        m.m[2][2] = 1.0f;
        m.m[0][3] = (k & 1) ? 17.0f : -17.0f;
        m.m[1][3] = (k & 2) ? 16.0f : -16.0f;
        // The custom index differs from the hit group, so the test sees that the SBT offset picks the group.
        instances.push_back({0, m, 7 - k, k});
    }
    return instances;
}

std::uint32_t expectedValue(std::uint32_t x, std::uint32_t y) {
    const float px = float(x) - 0.5f * kSize + 0.5f;
    const float py = float(y) - 0.5f * kSize + 0.5f;
    if (px > -2.0f && px < 2.0f)
        return y == kMissRow ? 101 : 100;
    const std::uint32_t k = (px > 0.0f ? 1u : 0u) + (py > 0.0f ? 2u : 0u);
    return k + 1;
}

void testProbe(gfx::Context& ctx) {
    const gfx::RayTracingProperties& props = ctx.rayTracingProperties();
    std::printf("max shader group stride %u\n", props.maxShaderGroupStride);
    CHECK(props.maxShaderGroupStride >= props.shaderGroupHandleSize);
    if (ctx.deviceName().find("llvmpipe") != std::string::npos)
        CHECK(props.maxShaderGroupStride == 16384);
    CHECK(ctx.deviceInfoJson().find("\"maxShaderGroupStride\":" +
                                    std::to_string(props.maxShaderGroupStride)) != std::string::npos);

    const std::array<rt::Mesh, 1> meshes = {unitQuad()};
    rt::GeometryStore store(ctx, meshes);
    rt::BlasSet blases(ctx, store);
    gfx::RayTracingPipeline pipeline(ctx, probeShaders(), kProbeBindings, sizeof(ProbePush));

    // The SBT follows computeSbtLayout for this device, and starts on the base alignment.
    const gfx::SbtLayout want = gfx::computeSbtLayout(props, 2, 4);
    CHECK(sameRegion(pipeline.sbtLayout().raygen, want.raygen, "pipeline raygen"));
    CHECK(sameRegion(pipeline.sbtLayout().miss, want.miss, "pipeline miss"));
    CHECK(sameRegion(pipeline.sbtLayout().hit, want.hit, "pipeline hit"));
    CHECK(pipeline.sbtLayout().totalBytes == want.totalBytes);
    CHECK(pipeline.sbtAddress() != 0);
    CHECK(pipeline.sbtAddress() % props.shaderGroupBaseAlignment == 0);
    CHECK(gfx::RayTracingPipeline::kMaxRecursionDepth == 1);

    gfx::Buffer out(ctx, {kSize * kSize * sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                          "sbt probe output"});
    std::memset(out.mapped(), 0xEE, out.size()); // no pixel may keep this
    out.flush();
    gfx::FrameRing ring(ctx);
    rt::Tlas tlas(ctx, ring, 4);

    VkCommandBuffer cmd = ring.begin();
    tlas.build(cmd, blases, quadrantQuads());
    const std::array<gfx::DescriptorWrite, 2> writes = {
        gfx::DescriptorWrite::accelerationStructure(0, tlas.handle()),
        gfx::DescriptorWrite::buffer(1, {out.handle(), 0, VK_WHOLE_SIZE})};
    pipeline.bind(cmd, ring, writes);
    const ProbePush push{kSize, kSize, kMissRow};
    pipeline.push(cmd, &push, sizeof push);
    pipeline.traceRays(cmd, kSize, kSize);
    gfx::bufferBarrier2(cmd, out.handle(), VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                        VK_ACCESS_2_HOST_READ_BIT);
    ring.wait(ring.submit());

    out.invalidate();
    std::vector<std::uint32_t> got(kSize * kSize);
    std::memcpy(got.data(), out.mapped(), got.size() * sizeof(std::uint32_t));
    std::map<std::uint32_t, int> histogram;
    int wrong = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const std::uint32_t value = got[y * kSize + x];
            ++histogram[value];
            if (value != expectedValue(x, y) && wrong++ < 8)
                std::printf("pixel %u,%u: got %u, want %u\n", x, y, value, expectedValue(x, y));
        }
    std::printf("probe values:");
    for (const auto& [value, count] : histogram)
        std::printf(" %u x%d", value, count);
    std::printf("\n");
    CHECK(wrong == 0);
    // Every hit group and both misses were reached: 4 quadrants of 30 x 32 pixels, 4 gap columns.
    for (std::uint32_t value : {1u, 2u, 3u, 4u})
        CHECK(histogram[value] == 30 * 32);
    CHECK(histogram[100] == 4 * 63);
    CHECK(histogram[101] == 4);
}

void testRefusals(gfx::Context& ctx) {
    std::printf("expecting six refused pipelines:\n");
    gfx::RtShaders noMiss = probeShaders();
    noMiss.misses.clear();
    CHECK(throws<std::invalid_argument>([&] { gfx::RayTracingPipeline(ctx, noMiss, kProbeBindings, 12); }));
    gfx::RtShaders noHit = probeShaders();
    noHit.closestHits.clear();
    CHECK(throws<std::invalid_argument>([&] { gfx::RayTracingPipeline(ctx, noHit, kProbeBindings, 12); }));
    CHECK(throws<std::invalid_argument>(
        [&] { gfx::RayTracingPipeline(ctx, probeShaders(), kProbeBindings, 10); }));
    gfx::RtShaders missing = probeShaders();
    missing.closestHits[2] = kSpirvDir / "no_such_shader.rchit.spv";
    CHECK(throws<std::runtime_error>([&] { gfx::RayTracingPipeline(ctx, missing, kProbeBindings, 12); }));

    // Recursion depth 1: only the raygen may trace, and no stage may call a callable. lavapipe and the
    // validation layer do not enforce maxPipelineRayRecursionDepth (an RTX GPU would hang), so the pipeline
    // scans the SPIR-V itself.
    std::printf("expecting three refused recursive pipelines:\n");
    gfx::RtShaders tracingHit = probeShaders();
    tracingHit.closestHits[1] = kSpirvDir / "test_recursive_hit.rchit.spv";
    CHECK(
        throws<std::invalid_argument>([&] { gfx::RayTracingPipeline(ctx, tracingHit, kProbeBindings, 12); }));
    gfx::RtShaders tracingMiss = probeShaders();
    tracingMiss.misses[0] = kSpirvDir / "test_recursive_miss.rmiss.spv";
    CHECK(throws<std::invalid_argument>(
        [&] { gfx::RayTracingPipeline(ctx, tracingMiss, kProbeBindings, 12); }));
    gfx::RtShaders callable = probeShaders();
    callable.raygen = kSpirvDir / "test_callable.rgen.spv";
    CHECK(throws<std::invalid_argument>([&] { gfx::RayTracingPipeline(ctx, callable, kProbeBindings, 12); }));

    // The scanner itself: the raygen traces, a miss does not; a module that is not SPIR-V is refused.
    CHECK(gfx::spirvHasOpcode(gfx::loadSpirv(probeShaders().raygen), gfx::kSpirvOpTraceRayKHR));
    CHECK(!gfx::spirvHasOpcode(gfx::loadSpirv(probeShaders().misses[0]), gfx::kSpirvOpTraceRayKHR));
    const std::vector<std::uint32_t> notSpirv{0x12345678u, 0, 0, 0, 0, 0x00010000u};
    CHECK(throws<std::runtime_error>([&] { (void)gfx::spirvHasOpcode(notSpirv, 1); }));
    const std::vector<std::uint32_t> zeroCount{0x07230203u, 0, 0, 0, 0, 0x00000011u};
    CHECK(throws<std::runtime_error>([&] { (void)gfx::spirvHasOpcode(zeroCount, 1); }));

    gfx::RayTracingPipeline pipeline(ctx, probeShaders(), kProbeBindings, sizeof(ProbePush));
    const ProbePush push{};
    CHECK(throws<std::invalid_argument>([&] { pipeline.push(VK_NULL_HANDLE, &push, 16); }));
    gfx::FrameRing ring(ctx);
    CHECK(throws<std::logic_error>([&] { pipeline.bind(VK_NULL_HANDLE, ring, {}); }));
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    try {
        testSbtLayouts();
        {
            auto ctx = gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the results are checked\n");
            testProbe(ctx);
            testRefusals(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        // Also counts what the layer reported while the objects and the Context were destroyed.
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("rt_pipeline_test: %d checks passed in %.1f s\n", checksPassed, seconds);
    return 0;
}
