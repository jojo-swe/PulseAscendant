// Materials and the structs the path tracer shares with GLSL, on the GPU (compute only, no ray tracing), with
// the validation layer on; fails on any message it reports.
//
// Layout (Review Focus 10): the CPU fills two elements of every shared struct with field sentinels (word k of
// element e holds k + 0.5 + 100 e as a float or k + 1 + 100 e as a uint), struct_layout.comp copies every
// field in declaration order into a word buffer, and the words must come back in order: a field whose C++
// offset differs from its scalar-layout GLSL offset reads another field's sentinel. The shader also writes
// the shared constants, which must equal the C++ enums.
//
// BSDFs (bsdf.glsl through bsdf_probe.comp): 8 materials x 8 outgoing angles from 0 to 89.9 degrees x 65 536
// samples each. White furnace (Lambert white 1.0, delta mirror exactly 1, rough metal = a CPU integration of
// single-scattering GGX, smooth glass reflect + transmit = 1 and reflect = exact Fresnel), no material
// above 1.01, the sampler's pdf equals bsdfEval's pdf, the sample weight equals f cos / pdf, reflection is
// reciprocal, and every output is finite (Review Focus 8), also for a stress set (IOR 1 and 1.0001, roughness
// at the delta threshold and below the alpha clamp, wo in the tangent plane and below the surface, an
// all-zero material).

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/sync.hpp"
#include "rt/geometry.hpp"
#include "rt/gpu_types.hpp"
#include "rt/random.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo::gfx;
namespace rt = evo::rt;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

Buffer storage(Context& ctx, VkDeviceSize bytes, const char* name) {
    return Buffer(ctx, {bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, name});
}

template <typename T> Buffer upload(Context& ctx, const std::vector<T>& items, const char* name) {
    Buffer buffer = storage(ctx, sizeof(T) * items.size(), name);
    std::memcpy(buffer.mapped(), items.data(), sizeof(T) * items.size());
    buffer.flush();
    return buffer;
}

VkDescriptorBufferInfo whole(const Buffer& buffer) {
    return {buffer.handle(), 0, VK_WHOLE_SIZE};
}

