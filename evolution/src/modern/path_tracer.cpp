#include "modern/path_tracer.hpp"

#include "gfx/descriptors.hpp"
#include "gfx/gpu_timers.hpp"
#include "gfx/paths.hpp"
#include "gfx/sync.hpp"
#include "rt/bindings.hpp"
#include "rt/gpu_types.hpp"
#include "rt/random.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace evo::modern {

namespace {

// The set shaders/rt/common.glsl declares (src/rt/bindings.hpp).
const std::array<gfx::Binding, EVO_BINDING_COUNT> kBindings = {
    gfx::Binding{EVO_BINDING_TLAS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR},
    gfx::Binding{EVO_BINDING_LIGHTING, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{EVO_BINDING_EMISSION, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{EVO_BINDING_ALBEDO, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{EVO_BINDING_NORMAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{EVO_BINDING_DEPTH, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{EVO_BINDING_MOTION, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{EVO_BINDING_FRAME, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_MATERIALS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_LIGHTS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_VERTICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_INDICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_MESH_RECORDS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_RETRO_SCREEN, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
    gfx::Binding{EVO_BINDING_ENVIRONMENT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_TREE_NODES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_TREE_LIGHTS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_MEDIUM, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_DENSITY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
    gfx::Binding{EVO_BINDING_MAJORANT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
    gfx::Binding{EVO_BINDING_PREVIOUS_TRANSFORMS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_STATIC_SPHERES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_PARTICLE_SPHERES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_CAUSTIC_MAPS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
    gfx::Binding{EVO_BINDING_LENSING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_VOLUME, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{EVO_BINDING_CAUSTIC_RECEIVERS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{EVO_BINDING_COUNTERS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
};

// pathtrace.rgen's push constants: which of the frame's samples this dispatch traces, and the subpixel
// window they are box-filtered over (its corner relative to the pixel's, and its size; (0, 0) and (1, 1) for
// the whole pixel), whether it writes the guides (one dispatch of the frame does), and the fast raygen's
// shadow-ray roulette level (TraceSettings::neeRoulette; the other builds do not declare it).
struct TracePush {
    std::uint32_t sampleBase, sampleCount;
    float windowOrigin[2], windowSize[2];
    std::uint32_t guides; // non-zero: this dispatch also writes the guides (depth, normal, motion, volume)
    float neeRoulette;
};
static_assert(sizeof(TracePush) == 32);

// The largest struct a feature binding may declare (MediumUniforms): the dummy buffer covers any of them.
constexpr VkDeviceSize kDummyBufferBytes = 256;
static_assert(sizeof(rt::MediumUniforms) <= kDummyBufferBytes &&
              sizeof(rt::EnvironmentUniforms) <= kDummyBufferBytes &&
              sizeof(rt::LensingUniforms) <= kDummyBufferBytes && sizeof(rt::Mat34) <= kDummyBufferBytes &&
              sizeof(rt::SphereRecord) <= kDummyBufferBytes && sizeof(rt::TreeLight) <= kDummyBufferBytes &&
              sizeof(rt::LightTreeNode) <= kDummyBufferBytes &&
              sizeof(rt::CausticReceiver) <= kDummyBufferBytes);

std::atomic<std::uint32_t> gPipelinesCreated{0};

gfx::RtShaders shaders(const TracerOptions& options, std::uint32_t features = rt::kFeatureAll) {
    const std::filesystem::path dir = gfx::shaderDir();
    gfx::RtShaders s;
    s.raygen = dir / (std::string("rt_pathtrace") + (options.fast ? "_fast" : "") +
                      (options.counters ? "_counters" : "") + ".rgen.spv");
    // miss 0: sky, 1: shadow, 2: the end of a shadow ray's walk through thin panes, 3: the end of a shadow
    // ray that passes thin panes through their any-hit (fast mode)
    s.misses = {dir / "rt_sky.rmiss.spv", dir / "rt_shadow.rmiss.spv", dir / "rt_pane.rmiss.spv",
                dir / "rt_shadow_through.rmiss.spv"};
    s.closestHits = {dir / "rt_pbr.rchit.spv", dir / "rt_glass.rchit.spv", dir / "rt_emissive.rchit.spv",
                     dir / "rt_checker.rchit.spv"}; // in MaterialClass order
    // Fast mode: the Glass group's any-hit passes a shadow ray through thin panes (kTraceFlagPaneAnyHit).
    if (options.fast)
        s.anyHits = {{}, dir / "rt_pane.rahit.spv", {}, {}};
    // Hit groups kHitGroupsPerKind.. (hitGroupFor(class, Spheres)): the same closest-hits built for analytic
    // spheres (-DGEOMETRY_SPHERES), behind the sphere intersection shader.
    const std::filesystem::path intersection = dir / "rt_sphere.rint.spv";
    s.proceduralHits = {{dir / "rt_pbr_sphere.rchit.spv", intersection},
                        {dir / "rt_glass_sphere.rchit.spv", intersection},
                        {dir / "rt_emissive_sphere.rchit.spv", intersection},
                        {dir / "rt_checker_sphere.rchit.spv", intersection}};
    if (features != rt::kFeatureAll)
        s.specialization = {features}; // constant_id 0: common.glsl's kFeatures
    return s;
}

// Whether the grid medium's density is read through the sampler's filter (kTraceFlagFilteredGrid): on every
// driver but lavapipe, whose filtered 3D fetch is off and whose pictures are the byte-exact reference.
// EVO_FILTERED_GRID=0 or 1 overrides the choice (a diagnostic).
bool filteredGridFor(const gfx::Context& ctx) {
    if (const char* forced = std::getenv("EVO_FILTERED_GRID"))
        return std::string(forced) == "1";
    return ctx.driverId() != VK_DRIVER_ID_MESA_LLVMPIPE;
}

std::uint32_t checkedSize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0)
        throw std::invalid_argument("PathTracer: empty size");
    return width;
}

// A one-texel image, cleared to zero and ready for sampling in GENERAL: the retro screen of a scene that
// shows none (black), and the stand-ins of the feature images.
gfx::Image clearedTexture(gfx::Context& ctx, VkFormat format, VkImageViewType type, const char* name) {
    gfx::ImageDesc desc{1, 1, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, name};
    desc.viewType = type;
    gfx::Image image(ctx, desc);
    ctx.immediate([&](VkCommandBuffer cmd) { // setup
        image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkClearColorValue black{};
        const VkImageSubresourceRange range = gfx::subresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
        vkCmdClearColorImage(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
        image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    return image;
}

gfx::Buffer zeroedBuffer(gfx::Context& ctx) {
    const std::vector<std::byte> zeros(kDummyBufferBytes);
    return gfx::createDeviceBuffer(
        ctx, {kDummyBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "pt dummy"}, zeros);
}

void store(float out[3], Vec3 v) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

float tanHalfFov(const Camera& camera) {
    return std::tan(camera.fovYDegrees * std::numbers::pi_v<float> / 360.f);
}

void writeBuffer(const gfx::Buffer& buffer, const void* data, std::size_t bytes) {
    if (bytes > 0)
        std::memcpy(buffer.mapped(), data, bytes);
    buffer.flush();
}

// A per-slot buffer holds one region per time slice (kMaxShutterSlices). A storage buffer descriptor's offset
// must be a multiple of minStorageBufferOffsetAlignment, at most 256 by the specification, so regions start
// on 256 bytes.
constexpr VkDeviceSize kRegionAlignment = 256;

// The bytes between two regions of `count` elements (at least one: a buffer of size 0 is invalid).
VkDeviceSize regionStride(std::size_t elementBytes, std::uint32_t count) {
    const VkDeviceSize bytes = VkDeviceSize{elementBytes} * std::max(count, 1u);
    return (bytes + kRegionAlignment - 1) / kRegionAlignment * kRegionAlignment;
}

// The nodes of a light tree of n lights (0 lights: none).
std::uint32_t treeNodeCount(std::uint32_t lights) {
    return lights > 0 ? 2 * lights - 1 : 0;
}

// A host-visible storage buffer of kMaxShutterSlices regions of `count` elements each.
gfx::Buffer slotBuffer(gfx::Context& ctx, std::size_t elementBytes, std::uint32_t count, const char* name) {
    return gfx::Buffer(ctx, {regionStride(elementBytes, count) * kMaxShutterSlices,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, name});
}

// Copies `bytes` to region `slice` of a slotBuffer of regions `stride` bytes apart (no flush: the caller
// flushes the buffer once all regions are written).
void writeRegion(const gfx::Buffer& buffer, VkDeviceSize stride, std::uint32_t slice, const void* data,
                 std::size_t bytes) {
    if (bytes > 0)
        std::memcpy(static_cast<std::byte*>(buffer.mapped()) + stride * slice, data, bytes);
}

// The descriptor range of region `slice` (the elements of a slotBuffer, at least one).
VkDescriptorBufferInfo regionInfo(const gfx::Buffer& buffer, VkDeviceSize stride, std::uint32_t slice,
                                  std::size_t elementBytes, std::uint32_t count) {
    return {buffer.handle(), stride * slice, VkDeviceSize{elementBytes} * std::max(count, 1u)};
}

// Whether the previous frame's instances describe the same objects as this frame's, instance by instance (so
// that previous[i].transform is where instance i was): the same count, mesh, kind and sphere range.
bool describesSameInstances(std::span<const SceneInstance> now, std::span<const SceneInstance> before) {
    if (now.size() != before.size())
        return false;
    for (std::size_t i = 0; i < now.size(); ++i) {
        const SceneInstance& a = now[i];
        const SceneInstance& b = before[i];
        if (a.kind != b.kind || a.dynamicSpheres != b.dynamicSpheres)
            return false;
        if (a.kind == rt::GeometryKind::Triangles
                ? a.mesh != b.mesh
                : a.firstSphere != b.firstSphere || a.sphereCount != b.sphereCount)
            return false;
    }
    return true;
}

} // namespace

std::uint32_t frameSeedFor(double showSeconds, std::uint32_t framesSinceReset) {
    const long long sampleIndex = std::llround(showSeconds * 48000.0);
    const auto index = static_cast<std::uint32_t>(static_cast<unsigned long long>(sampleIndex));
    return rt::pcgHash(index + 0x9E3779B9u * framesSinceReset);
}

TracerCapacity tracerCapacity(const SceneCapacity& scene) {
    return {scene.instances, scene.lights, scene.materials, scene.treeLights};
}

TracerCapacity unite(const TracerCapacity& a, const TracerCapacity& b) {
    return {std::max(a.instances, b.instances), std::max(a.lights, b.lights),
            std::max(a.materials, b.materials), std::max(a.treeLights, b.treeLights)};
}

namespace {

// traceFeatures without the scan of the instances for panes (`panes`: whether there are any).
std::uint32_t frameFeatures(const SceneStatic& scene, const SceneFrame& frame, bool panes) {
    const std::vector<rt::Material>& materials = frame.materials.empty() ? scene.materials : frame.materials;
    std::uint32_t features = panes ? rt::kFeaturePanes : 0u;
    if (frame.lensing)
        features |= rt::kFeatureLensing;
    if (frame.medium && frame.medium->fog)
        features |= rt::kFeatureFog;
    if (frame.medium && frame.medium->grid)
        features |= rt::kFeatureGrid;
    if (frame.caustics)
        features |= rt::kFeatureCaustics;
    if (!frame.treeLights.empty())
        features |= rt::kFeatureTree;
    for (const std::vector<rt::Material>* table : {&materials, &scene.materials})
        for (const rt::Material& m : *table) {
            features |= rt::patternFeature(m.pattern);
            if (m.abbe > 0.f)
                features |= rt::kFeatureDispersion;
            if (m.texture == rt::kRetroScreenTexture)
                features |= rt::kFeatureRetro;
        }
    return features;
}

} // namespace

std::uint32_t traceFeatures(const SceneStatic& scene, const SceneFrame& frame) {
    const std::vector<rt::Material>& materials = frame.materials.empty() ? scene.materials : frame.materials;
    bool panes = false;
    for (const SceneInstance& in : frame.instances) {
        const rt::Material& m = materials[in.material];
        panes = panes || ((m.flags & rt::kMaterialThinWalled) != 0 && m.transmission > 0.5f);
    }
    return frameFeatures(scene, frame, panes);
}

std::uint32_t PathTracer::pipelinesCreated() {
    return gPipelinesCreated.load();
}

PathTracer::PathTracer(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height,
                       const TracerCapacity& capacity, const TracerOptions& options)
    : ctx_(ctx), ring_(ring), options_(options), filteredGrid_(filteredGridFor(ctx)),
      width_(checkedSize(width, height)), height_(height),
      capacity_(unite(capacity, {1, 1, 1})),
      blackScreen_(clearedTexture(ctx, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_VIEW_TYPE_2D, "black screen")),
      pipeline_(ctx, shaders(options), kBindings, sizeof(TracePush)), dummyBuffer_(zeroedBuffer(ctx)),
      dummyDensity_(
          clearedTexture(ctx, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_VIEW_TYPE_3D, "pt dummy density")),
      dummyMajorant_(clearedTexture(ctx, VK_FORMAT_R16_SFLOAT, VK_IMAGE_VIEW_TYPE_3D, "pt dummy majorant")),
      dummyCaustics_(clearedTexture(ctx, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                                    "pt dummy caustics")) {
    ++gPipelinesCreated;
    ringBegin_ = ring.onBegin([this](std::uint32_t) { tracedThisFrame_ = false; });
    for (std::uint32_t slot = 0; slot < gfx::FrameRing::kFramesInFlight; ++slot) {
        uniforms_[slot] = slotBuffer(ctx, sizeof(rt::FrameUniforms), 1, "frame uniforms");
        environments_[slot] = slotBuffer(ctx, sizeof(rt::EnvironmentUniforms), 1, "environment uniforms");
        media_[slot] = slotBuffer(ctx, sizeof(rt::MediumUniforms), 1, "medium uniforms");
        lenses_[slot] = slotBuffer(ctx, sizeof(rt::LensingUniforms), 1, "lensing uniforms");
    }
    FrameBuffers buffers = createFrameBuffers(capacity_);
    instanceRecords_ = std::move(buffers.instanceRecords);
    lights_ = std::move(buffers.lights);
    materials_ = std::move(buffers.materials);
    previousTransforms_ = std::move(buffers.previousTransforms);
    treeNodes_ = std::move(buffers.treeNodes);
    treeLights_ = std::move(buffers.treeLights);
    tlas_ = std::make_unique<rt::Tlas>(ctx_, ring_, capacity_.instances, kMaxShutterSlices);
    if (options_.counters)
        for (gfx::Buffer& buffer : counters_) {
            buffer = gfx::Buffer(ctx, {2 * sizeof(std::uint32_t) * kCounterCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                       true, "work counters"});
            std::memset(buffer.mapped(), 0, buffer.size());
            buffer.flush();
        }
    createImages();
}

void PathTracer::preparePipeline(std::uint32_t features) {
    if (features == rt::kFeatureAll || specialized_.contains(features))
        return;
    auto pipeline = std::make_unique<gfx::RayTracingPipeline>(ctx_, shaders(options_, features), kBindings,
                                                              sizeof(TracePush));
    ++gPipelinesCreated;
    specialized_.emplace(features, std::move(pipeline));
}

gfx::RayTracingPipeline& PathTracer::pipelineFor(std::uint32_t features) {
    // The specialized pipeline with the fewest features that covers the frame's, else the generic one.
    gfx::RayTracingPipeline* best = &pipeline_;
    std::uint32_t bestFeatures = rt::kFeatureAll;
    for (const auto& [set, pipeline] : specialized_)
        if ((features & ~set) == 0 && std::popcount(set) < std::popcount(bestFeatures)) {
            best = pipeline.get();
            bestFeatures = set;
        }
    lastFeatures_ = bestFeatures;
    return *best;
}

gfx::RayTracingPipeline::StackSizes PathTracer::stackSizes() const {
    const auto it = specialized_.find(lastFeatures_);
    return it != specialized_.end() ? it->second->stackSizes() : pipeline_.stackSizes();
}

void PathTracer::gatherCounters(std::uint32_t slot) {
    if (!options_.counters || !countersPending_[slot])
        return;
    const gfx::Buffer& buffer = counters_[slot];
    buffer.invalidate();
    const auto* words = static_cast<const std::uint32_t*>(buffer.mapped());
    for (std::uint32_t k = 0; k < kCounterCount; ++k)
        counterSums_[k] += std::uint64_t{words[2 * k]} | std::uint64_t{words[2 * k + 1]} << 32;
    std::memset(buffer.mapped(), 0, buffer.size());
    buffer.flush();
    countersPending_[slot] = false;
}

Counters PathTracer::takeCounters() {
    for (std::uint32_t slot = 0; slot < gfx::FrameRing::kFramesInFlight; ++slot)
        gatherCounters(slot);
    const Counters sums = counterSums_;
    counterSums_ = {};
    return sums;
}

PathTracer::~PathTracer() = default;

PathTracer::FrameBuffers PathTracer::createFrameBuffers(const TracerCapacity& capacity) const {
    FrameBuffers b;
    for (std::uint32_t slot = 0; slot < gfx::FrameRing::kFramesInFlight; ++slot) {
        b.instanceRecords[slot] =
            slotBuffer(ctx_, sizeof(rt::InstanceRecord), capacity.instances, "instance records");
        b.lights[slot] = slotBuffer(ctx_, sizeof(rt::RectLight), capacity.lights, "rect lights");
        b.materials[slot] = slotBuffer(ctx_, sizeof(rt::Material), capacity.materials, "frame materials");
        b.previousTransforms[slot] =
            slotBuffer(ctx_, sizeof(rt::Mat34), capacity.instances, "previous transforms");
        b.treeNodes[slot] =
            slotBuffer(ctx_, sizeof(rt::LightTreeNode), treeNodeCount(capacity.treeLights), "tree nodes");
        b.treeLights[slot] = slotBuffer(ctx_, sizeof(rt::TreeLight), capacity.treeLights, "tree lights");
    }
    return b;
}

void PathTracer::reserve(const TracerCapacity& capacity) {
    const TracerCapacity grown = unite(capacity_, capacity);
    if (grown.instances == capacity_.instances && grown.lights == capacity_.lights &&
        grown.materials == capacity_.materials && grown.treeLights == capacity_.treeLights)
        return;
    // The frames in flight may still read the TLAS and the slots' buffers; nothing of the frame being
    // recorded (if any) uses them before trace(). A queue idle, not a timeline wait: synchronization
    // validation credits only this kind of wait when the old memory is reused (as in resize()).
    vkQueueWaitIdle(ctx_.queue()); // setup
    // The new buffers and TLAS first: if one cannot be made, nothing has changed.
    std::unique_ptr<rt::Tlas> tlas;
    if (grown.instances != capacity_.instances)
        tlas = std::make_unique<rt::Tlas>(ctx_, ring_, grown.instances, kMaxShutterSlices);
    FrameBuffers buffers = createFrameBuffers(grown);
    instanceRecords_ = std::move(buffers.instanceRecords);
    lights_ = std::move(buffers.lights);
    materials_ = std::move(buffers.materials);
    previousTransforms_ = std::move(buffers.previousTransforms);
    treeNodes_ = std::move(buffers.treeNodes);
    treeLights_ = std::move(buffers.treeLights);
    if (tlas)
        tlas_ = std::move(tlas);
    capacity_ = grown;
}

void PathTracer::createImages() {
    const VkImageUsageFlags usage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto make = [&](VkFormat format, const char* name) {
        return gfx::Image(ctx_, {width_, height_, format, usage, 1, name});
    };
    gbuffer_.lighting = make(VK_FORMAT_R32G32B32A32_SFLOAT, "pt lighting");
    gbuffer_.emission = make(VK_FORMAT_R16G16B16A16_SFLOAT, "pt emission");
    gbuffer_.albedo = make(VK_FORMAT_R16G16B16A16_SFLOAT, "pt albedo");
    gbuffer_.normal = make(VK_FORMAT_R16G16B16A16_SFLOAT, "pt normal");
    gbuffer_.depth = make(VK_FORMAT_R32_SFLOAT, "pt depth");
    gbuffer_.motion = make(VK_FORMAT_R16G16B16A16_SFLOAT, "pt motion");
    gbuffer_.volume = make(VK_FORMAT_R16G16B16A16_SFLOAT, "pt volume");
}

VkDeviceSize PathTracer::imageBytes() const {
    VkDeviceSize bytes = 0;
    for (const gfx::Image* image : {&gbuffer_.lighting, &gbuffer_.emission, &gbuffer_.albedo,
                                    &gbuffer_.normal, &gbuffer_.depth, &gbuffer_.motion, &gbuffer_.volume})
        bytes += image->bytes();
    return bytes;
}

VkDeviceSize PathTracer::bufferBytes() const {
    VkDeviceSize bytes = tlas_->bytes() + pipeline_.sbtBytes() + dummyBuffer_.bytes() + blackScreen_.bytes() +
                         dummyDensity_.bytes() + dummyMajorant_.bytes() + dummyCaustics_.bytes();
    for (const auto& [features, pipeline] : specialized_)
        bytes += pipeline->sbtBytes();
    for (const SlotBuffers* buffers :
         {&uniforms_, &instanceRecords_, &lights_, &materials_, &previousTransforms_, &environments_,
          &treeNodes_, &treeLights_, &media_, &lenses_, &counters_})
        for (const gfx::Buffer& buffer : *buffers)
            bytes += buffer.bytes();
    return bytes;
}

VkDeviceSize PathTracer::scenePipelineBytes() const {
    VkDeviceSize bytes = 0;
    for (const auto& [features, pipeline] : specialized_)
        bytes += pipeline->sbtBytes();
    return bytes;
}

void PathTracer::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0)
        throw std::invalid_argument("PathTracer::resize: empty size");
    if (width == width_ && height == height_)
        return;
    // A queue idle (not a timeline wait): the old images may still be in use, and synchronization validation
    // credits only this kind of wait when their memory is reused.
    vkQueueWaitIdle(ctx_.queue()); // resize
    width_ = width;
    height_ = height;
    createImages();
}

const rt::LightTree& PathTracer::lightTreeFor(const SceneFrame& frame) {
    std::vector<rt::TreeLight> key(frame.treeLights.size());
    std::vector<float> scale(frame.treeLights.size());
    for (std::size_t k = 0; k < key.size(); ++k) {
        const TreeLightFrame& l = frame.treeLights[k];
        rt::TreeLight& t = key[k];
        store(t.center, l.center);
        t.radius = l.radius;
        store(t.emission, l.emission);
        t.sphere = l.sphere;
        scale[k] = l.scale;
    }
    if (key.size() != treeKey_.size() ||
        std::memcmp(key.data(), treeKey_.data(), key.size() * sizeof(rt::TreeLight)) != 0) {
        tree_ = rt::buildLightTree(key);
        treeKey_ = std::move(key);
    }
    rt::refitLightTree(tree_, scale);
    return tree_;
}

void PathTracer::trace(VkCommandBuffer cmd, const SceneGpu& scene, std::span<const SceneFrame> slices,
                       const std::optional<Camera>& previousCamera, const TraceSettings& settings,
                       gfx::Image* retroScreen, const TraceExtras& extras, const SliceCallback& beforeSlice) {
    // Everything that can be refused is checked before anything is written or recorded.
    if (!ring_.recording())
        throw std::logic_error("PathTracer::trace() outside FrameRing::begin()/submit()");
    if (tracedThisFrame_)
        throw std::logic_error(
            "PathTracer::trace(): a second trace in one frame (the first reads this slot's "
            "uniforms, instances and lights when it runs)");
    if (settings.spp == 0)
        throw std::invalid_argument("PathTracer::trace(): spp 0");
    const auto sliceCount = static_cast<std::uint32_t>(slices.size());
    const std::uint32_t dispatches = (settings.spp + kSppPerDispatch - 1) / kSppPerDispatch;
    if (sliceCount == 0 || sliceCount > kMaxShutterSlices)
        throw std::invalid_argument("PathTracer::trace(): " + std::to_string(sliceCount) +
                                    " time slices (1 to " + std::to_string(kMaxShutterSlices) + ")");
    if (sliceCount > dispatches)
        throw std::invalid_argument("PathTracer::trace(): " + std::to_string(sliceCount) +
                                    " time slices for spp " + std::to_string(settings.spp) +
                                    ", which is only " + std::to_string(dispatches) +
                                    " dispatches (one slice per dispatch at most)");
    for (int a = 0; a < 2; ++a) {
        const float footprint = settings.pixelFootprint[a], jitter = settings.pixelJitter[a];
        if (!(footprint > 0.f && footprint <= 1.f) || !(std::fabs(jitter) + 0.5f * footprint <= 0.5f + 1e-6f))
            throw std::invalid_argument("PathTracer::trace(): the subpixel window (jitter " +
                                        std::to_string(jitter) + ", footprint " + std::to_string(footprint) +
                                        ") must lie inside the pixel");
    }
    if (scene.samplesRetro() && retroScreen == nullptr)
        throw std::invalid_argument(
            "PathTracer::trace(): the scene shows the retro screen, but no retro image");
    auto tooMany = [](const char* what, std::size_t count, std::uint32_t most) {
        return std::length_error("PathTracer::trace(): " + std::to_string(count) + " " + what +
                                 ", more than the tracer's capacity of " + std::to_string(most) +
                                 " (reserve() it)");
    };
    for (const SceneFrame& frame : slices) {
        if (frame.instances.size() > capacity_.instances)
            throw tooMany("instances", frame.instances.size(), capacity_.instances);
        if (frame.lights.size() > capacity_.lights)
            throw tooMany("lights", frame.lights.size(), capacity_.lights);
        if (frame.materials.size() > capacity_.materials)
            throw tooMany("materials", frame.materials.size(), capacity_.materials);
        if (frame.treeLights.size() > capacity_.treeLights)
            throw tooMany("tree lights", frame.treeLights.size(), capacity_.treeLights);
        checkSceneFrame(scene.source(), frame);
        const bool dynamicSpheres =
            std::any_of(frame.instances.begin(), frame.instances.end(), [](const SceneInstance& in) {
                return in.kind == rt::GeometryKind::Spheres && in.dynamicSpheres;
            });
        if (dynamicSpheres && (extras.particleSpheres == nullptr || extras.particleBlas == 0))
            throw std::invalid_argument(
                "PathTracer::trace(): the frame places its particle field, but no particle "
                "spheres and BLAS come with it (TraceExtras)");
        // Caustics: caustics need the maps' image, receiver table and photon pass (the maps check the frame in
        // detail; the receivers' materials are checked here too, since the closest-hits read their table).
        if (frame.caustics &&
            (extras.causticMaps == nullptr || extras.causticReceivers == nullptr || !extras.photons))
            throw std::invalid_argument(
                "PathTracer::trace(): the frame has caustics, but no caustic maps, receiver "
                "table and photon pass come with it (TraceExtras)");
        checkCausticFrame(scene.source(), frame, ~0u);
        // Media: a grid medium samples the medium volume's images, which must hold it.
        if (frame.medium && frame.medium->grid) {
            if (extras.density == nullptr || extras.majorant == nullptr)
                throw std::invalid_argument(
                    "PathTracer::trace(): the frame has a grid medium, but no density "
                    "and majorant images come with it (TraceExtras)");
            const std::array<std::uint32_t, 3>& size = frame.medium->grid->size;
            const std::array<std::uint32_t, 3> cells = majorantSize(size);
            const VkExtent2D d = extras.density->extent(), m = extras.majorant->extent();
            if (size[0] > d.width || size[1] > d.height || size[2] > extras.density->depth() ||
                cells[0] > m.width || cells[1] > m.height || cells[2] > extras.majorant->depth())
                throw std::length_error("PathTracer::trace(): the frame's grid medium is larger than its "
                                        "density and majorant images");
        }
    }

    // Each slice's tables, written to its region of this slot's buffers: the ring's begin() waited for the
    // frame that last read them, and the regions keep the slices apart while the GPU has not run any yet.
    const std::uint32_t slot = ring_.slot();
    const VkDeviceSize uniformStride = regionStride(sizeof(rt::FrameUniforms), 1);
    const VkDeviceSize environmentStride = regionStride(sizeof(rt::EnvironmentUniforms), 1);
    const VkDeviceSize mediumStride = regionStride(sizeof(rt::MediumUniforms), 1);
    const VkDeviceSize lensStride = regionStride(sizeof(rt::LensingUniforms), 1);
    const VkDeviceSize instanceStride = regionStride(sizeof(rt::InstanceRecord), capacity_.instances);
    const VkDeviceSize previousStride = regionStride(sizeof(rt::Mat34), capacity_.instances);
    const VkDeviceSize lightStride = regionStride(sizeof(rt::RectLight), capacity_.lights);
    const VkDeviceSize materialStride = regionStride(sizeof(rt::Material), capacity_.materials);
    const VkDeviceSize treeNodeStride =
        regionStride(sizeof(rt::LightTreeNode), treeNodeCount(capacity_.treeLights));
    const VkDeviceSize treeLightStride = regionStride(sizeof(rt::TreeLight), capacity_.treeLights);
    std::vector<std::vector<rt::Instance>> sliceInstances(sliceCount);
    std::uint32_t features = 0; // what the slices need of the pipeline (traceFeatures)
    for (std::uint32_t k = 0; k < sliceCount; ++k) {
        const SceneFrame& frame = slices[k];
        const rt::EnvironmentUniforms environment = packEnvironment(frame.environment);
        const rt::MediumUniforms medium = frame.medium ? packMedium(*frame.medium) : rt::MediumUniforms{};
        const rt::LensingUniforms lens = frame.lensing ? packLensing(*frame.lensing) : rt::LensingUniforms{};
        const std::vector<rt::Material>& materials =
            frame.materials.empty() ? scene.source().materials : frame.materials;
        std::vector<rt::Instance>& instances = sliceInstances[k];
        std::vector<rt::InstanceRecord> records;
        instances.reserve(frame.instances.size());
        records.reserve(frame.instances.size());
        bool panes = false;
        bool glassPanes = true; // every pane a Glass triangle mesh (pane.rahit's)
        for (std::size_t i = 0; i < frame.instances.size(); ++i) {
            const SceneInstance& in = frame.instances[i];
            const auto index = static_cast<std::uint32_t>(i);
            const auto materialClass = static_cast<rt::MaterialClass>(materials[in.material].materialClass);
            const bool pane = (materials[in.material].flags & rt::kMaterialThinWalled) != 0 &&
                              materials[in.material].transmission > 0.5f;
            panes = panes || pane;
            const bool spheres = in.kind == rt::GeometryKind::Spheres;
            if (pane && (spheres || materialClass != rt::MaterialClass::Glass))
                glassPanes = false;
            // A sphere instance's BLAS: the particle field's this frame, or its range's of the static
            // spheres (checkSceneFrame found the range).
            VkDeviceAddress blas = 0;
            if (spheres)
                blas = in.dynamicSpheres ? extras.particleBlas
                                         : scene.spheres()->address(
                                               *scene.spheres()->findRange({in.firstSphere, in.sphereCount}));
            instances.push_back(
                {in.mesh, in.transform, index, rt::hitGroupFor(materialClass, in.kind),
                 pane ? rt::kInstanceMaskPane : rt::kInstanceMaskSolid, blas});
            const std::uint32_t flags = (spheres ? rt::kInstanceSpheres : 0u) |
                                        (spheres && in.dynamicSpheres ? rt::kInstanceDynamicSpheres : 0u);
            records.push_back({spheres ? in.firstSphere : in.mesh, in.material, in.light, flags});
        }
        // Fast mode: shadow rays pass the panes in one trace, through the Glass group's any-hit, when every
        // pane is a Glass mesh of a scene whose BLASes call an any-hit once per triangle; the panes are then
        // not opaque to that ray alone (every other ray is traced opaque).
        const bool paneAnyHit = options_.fast && panes && glassPanes && scene.paneAnyHit();
        if (paneAnyHit)
            for (rt::Instance& instance : instances)
                instance.notOpaque = instance.mask == rt::kInstanceMaskPane;
        // The previous transforms (binding 21): instance i's transform in the previous frame, or its current
        // one (no motion of its own) where the previous frame's instances are not this slice's.
        const bool havePrevious = describesSameInstances(frame.instances, extras.previousInstances);
        std::vector<rt::Mat34> previousTransforms;
        previousTransforms.reserve(frame.instances.size());
        for (std::size_t i = 0; i < frame.instances.size(); ++i)
            previousTransforms.push_back(havePrevious ? extras.previousInstances[i].transform
                                                      : frame.instances[i].transform);

        const CameraBasis basis = cameraBasis(frame.camera);
        const Camera& prev = previousCamera ? *previousCamera : frame.camera;
        const CameraBasis prevBasis = cameraBasis(prev);
        rt::FrameUniforms u{};
        store(u.cameraPosition, frame.camera.eye);
        store(u.cameraRight, basis.right);
        store(u.cameraUp, basis.up);
        store(u.cameraForward, basis.forward);
        u.tanHalfFovY = tanHalfFov(frame.camera);
        u.aspect = static_cast<float>(width_) / static_cast<float>(height_);
        u.aperture = frame.camera.aperture;
        u.focusDistance = frame.camera.focusDistance;
        store(u.prevPosition, prev.eye);
        store(u.prevRight, prevBasis.right);
        store(u.prevUp, prevBasis.up);
        store(u.prevForward, prevBasis.forward);
        u.prevTanHalfFovY = tanHalfFov(prev);
        u.prevAspect = u.aspect;
        u.prevValid = previousCamera ? 1u : 0u;
        u.pixelSpread = std::atan(2.f * u.tanHalfFovY / static_cast<float>(height_));
        u.width = width_;
        u.height = height_;
        u.frameSeed = settings.frameSeed;
        u.spp = settings.spp; // exports stratify their hero wavelengths over it; instanceCount is for a debugger
        u.maxBounces = settings.maxBounces;
        u.flags = (settings.nee ? rt::kTraceFlagNee : 0u) |
                  (settings.russianRoulette ? rt::kTraceFlagRussianRoulette : 0u) |
                  (settings.fixedLightSelection ? rt::kTraceFlagFixedLightSelection : 0u) |
                  (panes ? rt::kTraceFlagThinPanes : 0u) | (filteredGrid_ ? rt::kTraceFlagFilteredGrid : 0u) |
                  (paneAnyHit ? rt::kTraceFlagPaneAnyHit : 0u) |
                  (options_.fast && settings.halfVolume != 0 ? rt::kTraceFlagHalfVolume : 0u) |
                  (options_.fast && settings.halfVolume == 2 ? rt::kTraceFlagHalfVolumeOdd : 0u);
        features |= frameFeatures(scene.source(), frame, panes);
        u.lightCount = static_cast<std::uint32_t>(frame.lights.size());
        u.instanceCount = static_cast<std::uint32_t>(frame.instances.size());
        u.clampIndirect = settings.clampIndirect;
        u.regularizeRoughness = settings.regularizeRoughness;
        u.seconds = settings.seconds;
        for (const rt::RectLight& light : frame.lights)
            u.lightWeightSum += std::max(light.selectWeight, 0.f);
        // the fields, each set by the task that reads it: no tree lights, medium or lensing; the caustic
        // receivers.
        u.environmentSelect = frame.environment.select;
        u.treeLightCount = static_cast<std::uint32_t>(frame.treeLights.size());
        u.mediumFlags = frame.medium ? mediumFlags(*frame.medium) : 0u;
        // Lensing: the lens bends main rays; when its disc is the frame's grid medium, the lens marches that
        // grid along the bent segments itself and the medium integrator leaves it alone.
        u.lensing = frame.lensing ? 1u : 0u;
        if (frame.lensing && frame.lensing->disc)
            u.mediumFlags &= ~kMediumGrid;
        u.causticReceivers =
            frame.caustics ? static_cast<std::uint32_t>(frame.caustics->receivers.size()) : 0u;
        u.shutterOffset = settings.sliceOffsets[k];
        u.sliceIndex = k;
        u.sliceCount = sliceCount;

        writeRegion(uniforms_[slot], uniformStride, k, &u, sizeof u);
        writeRegion(environments_[slot], environmentStride, k, &environment, sizeof environment);
        writeRegion(media_[slot], mediumStride, k, &medium, sizeof medium);
        writeRegion(lenses_[slot], lensStride, k, &lens, sizeof lens);
        writeRegion(instanceRecords_[slot], instanceStride, k, records.data(),
                    records.size() * sizeof(rt::InstanceRecord));
        writeRegion(previousTransforms_[slot], previousStride, k, previousTransforms.data(),
                    previousTransforms.size() * sizeof(rt::Mat34));
        writeRegion(lights_[slot], lightStride, k, frame.lights.data(),
                    frame.lights.size() * sizeof(rt::RectLight));
        if (!frame.materials.empty())
            writeRegion(materials_[slot], materialStride, k, frame.materials.data(),
                        frame.materials.size() * sizeof(rt::Material));
        // The light tree (checkSceneFrame checked its lights, so neither the build nor the refit
        // throws): this slice's, since a light's scale may change within the shutter.
        if (!frame.treeLights.empty()) {
            const rt::LightTree& tree = lightTreeFor(frame);
            writeRegion(treeNodes_[slot], treeNodeStride, k, tree.nodes.data(),
                        tree.nodes.size() * sizeof(rt::LightTreeNode));
            writeRegion(treeLights_[slot], treeLightStride, k, tree.lights.data(),
                        tree.lights.size() * sizeof(rt::TreeLight));
        }
    }
    for (const gfx::Buffer* buffer :
         {&uniforms_[slot], &environments_[slot], &instanceRecords_[slot], &previousTransforms_[slot],
          &lights_[slot], &materials_[slot], &treeNodes_[slot], &treeLights_[slot], &media_[slot],
          &lenses_[slot]})
        buffer->flush();
    tracedThisFrame_ = true; // nothing is refused from here on
    // The pipeline: a scene's own (preparePipeline) when it covers every slice's features, else the generic.
    gfx::RayTracingPipeline& pipeline = pipelineFor(features);
    // The counting build: the slot's last frame has completed (the ring's begin() waited for it).
    gatherCounters(slot);

    gfx::Image& screen = scene.samplesRetro() ? *retroScreen : blackScreen_;
    // The feature images: the frame's, or the stand-ins (already in GENERAL, sampled).
    gfx::Image& density = extras.density ? *extras.density : dummyDensity_;
    gfx::Image& majorant = extras.majorant ? *extras.majorant : dummyMajorant_;
    gfx::Image& caustics = extras.causticMaps ? *extras.causticMaps : dummyCaustics_;
    for (gfx::Image* image : {&screen, &density, &majorant, &caustics})
        image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    auto storageImage = [](std::uint32_t binding, const gfx::Image& image) {
        return gfx::DescriptorWrite::image(binding, {VK_NULL_HANDLE, image.view(), VK_IMAGE_LAYOUT_GENERAL});
    };
    auto sampledImage = [&](std::uint32_t binding, const gfx::Image& image) {
        return gfx::DescriptorWrite::image(
            binding, {ctx_.trilinearSampler(), image.sampledView(), VK_IMAGE_LAYOUT_GENERAL});
    };
    auto storageBuffer = [](std::uint32_t binding, const gfx::Buffer& buffer) {
        return gfx::DescriptorWrite::buffer(binding, {buffer.handle(), 0, VK_WHOLE_SIZE});
    };
    auto extraBuffer = [&](std::uint32_t binding, const gfx::Buffer* buffer) {
        return storageBuffer(binding, buffer ? *buffer : dummyBuffer_);
    };
    auto regionBuffer = [](std::uint32_t binding, const VkDescriptorBufferInfo& info) {
        return gfx::DescriptorWrite::buffer(binding, info);
    };
    const gfx::Buffer& staticSpheres = extras.staticSpheres ? *extras.staticSpheres
                                       : scene.spheres()    ? scene.spheres()->records()
                                                            : dummyBuffer_;
    // The descriptor set of slice k: its regions of this slot's per-slice buffers, the rest shared.
    auto writesFor = [&](std::uint32_t k) {
        const SceneFrame& frame = slices[k];
        const gfx::Buffer& materialTable =
            frame.materials.empty() ? scene.staticMaterials() : materials_[slot];
        const std::array<gfx::DescriptorWrite, EVO_BINDING_COUNT> writes = {
            gfx::DescriptorWrite::accelerationStructure(EVO_BINDING_TLAS, tlas_->handle()),
            storageImage(EVO_BINDING_LIGHTING, gbuffer_.lighting),
            storageImage(EVO_BINDING_EMISSION, gbuffer_.emission),
            storageImage(EVO_BINDING_ALBEDO, gbuffer_.albedo),
            storageImage(EVO_BINDING_NORMAL, gbuffer_.normal),
            storageImage(EVO_BINDING_DEPTH, gbuffer_.depth),
            storageImage(EVO_BINDING_MOTION, gbuffer_.motion),
            regionBuffer(EVO_BINDING_FRAME,
                         regionInfo(uniforms_[slot], uniformStride, k, sizeof(rt::FrameUniforms), 1)),
            frame.materials.empty()
                ? storageBuffer(EVO_BINDING_MATERIALS, materialTable)
                : regionBuffer(EVO_BINDING_MATERIALS, regionInfo(materials_[slot], materialStride, k,
                                                                 sizeof(rt::Material), capacity_.materials)),
            regionBuffer(EVO_BINDING_LIGHTS,
                         regionInfo(lights_[slot], lightStride, k, sizeof(rt::RectLight), capacity_.lights)),
            regionBuffer(EVO_BINDING_INSTANCES, regionInfo(instanceRecords_[slot], instanceStride, k,
                                                           sizeof(rt::InstanceRecord), capacity_.instances)),
            storageBuffer(EVO_BINDING_VERTICES, scene.geometry().vertices()),
            storageBuffer(EVO_BINDING_INDICES, scene.geometry().indices()),
            storageBuffer(EVO_BINDING_MESH_RECORDS, scene.geometry().meshRecords()),
            sampledImage(EVO_BINDING_RETRO_SCREEN, screen),
            extras.environment
                ? storageBuffer(EVO_BINDING_ENVIRONMENT, *extras.environment)
                : regionBuffer(EVO_BINDING_ENVIRONMENT, regionInfo(environments_[slot], environmentStride, k,
                                                                   sizeof(rt::EnvironmentUniforms), 1)),
            // The tree's buffers: the frame's slice (a non-null TraceExtras pointer replaces them, a hook),
            // or the dummy.
            extras.treeNodes || frame.treeLights.empty()
                ? extraBuffer(EVO_BINDING_TREE_NODES, extras.treeNodes)
                : regionBuffer(EVO_BINDING_TREE_NODES,
                               regionInfo(treeNodes_[slot], treeNodeStride, k, sizeof(rt::LightTreeNode),
                                          treeNodeCount(capacity_.treeLights))),
            extras.treeLights || frame.treeLights.empty()
                ? extraBuffer(EVO_BINDING_TREE_LIGHTS, extras.treeLights)
                : regionBuffer(EVO_BINDING_TREE_LIGHTS,
                               regionInfo(treeLights_[slot], treeLightStride, k, sizeof(rt::TreeLight),
                                          capacity_.treeLights)),
            extras.medium ? storageBuffer(EVO_BINDING_MEDIUM, *extras.medium)
                          : regionBuffer(EVO_BINDING_MEDIUM, regionInfo(media_[slot], mediumStride, k,
                                                                        sizeof(rt::MediumUniforms), 1)),
            sampledImage(EVO_BINDING_DENSITY, density),
            sampledImage(EVO_BINDING_MAJORANT, majorant),
            extras.previousTransforms
                ? storageBuffer(EVO_BINDING_PREVIOUS_TRANSFORMS, *extras.previousTransforms)
                : regionBuffer(EVO_BINDING_PREVIOUS_TRANSFORMS,
                               regionInfo(previousTransforms_[slot], previousStride, k, sizeof(rt::Mat34),
                                          capacity_.instances)),
            storageBuffer(EVO_BINDING_STATIC_SPHERES, staticSpheres),
            extraBuffer(EVO_BINDING_PARTICLE_SPHERES, extras.particleSpheres),
            sampledImage(EVO_BINDING_CAUSTIC_MAPS, caustics),
            extras.lensing ? storageBuffer(EVO_BINDING_LENSING, *extras.lensing)
                           : regionBuffer(EVO_BINDING_LENSING, regionInfo(lenses_[slot], lensStride, k,
                                                                          sizeof(rt::LensingUniforms), 1)),
            storageImage(EVO_BINDING_VOLUME, gbuffer_.volume),
            extraBuffer(EVO_BINDING_CAUSTIC_RECEIVERS, extras.causticReceivers),
            storageBuffer(EVO_BINDING_COUNTERS, options_.counters ? counters_[slot] : dummyBuffer_),
        };
        return writes;
    };

    // ceil(spp / kSppPerDispatch) dispatches, split evenly over the slices. lighting, emission and albedo
    // keep a running mean over the frame's samples, so each dispatch reads what the one before wrote; the
    // other guides come from the first dispatch of the middle slice alone. Every dispatch still gets a
    // barrier on all seven: the raygen writes the guides statically, and synchronization validation judges a
    // dispatch by the accesses its pipeline could make, not the ones it makes. With one slice the TLAS build
    // and the trace are timed apart; with several, the builds are part of the "trace" scope (a frame has only
    // so many timer scopes).
    constexpr VkAccessFlags2 kReadWrite =
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    const std::uint32_t guideSlice = sliceCount / 2;
    const bool singleSlice = sliceCount == 1;
    if (!singleSlice && timers_)
        timers_->begin(cmd, "trace");
    std::uint32_t boundSlice = kMaxShutterSlices; // none yet
    bool tracing = false;                         // the single slice's "trace" scope is open
    for (std::uint32_t d = 0; d < dispatches; ++d) {
        const std::uint32_t k = sliceOfDispatch(d, dispatches, sliceCount);
        if (k != boundSlice) {
            if (tracing && timers_) {
                timers_->end(cmd);
                tracing = false;
            }
            if (beforeSlice)
                beforeSlice(cmd, k);
            if (singleSlice && timers_)
                timers_->begin(cmd, "tlas");
            tlas_->build(cmd, scene.blases(), sliceInstances[k]);
            if (singleSlice && timers_)
                timers_->end(cmd);
            // Caustics: the photon pass, once, against the first slice's TLAS and tables (region 0 of this
            // slot's per-slice buffers), before any trace dispatch: the maps are a function of the frame's
            // time, shared by every slice.
            if (d == 0 && slices[0].caustics) {
                const SceneFrame& first = slices[0];
                PhotonScene photons;
                photons.tlas = tlas_->handle();
                photons.instances = &instanceRecords_[slot];
                photons.materials = first.materials.empty() ? &scene.staticMaterials() : &materials_[slot];
                photons.vertices = &scene.geometry().vertices();
                photons.indices = &scene.geometry().indices();
                photons.meshRecords = &scene.geometry().meshRecords();
                photons.staticSpheres = &staticSpheres;
                photons.particleSpheres = extras.particleSpheres ? extras.particleSpheres : &dummyBuffer_;
                photons.lights = &lights_[slot];
                photons.environment = extras.environment ? extras.environment : &environments_[slot];
                if (singleSlice && timers_)
                    timers_->begin(cmd, "photons");
                extras.photons(cmd, photons);
                if (singleSlice && timers_)
                    timers_->end(cmd);
            }
            if (singleSlice && timers_) {
                timers_->begin(cmd, "trace");
                tracing = true;
            }
            pipeline.bind(cmd, ring_, writesFor(k));
            boundSlice = k;
        }
        // The volume layer keeps a running mean too while the frame has a medium.
        for (gfx::Image* image : {&gbuffer_.lighting, &gbuffer_.emission, &gbuffer_.albedo, &gbuffer_.volume})
            image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                              kReadWrite);
        for (gfx::Image* image : {&gbuffer_.normal, &gbuffer_.depth, &gbuffer_.motion})
            image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const bool firstOfSlice = d == 0 || sliceOfDispatch(d - 1, dispatches, sliceCount) != k;
        const TracePush push{d * kSppPerDispatch,
                             std::min(kSppPerDispatch, settings.spp - d * kSppPerDispatch),
                             {0.5f + settings.pixelJitter[0] - 0.5f * settings.pixelFootprint[0],
                              0.5f + settings.pixelJitter[1] - 0.5f * settings.pixelFootprint[1]},
                             {settings.pixelFootprint[0], settings.pixelFootprint[1]},
                             k == guideSlice && firstOfSlice ? 1u : 0u,
                             options_.fast ? settings.neeRoulette : 0.f};
        pipeline.push(cmd, &push, sizeof push);
        pipeline.traceRays(cmd, width_, height_);
    }
    if (timers_ && (tracing || !singleSlice))
        timers_->end(cmd);
    if (options_.counters) {
        gfx::bufferBarrier2(cmd, counters_[slot].handle(), VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                            kReadWrite, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        countersPending_[slot] = true;
    }
    stats_.traceDispatches = dispatches;
}

} // namespace evo::modern