// Records one dispatch of `pipeline` over the given storage buffers (binding order), waits for it and makes
// `output` visible to the host.
void runOnce(Context& ctx, ComputePipeline& pipeline, const std::vector<const Buffer*>& buffers,
             const Buffer& output, const void* push, std::uint32_t pushBytes, std::uint32_t width,
             std::uint32_t height, std::uint32_t localX, std::uint32_t localY) {
    std::vector<VkDescriptorBufferInfo> infos;
    for (const Buffer* buffer : buffers)
        infos.push_back(whole(*buffer));
    FrameRing ring(ctx); // declared after what its frame uses
    VkCommandBuffer cmd = ring.begin();
    pipeline.bind(cmd, ring, {}, infos);
    pipeline.push(cmd, push, pushBytes);
    pipeline.dispatch(cmd, width, height, localX, localY);
    bufferBarrier2(cmd, output.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    ring.wait(ring.submit());
    output.invalidate();
}

// --- Layout ------------------------------------------------------------------------------------------------

// Word k of element e as the sentinel the CPU wrote: a float k + 0.5 + 100 e, or a uint k + 1 + 100 e.
float floatSentinel(std::uint32_t word, std::uint32_t element) {
    return static_cast<float>(word) + 0.5f + 100.f * static_cast<float>(element);
}
std::uint32_t uintSentinel(std::uint32_t word, std::uint32_t element) {
    return word + 1 + 100 * element;
}

rt::Material sentinelMaterial(std::uint32_t e) {
    rt::Material m{};
    m.baseColor[0] = floatSentinel(0, e);
    m.baseColor[1] = floatSentinel(1, e);
    m.baseColor[2] = floatSentinel(2, e);
    m.roughness = floatSentinel(3, e);
    m.metallic = floatSentinel(4, e);
    m.ior = floatSentinel(5, e);
    m.transmission = floatSentinel(6, e);
    m.emission[0] = floatSentinel(7, e);
    m.emission[1] = floatSentinel(8, e);
    m.emission[2] = floatSentinel(9, e);
    m.emissionScale = floatSentinel(10, e);
    m.coat = floatSentinel(11, e);
    m.coatRoughness = floatSentinel(12, e);
    m.pattern = uintSentinel(13, e);
    m.texture = uintSentinel(14, e);
    m.patternScale = floatSentinel(15, e);
    m.baseColor2[0] = floatSentinel(16, e);
    m.baseColor2[1] = floatSentinel(17, e);
    m.baseColor2[2] = floatSentinel(18, e);
    m.materialClass = uintSentinel(19, e);
    m.flags = uintSentinel(20, e);
    m.abbe = floatSentinel(21, e);
    m.medium = uintSentinel(22, e);
    m.receiver = uintSentinel(23, e);
    return m;
}
constexpr std::array<bool, 24> kMaterialIsUint{false, false, false, false, false, false, false, false,
                                               false, false, false, false, false, true,  true,  false,
                                               false, false, false, true,  true,  false, true,  true};

rt::RectLight sentinelLight(std::uint32_t e) {
    rt::RectLight l{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        l.corner[i] = floatSentinel(i, e);
        l.edgeU[i] = floatSentinel(3 + i, e);
        l.edgeV[i] = floatSentinel(6 + i, e);
        l.emission[i] = floatSentinel(9 + i, e);
    }
    l.kind = uintSentinel(12, e);
    l.selectWeight = floatSentinel(13, e);
    l.area = floatSentinel(14, e);
    l.gate = uintSentinel(15, e);
    return l;
}
constexpr std::array<bool, 16> kLightIsUint{false, false, false, false, false, false, false, false,
                                            false, false, false, false, true,  false, false, true};

rt::InstanceRecord sentinelInstance(std::uint32_t e) {
    return {uintSentinel(0, e), uintSentinel(1, e), uintSentinel(2, e), uintSentinel(3, e)};
}

rt::Vertex sentinelVertex(std::uint32_t e) {
    rt::Vertex v{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        v.position[i] = floatSentinel(i, e);
        v.normal[i] = floatSentinel(3 + i, e);
    }
    v.uv[0] = floatSentinel(6, e);
    v.uv[1] = floatSentinel(7, e);
    return v;
}

rt::MeshRecord sentinelMeshRecord(std::uint32_t e) {
    return {uintSentinel(0, e), uintSentinel(1, e), uintSentinel(2, e), uintSentinel(3, e)};
}

// FrameUniforms: 52 words; the uint fields are prevValid (27), width .. instanceCount (32-39),
// treeLightCount .. lensing (43-46), sliceIndex and sliceCount (50, 51).
constexpr std::array<bool, 52> kFrameIsUint = [] {
    std::array<bool, 52> isUint{};
    isUint[27] = true;
    for (std::size_t k = 32; k < 40; ++k)
        isUint[k] = true;
    for (std::size_t k = 43; k < 47; ++k)
        isUint[k] = true;
    isUint[50] = isUint[51] = true;
    return isUint;
}();

rt::FrameUniforms sentinelFrame(std::uint32_t e) {
    rt::FrameUniforms f{};
    float* const floats[52] = {&f.cameraPosition[0],
                               &f.cameraPosition[1],
                               &f.cameraPosition[2],
                               &f.tanHalfFovY,
                               &f.cameraRight[0],
                               &f.cameraRight[1],
                               &f.cameraRight[2],
                               &f.aspect,
                               &f.cameraUp[0],
                               &f.cameraUp[1],
                               &f.cameraUp[2],
                               &f.aperture,
                               &f.cameraForward[0],
                               &f.cameraForward[1],
                               &f.cameraForward[2],
                               &f.focusDistance,
                               &f.prevPosition[0],
                               &f.prevPosition[1],
                               &f.prevPosition[2],
                               &f.prevTanHalfFovY,
                               &f.prevRight[0],
                               &f.prevRight[1],
                               &f.prevRight[2],
                               &f.prevAspect,
                               &f.prevUp[0],
                               &f.prevUp[1],
                               &f.prevUp[2],
                               nullptr,
                               &f.prevForward[0],
                               &f.prevForward[1],
                               &f.prevForward[2],
                               &f.pixelSpread,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               &f.clampIndirect,
                               &f.lightWeightSum,
                               &f.regularizeRoughness,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               &f.seconds,
                               &f.shutterOffset,
                               &f.environmentSelect,
                               nullptr,
                               nullptr};
    std::uint32_t* const uints[52] = {nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      nullptr,        &f.prevValid,
                                      nullptr,        nullptr,
                                      nullptr,        nullptr,
                                      &f.width,       &f.height,
                                      &f.frameSeed,   &f.spp,
                                      &f.maxBounces,  &f.flags,
                                      &f.lightCount,  &f.instanceCount,
                                      nullptr,        nullptr,
                                      nullptr,        &f.treeLightCount,
                                      &f.mediumFlags, &f.causticReceivers,
                                      &f.lensing,     nullptr,
                                      nullptr,        nullptr,
                                      &f.sliceIndex,  &f.sliceCount};
    for (std::uint32_t k = 0; k < 52; ++k) {
        if (kFrameIsUint[k])
            *uints[k] = uintSentinel(k, e);
        else
            *floats[k] = floatSentinel(k, e);
    }
    return f;
}

// A struct of N plain 4-byte fields (the step 5 feature structs and Mat34), filled with the sentinels word by
// word: gpu_types.hpp pins every field's offsetof at 4 k for its declaration index k, so word k is field k.
template <typename T, std::size_t N> T sentinelWords(const std::array<bool, N>& isUint, std::uint32_t e) {
    static_assert(sizeof(T) == 4 * N && rt::kGpuMirror<T>);
    std::array<std::uint32_t, N> words{};
    for (std::uint32_t k = 0; k < N; ++k)
        words[k] = isUint[k] ? uintSentinel(k, e) : std::bit_cast<std::uint32_t>(floatSentinel(k, e));
    T value{};
    std::memcpy(&value, words.data(), sizeof value);
    return value;
}

template <std::size_t N> constexpr std::array<bool, N> uintsAt(std::initializer_list<std::size_t> indices) {
    std::array<bool, N> isUint{};
    for (std::size_t k : indices)
        isUint[k] = true;
    return isUint;
}
template <std::size_t N> constexpr std::array<bool, N> allUints() {
    std::array<bool, N> isUint{};
    isUint.fill(true);
    return isUint;
}
constexpr std::array<bool, 12> kMat34IsUint{};
constexpr auto kEnvironmentIsUint = uintsAt<28>({0});
constexpr auto kMediumIsUint = allUints<32>();
constexpr auto kSphereIsUint = uintsAt<12>({7});
constexpr auto kTreeLightIsUint = uintsAt<12>({7, 8, 9, 10, 11});
constexpr auto kTreeNodeIsUint = uintsAt<8>({7});
constexpr auto kLensingIsUint = allUints<8>();
constexpr auto kReceiverIsUint = uintsAt<16>({12, 13, 14, 15});

// The inputs struct_layout.comp hashes with random.glsl's pcgHash; the CPU mirror rt::pcgHash (which
// frameSeedFor uses) must give the same words.
constexpr std::array<std::uint32_t, 8> kHashInputs{0u,          1u,          2u,          48000u,
                                                   0x9E3779B9u, 0x7FFFFFFFu, 0xFFFFFFFFu, 123456789u};

// Appends the expected words of one struct element: word k is a uint or float sentinel per `isUint`.
template <std::size_t N>
void expectWords(std::vector<std::uint32_t>& out, const std::array<bool, N>& isUint, std::uint32_t e) {
    for (std::uint32_t k = 0; k < N; ++k)
        out.push_back(isUint[k] ? uintSentinel(k, e) : std::bit_cast<std::uint32_t>(floatSentinel(k, e)));
}

void testStructLayout(Context& ctx) {
    constexpr std::uint32_t kElements = 2;
    std::vector<rt::Material> materials;
    std::vector<rt::RectLight> lights;
    std::vector<rt::InstanceRecord> instances;
    std::vector<rt::Vertex> vertices;
    std::vector<rt::MeshRecord> records;
    std::vector<rt::FrameUniforms> frames;
    std::vector<rt::Mat34> transforms;
    std::vector<rt::EnvironmentUniforms> environments;
    std::vector<rt::MediumUniforms> media;
    std::vector<rt::SphereRecord> spheres;
    std::vector<rt::TreeLight> treeLights;
    std::vector<rt::LightTreeNode> treeNodes;
    std::vector<rt::LensingUniforms> lenses;
    std::vector<rt::CausticReceiver> receivers;
    for (std::uint32_t e = 0; e < kElements; ++e) {
        transforms.push_back(sentinelWords<rt::Mat34>(kMat34IsUint, e));
        environments.push_back(sentinelWords<rt::EnvironmentUniforms>(kEnvironmentIsUint, e));
        media.push_back(sentinelWords<rt::MediumUniforms>(kMediumIsUint, e));
        spheres.push_back(sentinelWords<rt::SphereRecord>(kSphereIsUint, e));
        treeLights.push_back(sentinelWords<rt::TreeLight>(kTreeLightIsUint, e));
        treeNodes.push_back(sentinelWords<rt::LightTreeNode>(kTreeNodeIsUint, e));
        lenses.push_back(sentinelWords<rt::LensingUniforms>(kLensingIsUint, e));
        receivers.push_back(sentinelWords<rt::CausticReceiver>(kReceiverIsUint, e));
        frames.push_back(sentinelFrame(e));
        materials.push_back(sentinelMaterial(e));
        lights.push_back(sentinelLight(e));
        instances.push_back(sentinelInstance(e));
        vertices.push_back(sentinelVertex(e));
        records.push_back(sentinelMeshRecord(e));
    }

    // The order struct_layout.comp writes: per struct, both elements; then the shared constants.
    std::vector<std::uint32_t> expected;
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kMaterialIsUint, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kLightIsUint, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, std::array<bool, 4>{true, true, true, true}, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, std::array<bool, 8>{}, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, std::array<bool, 4>{true, true, true, true}, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kFrameIsUint, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kMat34IsUint, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kEnvironmentIsUint, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kMediumIsUint, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kSphereIsUint, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kTreeLightIsUint, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kTreeNodeIsUint, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kLensingIsUint, e);
    for (std::uint32_t e = 0; e < kElements; ++e)
        expectWords(expected, kReceiverIsUint, e);
    const std::vector<std::uint32_t> constants{static_cast<std::uint32_t>(rt::MaterialClass::Pbr),
                                               static_cast<std::uint32_t>(rt::MaterialClass::Glass),
                                               static_cast<std::uint32_t>(rt::MaterialClass::Emissive),
                                               static_cast<std::uint32_t>(rt::MaterialClass::Checker),
                                               static_cast<std::uint32_t>(rt::Pattern::None),
                                               static_cast<std::uint32_t>(rt::Pattern::WoodPlanks),
                                               static_cast<std::uint32_t>(rt::Pattern::BrushedMetal),
                                               static_cast<std::uint32_t>(rt::Pattern::Concrete),
                                               static_cast<std::uint32_t>(rt::Pattern::Tesserae),
                                               static_cast<std::uint32_t>(rt::Pattern::TesseraeSphere),
                                               rt::kNoTexture,
                                               rt::kRetroScreenTexture,
                                               rt::kNoLight,
                                               static_cast<std::uint32_t>(rt::LightKind::Emitter),
                                               static_cast<std::uint32_t>(rt::LightKind::RetroScreen),
                                               static_cast<std::uint32_t>(rt::LightKind::Portal),
                                               rt::kTraceFlagNee,
                                               rt::kTraceFlagRussianRoulette,
                                               rt::kNoMedium,
                                               rt::kMaterialCameraOnlyEmission,
                                               rt::kMaterialCausticReceiver,
                                               rt::kMaterialThinWalled,
                                               static_cast<std::uint32_t>(rt::LightKind::Tree),
                                               rt::kInstanceSpheres,
                                               rt::kInstanceDynamicSpheres,
                                               static_cast<std::uint32_t>(rt::GeometryKind::Triangles),
                                               static_cast<std::uint32_t>(rt::GeometryKind::Spheres),
                                               rt::kHitGroupsPerKind,
                                               rt::kNoReceiver};
    expected.insert(expected.end(), constants.begin(), constants.end());
    for (std::uint32_t input : kHashInputs)
        expected.push_back(rt::pcgHash(input));
    CHECK(expected.size() == kElements * (24 + 16 + 4 + 8 + 4 + 52 + 12 + 28 + 32 + 12 + 12 + 8 + 8 + 16) +
                                 29 + kHashInputs.size());
    // The step 5 ABI (Tasks 1 and 1b): sizes and the hit group of each geometry kind.
    static_assert(sizeof(rt::Material) == 96 && sizeof(rt::FrameUniforms) == 208 &&
                  sizeof(rt::EnvironmentUniforms) == 112 && sizeof(rt::MediumUniforms) == 128 &&
                  sizeof(rt::SphereRecord) == 48 && sizeof(rt::TreeLight) == 48 &&
                  sizeof(rt::LightTreeNode) == 32 && sizeof(rt::LensingUniforms) == 32 &&
                  sizeof(rt::CausticReceiver) == 64);
    static_assert(rt::hitGroupFor(rt::MaterialClass::Checker, rt::GeometryKind::Triangles) == 3 &&
                  rt::hitGroupFor(rt::MaterialClass::Pbr, rt::GeometryKind::Spheres) == 4 &&
                  rt::hitGroupFor(rt::MaterialClass::Checker, rt::GeometryKind::Spheres) == 7);
    // New fields default so that step 4's pictures do not change: no flags, no dispersion, no medium, no
    // caustic receiver.
    const rt::Material plain{};
    CHECK(plain.flags == 0 && plain.abbe == 0.f && plain.medium == rt::kNoMedium &&
          plain.receiver == rt::kNoReceiver);
    static_assert(rt::pcgHash(0) == 129708002u); // the reference value of PCG-RXS-M-XS for 0

    const Buffer materialBuffer = upload(ctx, materials, "layout materials");
    const Buffer lightBuffer = upload(ctx, lights, "layout lights");
    const Buffer instanceBuffer = upload(ctx, instances, "layout instances");
    const Buffer vertexBuffer = upload(ctx, vertices, "layout vertices");
    const Buffer recordBuffer = upload(ctx, records, "layout mesh records");
    const Buffer frameBuffer = upload(ctx, frames, "layout frame uniforms");
    const Buffer transformBuffer = upload(ctx, transforms, "layout transforms");
    const Buffer environmentBuffer = upload(ctx, environments, "layout environments");
    const Buffer mediumBuffer = upload(ctx, media, "layout media");
    const Buffer sphereBuffer = upload(ctx, spheres, "layout spheres");
    const Buffer treeLightBuffer = upload(ctx, treeLights, "layout tree lights");
    const Buffer treeNodeBuffer = upload(ctx, treeNodes, "layout tree nodes");
    const Buffer lensBuffer = upload(ctx, lenses, "layout lensing");
    const Buffer receiverBuffer = upload(ctx, receivers, "layout caustic receivers");
    Buffer words = storage(ctx, sizeof(std::uint32_t) * (expected.size() + 8), "layout words");
    std::memset(words.mapped(), 0xEE, words.size());
    words.flush();

    std::vector<Binding> bindings;
    for (std::uint32_t b = 0; b < 15; ++b)
        bindings.push_back({b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    ComputePipeline pipeline(ctx, EVO_TEST_SPIRV_DIR "/test_struct_layout.comp.spv", bindings, 0);
    runOnce(ctx, pipeline,
            {&materialBuffer, &lightBuffer, &instanceBuffer, &vertexBuffer, &recordBuffer, &words,
             &frameBuffer, &transformBuffer, &environmentBuffer, &mediumBuffer, &sphereBuffer,
             &treeLightBuffer, &treeNodeBuffer, &lensBuffer, &receiverBuffer},
            words, nullptr, 0, 1, 1, 1, 1);

    const auto* back = static_cast<const std::uint32_t*>(words.mapped());
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (back[i] != expected[i]) {
            if (mismatches < 8)
                std::fprintf(stderr, "layout word %zu: got 0x%08x, expected 0x%08x\n", i, back[i],
                             expected[i]);
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
    CHECK(back[expected.size()] == 0xEEEEEEEEu); // and nothing past the end
}

// --- BSDF probe --------------------------------------------------------------------------------------------

struct ProbeCase {
    std::uint32_t material;
    float thetaDegrees; // angle of wo from the shading normal; above 90 it is below the surface
};
static_assert(sizeof(ProbeCase) == 8);

struct ProbePush {
    std::uint32_t samplesPerThread, threadsPerCase, seed, caseCount;
};

constexpr std::uint32_t kResultFloats = 17; // must match bsdf_probe.comp

// One case reduced over its threads in a fixed order (double sums, no atomics).
struct CaseResult {
    double samples = 0;
    std::array<double, 3> reflect{}, transmit{}; // mean weight per sample on wo's side and the other side
    double maxWeight = 0, maxPdfError = 0, maxReciprocityError = 0, maxWeightError = 0;
    double nonDelta = 0, delta = 0, rejected = 0, nonFinite = 0, badPdf = 0, maxUnitError = 0;
    bool hasNonDeltaLobe = false; // bsdfHasNonDeltaLobe, the same in every thread
    double mean(int c) const { return reflect[c] + transmit[c]; }
};

std::vector<CaseResult> runProbe(Context& ctx, const std::vector<rt::Material>& materials,
                                 const std::vector<ProbeCase>& cases, std::uint32_t threadsPerCase,
                                 std::uint32_t samplesPerThread, std::uint32_t seed) {
    const Buffer materialBuffer = upload(ctx, materials, "probe materials");
    const Buffer caseBuffer = upload(ctx, cases, "probe cases");
    const std::size_t floats = static_cast<std::size_t>(kResultFloats) * threadsPerCase * cases.size();
    Buffer results = storage(ctx, sizeof(float) * floats, "probe results");

    const std::array<Binding, 3> bindings{{{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                           {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                           {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}}};
    ComputePipeline pipeline(ctx, EVO_TEST_SPIRV_DIR "/test_bsdf_probe.comp.spv", bindings,
                             sizeof(ProbePush));
    const ProbePush push{samplesPerThread, threadsPerCase, seed, static_cast<std::uint32_t>(cases.size())};
    runOnce(ctx, pipeline, {&materialBuffer, &caseBuffer, &results}, results, &push, sizeof(push),
            threadsPerCase, static_cast<std::uint32_t>(cases.size()), 64, 1);

    const auto* r = static_cast<const float*>(results.mapped());
    std::vector<CaseResult> out(cases.size());
    for (std::size_t c = 0; c < cases.size(); ++c) {
        CaseResult& res = out[c];
        for (std::uint32_t t = 0; t < threadsPerCase; ++t) {
            const float* v = r + (c * threadsPerCase + t) * kResultFloats;
            for (int k = 0; k < 3; ++k) {
                res.reflect[k] += v[k];
                res.transmit[k] += v[3 + k];
            }
            res.maxWeight = std::max<double>(res.maxWeight, v[6]);
            res.maxPdfError = std::max<double>(res.maxPdfError, v[7]);
            res.maxReciprocityError = std::max<double>(res.maxReciprocityError, v[8]);
            res.maxWeightError = std::max<double>(res.maxWeightError, v[9]);
            res.nonDelta += v[10];
            res.delta += v[11];
            res.rejected += v[12];
            res.nonFinite += v[13];
            res.badPdf += v[14];
            res.maxUnitError = std::max<double>(res.maxUnitError, v[15]);
            res.hasNonDeltaLobe = v[16] != 0.f;
        }
        res.samples = static_cast<double>(threadsPerCase) * samplesPerThread;
        for (int k = 0; k < 3; ++k) {
            res.reflect[k] /= res.samples;
            res.transmit[k] /= res.samples;
        }
    }
    return out;
}

// Unpolarised Fresnel reflectance of a smooth dielectric; eta = eta_incident / eta_transmitted.
double fresnelDielectric(double cosI, double eta) {
    const double sin2T = eta * eta * (1.0 - cosI * cosI);
    if (sin2T >= 1.0)
        return 1.0;
    const double cosT = std::sqrt(1.0 - sin2T);
    const double rs = (eta * cosI - cosT) / (eta * cosI + cosT);
    const double rp = (cosI - eta * cosT) / (cosI + eta * cosT);
    return 0.5 * (rs * rs + rp * rp);
}

// Directional albedo of single-scattering GGX with F = 1 (a white metal), integrated on a fine (cos theta,
// phi) grid of incoming directions in double precision: the integral of D G2 / (4 cos theta_o) over the
// hemisphere, with Smith's height-correlated G2. An independent reference for the sampler's furnace mean.
double ggxAlbedoReference(double thetaDegrees, double alpha) {
    constexpr int kSteps = 600;
    const double pi = 3.14159265358979323846;
    const double theta = thetaDegrees * pi / 180.0;
    const double wo[3] = {std::sin(theta), 0.0, std::cos(theta)};
    const double a2 = alpha * alpha;
    auto smith = [&](const double* w) { return std::sqrt(w[2] * w[2] + a2 * (w[0] * w[0] + w[1] * w[1])); };
    const double so = smith(wo);
    double sum = 0;
    for (int i = 0; i < kSteps; ++i) {
        const double mu = (i + 0.5) / kSteps;
        const double sinI = std::sqrt(1.0 - mu * mu);
        for (int j = 0; j < 2 * kSteps; ++j) {
            const double phi = (j + 0.5) / (2 * kSteps) * 2.0 * pi;
            const double wi[3] = {sinI * std::cos(phi), sinI * std::sin(phi), mu};
            double h[3] = {wi[0] + wo[0], wi[1] + wo[1], wi[2] + wo[2]};
            const double len = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
            for (double& c : h)
                c /= len;
            const double d = (h[0] * h[0] + h[1] * h[1]) + h[2] * h[2] * a2;
            const double D = a2 / (pi * d * d);
            sum += D * mu / (2.0 * (so * mu + smith(wi) * wo[2]));
        }
    }
    return sum * (1.0 / kSteps) * (2.0 * pi / (2 * kSteps));
}

rt::Material opaque(std::array<float, 3> color, float roughness, float metallic, float ior = 1.5f,
                    float coat = 0.f, float coatRoughness = 0.03f) {
    rt::Material m{};
    std::copy(color.begin(), color.end(), m.baseColor);
    m.roughness = roughness;
    m.metallic = metallic;
    m.ior = ior;
    m.coat = coat;
    m.coatRoughness = coatRoughness;
    return m;
}

rt::Material glass(float ior, float roughness) {
    rt::Material m = opaque({1.f, 1.f, 1.f}, roughness, 0.f, ior);
    m.transmission = 1.f;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Glass);
    return m;
}

// bsdfEval's f cos integrated over the sphere by bsdf_quadrature.comp (thetaSteps x phiSteps, theta-uniform),
// per case and channel; the rows are added here in double.
std::vector<std::array<double, 3>> runQuadrature(Context& ctx, const std::vector<rt::Material>& materials,
                                                 const std::vector<ProbeCase>& cases,
                                                 std::uint32_t thetaSteps, std::uint32_t phiSteps) {
    const Buffer materialBuffer = upload(ctx, materials, "quadrature materials");
    const Buffer caseBuffer = upload(ctx, cases, "quadrature cases");
    Buffer rows = storage(ctx, sizeof(float) * 3 * thetaSteps * cases.size(), "quadrature rows");
    const std::array<Binding, 3> bindings{{{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                           {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                           {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}}};
    struct Push {
        std::uint32_t thetaSteps, phiSteps, caseCount;
    };
    ComputePipeline pipeline(ctx, EVO_TEST_SPIRV_DIR "/test_bsdf_quadrature.comp.spv", bindings,
                             sizeof(Push));
    const Push push{thetaSteps, phiSteps, static_cast<std::uint32_t>(cases.size())};
    runOnce(ctx, pipeline, {&materialBuffer, &caseBuffer, &rows}, rows, &push, sizeof(push), thetaSteps,
            static_cast<std::uint32_t>(cases.size()), 64, 1);
    const auto* r = static_cast<const float*>(rows.mapped());
    std::vector<std::array<double, 3>> out(cases.size());
    for (std::size_t c = 0; c < cases.size(); ++c)
        for (std::uint32_t i = 0; i < thetaSteps; ++i)
            for (int k = 0; k < 3; ++k)
                out[c][k] += r[(c * thetaSteps + i) * 3 + k];
    return out;
}

// An independent check of the samplers (the Task 6 review): bsdfSample's mean weight must equal the
// quadrature of bsdfEval's f cos over the sphere, which uses no sampling code. A sampler whose pdf is not its
// true density (rough glass keeping microfacet samples that land in the wrong hemisphere) biases the mean.
// Rough lobes only (roughness >= 0.3), so the grid resolves them; glass from outside and from inside.
void testSamplerAgainstQuadrature(Context& ctx) {
    rt::Material varnish = opaque({0.5f, 0.3f, 0.15f}, 0.6f, 0.f, 1.5f, 0.6f, 0.3f);
    const std::vector<rt::Material> materials = {glass(1.5f, 0.3f), glass(1.5f, 0.8f),
                                                 opaque({0.8f, 0.1f, 0.1f}, 0.4f, 0.f), varnish};
    const char* names[] = {"rough glass 0.3", "rough glass 0.8", "plastic", "varnish"};
    std::vector<ProbeCase> cases;
    for (std::uint32_t m = 0; m < materials.size(); ++m)
        for (float angle : {0.f, 30.f, 60.f, 75.f, 85.f}) {
            cases.push_back({m, angle});
            if (materials[m].transmission > 0.5f)
                cases.push_back({m, 180.f - angle}); // from inside
        }
    const auto start = std::chrono::steady_clock::now();
    const std::vector<CaseResult> sampled = runProbe(ctx, materials, cases, 256, 1024, 0x5EED);
    const std::vector<std::array<double, 3>> integrated = runQuadrature(ctx, materials, cases, 1500, 1000);
    double worst = 0;
    for (std::size_t c = 0; c < cases.size(); ++c) {
        double error = 0;
        for (int k = 0; k < 3; ++k)
            error = std::max(error, std::abs(sampled[c].mean(k) - integrated[c][k]));
        worst = std::max(worst, error);
        if (error >= 0.01)
            std::printf("  %s at %.0f deg: sampled %.4f, quadrature %.4f\n", names[cases[c].material],
                        cases[c].thetaDegrees, sampled[c].mean(0), integrated[c][0]);
        CHECK(error < 0.01);
    }
    std::printf("sampler vs quadrature: %zu cases, worst difference %.4f (%.1f s)\n", cases.size(), worst,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
}

void describe(const char* what, std::size_t c, const ProbeCase& pc, const CaseResult& r) {
    std::printf(
        "  %s case %zu (material %u, %.3f deg): mean %.4f %.4f %.4f (reflect %.4f), max %.3f, pdf err "
        "%.2e, recip err %.2e, weight err %.2e, delta %.0f, rejected %.0f, non-finite %.0f\n",
        what, c, pc.material, pc.thetaDegrees, r.mean(0), r.mean(1), r.mean(2), r.reflect[0], r.maxWeight,
        r.maxPdfError, r.maxReciprocityError, r.maxWeightError, r.delta, r.rejected, r.nonFinite);
}

// Holds for every case, strict or stress: finite, unit directions, a positive pdf for every non-delta
// sample, and no more energy out than in.
void checkSane(const CaseResult& r) {
    CHECK(r.nonFinite == 0);
    // NEE runs only where bsdfHasNonDeltaLobe says bsdfEval can be non-zero: never false where the sampler
    // produced a non-delta direction.
    if (r.nonDelta > 0)
        CHECK(r.hasNonDeltaLobe);
    CHECK(r.badPdf == 0);
    CHECK(r.maxUnitError < 1e-5);
    for (int k = 0; k < 3; ++k)
        CHECK(r.mean(k) <= 1.01);
    CHECK(r.nonDelta + r.delta + r.rejected == r.samples);
}

void testBsdfs(Context& ctx) {
    enum : std::uint32_t {
        kLambertWhite,
        kMirror,
        kRoughMetal,
        kSmoothGlass,
        kRoughGlass,
        kPlastic,
        kCoated,
        kBrushedMetal,
        kMaterialCount
    };
    std::vector<rt::Material> materials(kMaterialCount);
    // IOR 1 gives F0 = 0: a pure Lambert lobe with no specular layer.
    materials[kLambertWhite] = opaque({1.f, 1.f, 1.f}, 1.f, 0.f, 1.f);
    materials[kMirror] = opaque({1.f, 1.f, 1.f}, 0.f, 1.f);
    materials[kRoughMetal] = opaque({1.f, 1.f, 1.f}, 0.5f, 1.f);
    materials[kSmoothGlass] = glass(1.5f, 0.f);
    materials[kRoughGlass] = glass(1.5f, 0.3f);
    materials[kPlastic] = opaque({0.8f, 0.1f, 0.1f}, 0.4f, 0.f);
    materials[kCoated] = opaque({0.5f, 0.3f, 0.15f}, 0.6f, 0.f, 1.5f, 0.6f, 0.15f); // the desk's varnish
    materials[kBrushedMetal] = opaque({0.9f, 0.6f, 0.3f}, 0.25f, 0.7f, 1.5f, 1.f, 0.05f);

    const std::array<float, 8> angles{0.f, 15.f, 30.f, 45.f, 60.f, 75.f, 85.f, 89.9f};
    std::vector<ProbeCase> cases;
    for (std::uint32_t m = 0; m < kMaterialCount; ++m)
        for (float a : angles)
            cases.push_back({m, a});

    const auto start = std::chrono::steady_clock::now();
    const std::vector<CaseResult> results = runProbe(ctx, materials, cases, 256, 256, 0x1234u); // 65 536 each
    std::printf("material_test: %zu BSDF cases x 65536 samples in %.2f s\n", cases.size(),
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());

    for (std::size_t c = 0; c < cases.size(); ++c) {
        const ProbeCase& pc = cases[c];
        const CaseResult& r = results[c];
        describe("strict", c, pc, r);
        checkSane(r);
        CHECK(r.samples == 65536);
        CHECK(r.maxPdfError < 1e-3);
        CHECK(r.maxWeightError < 1e-3);
        CHECK(r.maxReciprocityError < 1e-4);
        // Only the mirror and smooth glass are delta through and through (no NEE there).
        CHECK(r.hasNonDeltaLobe == (pc.material != kMirror && pc.material != kSmoothGlass));
        switch (pc.material) {
        case kLambertWhite:
            for (int k = 0; k < 3; ++k)
                CHECK(std::abs(r.mean(k) - 1.0) <= 0.01);
            CHECK(r.delta == 0 && r.transmit[0] == 0);
            break;
        case kMirror:
            // Delta: every sample is the mirror direction with weight exactly 1.
            CHECK(r.delta == r.samples);
            for (int k = 0; k < 3; ++k)
                CHECK(r.mean(k) == 1.0);
            CHECK(r.maxWeight == 1.0);
            break;
        case kRoughMetal: {
            // Single-scattering GGX loses energy to the light it scatters more than once, most of it (16 %)
            // near 73 degrees, so the furnace mean is the reference albedo, not 1.
            const double reference = ggxAlbedoReference(pc.thetaDegrees, 0.25);
            for (int k = 0; k < 3; ++k) {
                CHECK(std::abs(r.mean(k) - reference) <= 0.01);
                CHECK(r.mean(k) >= 0.83 && r.mean(k) <= 1.01);
            }
            CHECK(r.delta == 0 && r.transmit[0] == 0);
            break;
        }
        case kSmoothGlass: {
            CHECK(r.delta == r.samples);
            for (int k = 0; k < 3; ++k)
                CHECK(std::abs(r.mean(k) - 1.0) <= 0.01);
            const double fresnel =
                fresnelDielectric(std::cos(pc.thetaDegrees * 3.14159265358979 / 180.0), 1.0 / 1.5);
            CHECK(std::abs(r.reflect[0] - fresnel) <= 0.01);
            break;
        }
        case kRoughGlass:
            CHECK(r.delta == 0);
            CHECK(r.transmit[0] > 0 || pc.thetaDegrees > 89.f);
            CHECK(r.mean(0) > 0.5);
            break;
        default:
            // Plastic, varnish, coated metal: some light comes back, none goes through.
            CHECK(r.mean(0) > 0.05 && r.transmit[0] == 0);
            break;
        }
    }

    // Stress (Review Focus 8): only finiteness, unit directions, positive pdfs and energy are required.
    std::vector<rt::Material> stress{
        glass(1.0001f, 0.f),                                  // smooth, index almost matched
        glass(1.0001f, 0.05f),                                // rough, index almost matched
        glass(1.f, 0.3f),                                     // index matched exactly
        opaque({1.f, 1.f, 1.f}, 0.02f, 1.f),                  // first GGX roughness: alpha clamped
        opaque({1.f, 1.f, 1.f}, 0.0199f, 1.f),                // last delta roughness
        opaque({0.5f, 0.5f, 0.5f}, 0.f, 0.f, 1.5f, 1.f, 0.f), // delta base and delta coat over Lambert
        glass(1.5f, 0.3f),                                    // seen from inside below (total reflection)
        rt::Material{.baseColor = {0.f, 0.f, 0.f},
                     .roughness = 0.f,
                     .ior = 0.f,
                     .coatRoughness = 0.f,
                     .patternScale = 0.f}, // everything zero
    };
    const std::array<float, 8> stressAngles{0.f, 45.f, 89.9f, 89.999f, 90.f, 90.001f, 135.f, 179.9f};
    std::vector<ProbeCase> stressCases;
    for (std::uint32_t m = 0; m < stress.size(); ++m)
        for (float a : stressAngles)
            stressCases.push_back({m, a});
    const std::vector<CaseResult> stressResults = runProbe(ctx, stress, stressCases, 64, 64, 0x5678u);
    for (std::size_t c = 0; c < stressCases.size(); ++c) {
        describe("stress", c, stressCases[c], stressResults[c]);
        checkSane(stressResults[c]);
    }
    // The all-zero material reflects nothing; the index-matched smooth glass lets almost everything through.
    for (std::size_t c = 0; c < stressCases.size(); ++c) {
        if (stressCases[c].material == 7)
            CHECK(stressResults[c].mean(0) == 0.0);
        if (stressCases[c].material == 0 && stressCases[c].thetaDegrees == 0.f)
            CHECK(stressResults[c].transmit[0] > 0.99);
    }
}

} // namespace

int main() {
    try {
        {
            auto ctx = Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the results are checked\n");
            testStructLayout(ctx);
            testBsdfs(ctx);
            testSamplerAgainstQuadrature(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("material_test: %d checks passed\n", checksPassed);
    return 0;
}
