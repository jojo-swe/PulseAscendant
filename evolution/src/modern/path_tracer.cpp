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
#include <cmath>
#include <cstddef>
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
};

// pathtrace.rgen's push constants: which of the frame's samples this dispatch traces, and the subpixel
// window they are box-filtered over (its corner relative to the pixel's, and its size; (0, 0) and (1, 1) for
// the whole pixel).
struct TracePush {
    std::uint32_t sampleBase, sampleCount;
    float windowOrigin[2], windowSize[2];
};
static_assert(sizeof(TracePush) == 24);

// The largest struct a feature binding may declare (MediumUniforms): the dummy buffer covers any of them.
constexpr VkDeviceSize kDummyBufferBytes = 256;
static_assert(sizeof(rt::MediumUniforms) <= kDummyBufferBytes &&
              sizeof(rt::EnvironmentUniforms) <= kDummyBufferBytes &&
              sizeof(rt::LensingUniforms) <= kDummyBufferBytes && sizeof(rt::Mat34) <= kDummyBufferBytes &&
              sizeof(rt::SphereRecord) <= kDummyBufferBytes && sizeof(rt::TreeLight) <= kDummyBufferBytes &&
              sizeof(rt::LightTreeNode) <= kDummyBufferBytes &&
              sizeof(rt::CausticReceiver) <= kDummyBufferBytes);

std::atomic<std::uint32_t> gPipelinesCreated{0};

gfx::RtShaders shaders() {
    const std::filesystem::path dir = gfx::shaderDir();
    gfx::RtShaders s;
    s.raygen = dir / "rt_pathtrace.rgen.spv";
    s.misses = {dir / "rt_sky.rmiss.spv", dir / "rt_shadow.rmiss.spv"}; // miss 0: sky, miss 1: shadow
    s.closestHits = {dir / "rt_pbr.rchit.spv", dir / "rt_glass.rchit.spv", dir / "rt_emissive.rchit.spv",
                     dir / "rt_checker.rchit.spv"}; // in MaterialClass order
    // Hit groups kHitGroupsPerKind.. (hitGroupFor(class, Spheres)): the same closest-hits built for analytic
    // spheres (-DGEOMETRY_SPHERES), behind the sphere intersection shader (Task 8).
    const std::filesystem::path intersection = dir / "rt_sphere.rint.spv";
    s.proceduralHits = {{dir / "rt_pbr_sphere.rchit.spv", intersection},
                        {dir / "rt_glass_sphere.rchit.spv", intersection},
                        {dir / "rt_emissive_sphere.rchit.spv", intersection},
                        {dir / "rt_checker_sphere.rchit.spv", intersection}};
    return s;
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

// A host-visible storage buffer of `count` elements (at least one: a buffer of size 0 is invalid).
gfx::Buffer slotBuffer(gfx::Context& ctx, std::size_t elementBytes, std::uint32_t count, const char* name) {
    return gfx::Buffer(ctx,
                       {elementBytes * std::max(count, 1u), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, name});
}

} // namespace

std::uint32_t frameSeedFor(double showSeconds, std::uint32_t framesSinceReset) {
    const long long sampleIndex = std::llround(showSeconds * 48000.0);
    const auto index = static_cast<std::uint32_t>(static_cast<unsigned long long>(sampleIndex));
    return rt::pcgHash(index + 0x9E3779B9u * framesSinceReset);
}

TracerCapacity tracerCapacity(const SceneCapacity& scene) {
    return {scene.instances, scene.lights, scene.materials};
}

TracerCapacity unite(const TracerCapacity& a, const TracerCapacity& b) {
    return {std::max(a.instances, b.instances), std::max(a.lights, b.lights),
            std::max(a.materials, b.materials)};
}

std::uint32_t PathTracer::pipelinesCreated() {
    return gPipelinesCreated.load();
}

PathTracer::PathTracer(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height,
                       const TracerCapacity& capacity)
    : ctx_(ctx), ring_(ring), width_(checkedSize(width, height)), height_(height),
      capacity_(unite(capacity, {1, 1, 1})),
      blackScreen_(clearedTexture(ctx, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_VIEW_TYPE_2D, "black screen")),
      pipeline_(ctx, shaders(), kBindings, sizeof(TracePush)), dummyBuffer_(zeroedBuffer(ctx)),
      dummyDensity_(
          clearedTexture(ctx, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_VIEW_TYPE_3D, "pt dummy density")),
      dummyMajorant_(clearedTexture(ctx, VK_FORMAT_R16_SFLOAT, VK_IMAGE_VIEW_TYPE_3D, "pt dummy majorant")),
      dummyCaustics_(clearedTexture(ctx, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                                    "pt dummy caustics")) {
    ++gPipelinesCreated;
    ringBegin_ = ring.onBegin([this](std::uint32_t) { tracedThisFrame_ = false; });
    for (std::uint32_t slot = 0; slot < gfx::FrameRing::kFramesInFlight; ++slot) {
        uniforms_[slot] = gfx::Buffer(
            ctx, {sizeof(rt::FrameUniforms), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "frame uniforms"});
        environments_[slot] =
            gfx::Buffer(ctx, {sizeof(rt::EnvironmentUniforms), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                              "environment uniforms"});
        media_[slot] = gfx::Buffer(
            ctx, {sizeof(rt::MediumUniforms), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "medium uniforms"});
    }
    FrameBuffers buffers = createFrameBuffers(capacity_);
    instanceRecords_ = std::move(buffers.instanceRecords);
    lights_ = std::move(buffers.lights);
    materials_ = std::move(buffers.materials);
    tlas_ = std::make_unique<rt::Tlas>(ctx_, ring_, capacity_.instances);
    createImages();
}

PathTracer::~PathTracer() = default;

PathTracer::FrameBuffers PathTracer::createFrameBuffers(const TracerCapacity& capacity) const {
    FrameBuffers b;
    for (std::uint32_t slot = 0; slot < gfx::FrameRing::kFramesInFlight; ++slot) {
        b.instanceRecords[slot] =
            slotBuffer(ctx_, sizeof(rt::InstanceRecord), capacity.instances, "instance records");
        b.lights[slot] = slotBuffer(ctx_, sizeof(rt::RectLight), capacity.lights, "rect lights");
        b.materials[slot] = slotBuffer(ctx_, sizeof(rt::Material), capacity.materials, "frame materials");
    }
    return b;
}

void PathTracer::reserve(const TracerCapacity& capacity) {
    const TracerCapacity grown = unite(capacity_, capacity);
    if (grown.instances == capacity_.instances && grown.lights == capacity_.lights &&
        grown.materials == capacity_.materials)
        return;
    // The frames in flight may still read the TLAS and the slots' buffers; nothing of the frame being
    // recorded (if any) uses them before trace(). A queue idle, not a timeline wait: synchronization
    // validation credits only this kind of wait when the old memory is reused (as in resize()).
    vkQueueWaitIdle(ctx_.queue()); // setup
    // The new buffers and TLAS first: if one cannot be made, nothing has changed.
    std::unique_ptr<rt::Tlas> tlas;
    if (grown.instances != capacity_.instances)
        tlas = std::make_unique<rt::Tlas>(ctx_, ring_, grown.instances);
    FrameBuffers buffers = createFrameBuffers(grown);
    instanceRecords_ = std::move(buffers.instanceRecords);
    lights_ = std::move(buffers.lights);
    materials_ = std::move(buffers.materials);
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
    for (const SlotBuffers* buffers :
         {&uniforms_, &instanceRecords_, &lights_, &materials_, &environments_, &media_})
        for (const gfx::Buffer& buffer : *buffers)
            bytes += buffer.bytes();
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

void PathTracer::trace(VkCommandBuffer cmd, const SceneGpu& scene, const SceneFrame& frame,
                       const std::optional<Camera>& previousCamera, const TraceSettings& settings,
                       gfx::Image* retroScreen, const TraceExtras& extras) {
    // Everything that can be refused is checked before anything is written or recorded.
    if (!ring_.recording())
        throw std::logic_error("PathTracer::trace() outside FrameRing::begin()/submit()");
    if (tracedThisFrame_)
        throw std::logic_error(
            "PathTracer::trace(): a second trace in one frame (the first reads this slot's "
            "uniforms, instances and lights when it runs)");
    if (settings.spp == 0)
        throw std::invalid_argument("PathTracer::trace(): spp 0");
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
    if (frame.instances.size() > capacity_.instances)
        throw tooMany("instances", frame.instances.size(), capacity_.instances);
    if (frame.lights.size() > capacity_.lights)
        throw tooMany("lights", frame.lights.size(), capacity_.lights);
    if (frame.materials.size() > capacity_.materials)
        throw tooMany("materials", frame.materials.size(), capacity_.materials);
    checkSceneFrame(scene.source(), frame);
    const bool dynamicSpheres =
        std::any_of(frame.instances.begin(), frame.instances.end(), [](const SceneInstance& in) {
            return in.kind == rt::GeometryKind::Spheres && in.dynamicSpheres;
        });
    if (dynamicSpheres && (extras.particleSpheres == nullptr || extras.particleBlas == 0))
        throw std::invalid_argument(
            "PathTracer::trace(): the frame places its particle field, but no particle "
            "spheres and BLAS come with it (TraceExtras)");
    const rt::EnvironmentUniforms environment = packEnvironment(frame.environment);
    // The frame's media (Task 11): a grid samples the medium volume's images, which must hold it.
    const rt::MediumUniforms medium = frame.medium ? packMedium(*frame.medium) : rt::MediumUniforms{};
    const std::uint32_t mediaFlags = frame.medium ? mediumFlags(*frame.medium) : 0u;
    if (frame.medium && frame.medium->grid) {
        if (extras.density == nullptr || extras.majorant == nullptr)
            throw std::invalid_argument(
                "PathTracer::trace(): the frame has a grid medium, but no density and "
                "majorant images come with it (TraceExtras)");
        const std::array<std::uint32_t, 3>& size = frame.medium->grid->size;
        const std::array<std::uint32_t, 3> cells = majorantSize(size);
        const VkExtent2D d = extras.density->extent(), m = extras.majorant->extent();
        if (size[0] > d.width || size[1] > d.height || size[2] > extras.density->depth() ||
            cells[0] > m.width || cells[1] > m.height || cells[2] > extras.majorant->depth())
            throw std::length_error("PathTracer::trace(): the frame's grid medium is larger than its density "
                                    "and majorant images");
    }
    const std::vector<rt::Material>& materials =
        frame.materials.empty() ? scene.source().materials : frame.materials;
    std::vector<rt::Instance> instances;
    std::vector<rt::InstanceRecord> records;
    instances.reserve(frame.instances.size());
    records.reserve(frame.instances.size());
    for (std::size_t i = 0; i < frame.instances.size(); ++i) {
        const SceneInstance& in = frame.instances[i];
        const auto index = static_cast<std::uint32_t>(i);
        const auto materialClass = static_cast<rt::MaterialClass>(materials[in.material].materialClass);
        const bool spheres = in.kind == rt::GeometryKind::Spheres;
        // A sphere instance's BLAS: the particle field's this frame, or its range's of the static spheres
        // (checkSceneFrame found the range).
        VkDeviceAddress blas = 0;
        if (spheres)
            blas =
                in.dynamicSpheres
                    ? extras.particleBlas
                    : scene.spheres()->address(*scene.spheres()->findRange({in.firstSphere, in.sphereCount}));
        instances.push_back(
            {in.mesh, in.transform, index, rt::hitGroupFor(materialClass, in.kind), 0xFF, blas});
        const std::uint32_t flags = (spheres ? rt::kInstanceSpheres : 0u) |
                                    (spheres && in.dynamicSpheres ? rt::kInstanceDynamicSpheres : 0u);
        records.push_back({spheres ? in.firstSphere : in.mesh, in.material, in.light, flags});
    }
    tracedThisFrame_ = true; // nothing is refused from here on

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
    u.spp = settings.spp; // spp and instanceCount: not read by the shaders (for a debugger's eyes)
    u.maxBounces = settings.maxBounces;
    u.flags = (settings.nee ? rt::kTraceFlagNee : 0u) |
              (settings.russianRoulette ? rt::kTraceFlagRussianRoulette : 0u) |
              (settings.fixedLightSelection ? rt::kTraceFlagFixedLightSelection : 0u);
    u.lightCount = static_cast<std::uint32_t>(frame.lights.size());
    u.instanceCount = static_cast<std::uint32_t>(frame.instances.size());
    u.clampIndirect = settings.clampIndirect;
    u.seconds = settings.seconds;
    for (const rt::RectLight& light : frame.lights)
        u.lightWeightSum += std::max(light.selectWeight, 0.f);
    // Step 5's fields, each set by the task that reads it: one time slice at the frame's time, no
    // regularisation, no tree lights, medium, caustics or lensing.
    u.environmentSelect = frame.environment.select;
    u.mediumFlags = mediaFlags;
    u.sliceIndex = 0;
    u.sliceCount = 1;

    // This slot's buffers: the ring's begin() waited for the frame that last read them.
    const std::uint32_t slot = ring_.slot();
    writeBuffer(uniforms_[slot], &u, sizeof u);
    writeBuffer(environments_[slot], &environment, sizeof environment);
    writeBuffer(media_[slot], &medium, sizeof medium);
    writeBuffer(instanceRecords_[slot], records.data(), records.size() * sizeof(rt::InstanceRecord));
    writeBuffer(lights_[slot], frame.lights.data(), frame.lights.size() * sizeof(rt::RectLight));
    if (!frame.materials.empty())
        writeBuffer(materials_[slot], frame.materials.data(), frame.materials.size() * sizeof(rt::Material));
    const gfx::Buffer& materialTable = frame.materials.empty() ? scene.staticMaterials() : materials_[slot];

    if (timers_)
        timers_->begin(cmd, "tlas");
    tlas_->build(cmd, scene.blases(), instances);
    if (timers_)
        timers_->end(cmd);

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
    const std::array<gfx::DescriptorWrite, EVO_BINDING_COUNT> writes = {
        gfx::DescriptorWrite::accelerationStructure(EVO_BINDING_TLAS, tlas_->handle()),
        storageImage(EVO_BINDING_LIGHTING, gbuffer_.lighting),
        storageImage(EVO_BINDING_EMISSION, gbuffer_.emission),
        storageImage(EVO_BINDING_ALBEDO, gbuffer_.albedo),
        storageImage(EVO_BINDING_NORMAL, gbuffer_.normal),
        storageImage(EVO_BINDING_DEPTH, gbuffer_.depth),
        storageImage(EVO_BINDING_MOTION, gbuffer_.motion),
        storageBuffer(EVO_BINDING_FRAME, uniforms_[slot]),
        storageBuffer(EVO_BINDING_MATERIALS, materialTable),
        storageBuffer(EVO_BINDING_LIGHTS, lights_[slot]),
        storageBuffer(EVO_BINDING_INSTANCES, instanceRecords_[slot]),
        storageBuffer(EVO_BINDING_VERTICES, scene.geometry().vertices()),
        storageBuffer(EVO_BINDING_INDICES, scene.geometry().indices()),
        storageBuffer(EVO_BINDING_MESH_RECORDS, scene.geometry().meshRecords()),
        sampledImage(EVO_BINDING_RETRO_SCREEN, screen),
        storageBuffer(EVO_BINDING_ENVIRONMENT,
                      extras.environment ? *extras.environment : environments_[slot]),
        extraBuffer(EVO_BINDING_TREE_NODES, extras.treeNodes),
        extraBuffer(EVO_BINDING_TREE_LIGHTS, extras.treeLights),
        storageBuffer(EVO_BINDING_MEDIUM, extras.medium ? *extras.medium : media_[slot]),
        sampledImage(EVO_BINDING_DENSITY, density),
        sampledImage(EVO_BINDING_MAJORANT, majorant),
        extraBuffer(EVO_BINDING_PREVIOUS_TRANSFORMS, extras.previousTransforms),
        storageBuffer(EVO_BINDING_STATIC_SPHERES, extras.staticSpheres ? *extras.staticSpheres
                                                  : scene.spheres()    ? scene.spheres()->records()
                                                                       : dummyBuffer_),
        extraBuffer(EVO_BINDING_PARTICLE_SPHERES, extras.particleSpheres),
        sampledImage(EVO_BINDING_CAUSTIC_MAPS, caustics),
        extraBuffer(EVO_BINDING_LENSING, extras.lensing),
        storageImage(EVO_BINDING_VOLUME, gbuffer_.volume),
        extraBuffer(EVO_BINDING_CAUSTIC_RECEIVERS, extras.causticReceivers),
    };
    pipeline_.bind(cmd, ring_, writes);

    // ceil(spp / kSppPerDispatch) dispatches. lighting, emission and albedo keep a running mean, so each
    // dispatch reads what the one before wrote (the volume layer too, with a medium); the other guides come
    // from the first dispatch alone. Every dispatch still gets a barrier on all seven: the raygen writes the
    // guides statically, and synchronization validation judges a dispatch by the accesses its pipeline could
    // make, not the ones it makes.
    constexpr VkAccessFlags2 kReadWrite =
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    const std::uint32_t dispatches = (settings.spp + kSppPerDispatch - 1) / kSppPerDispatch;
    if (timers_)
        timers_->begin(cmd, "trace");
    for (std::uint32_t d = 0; d < dispatches; ++d) {
        // The volume layer keeps a running mean too while the frame has a medium (Task 11).
        for (gfx::Image* image : {&gbuffer_.lighting, &gbuffer_.emission, &gbuffer_.albedo, &gbuffer_.volume})
            image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                              kReadWrite);
        for (gfx::Image* image : {&gbuffer_.normal, &gbuffer_.depth, &gbuffer_.motion})
            image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const TracePush push{d * kSppPerDispatch,
                             std::min(kSppPerDispatch, settings.spp - d * kSppPerDispatch),
                             {0.5f + settings.pixelJitter[0] - 0.5f * settings.pixelFootprint[0],
                              0.5f + settings.pixelJitter[1] - 0.5f * settings.pixelFootprint[1]},
                             {settings.pixelFootprint[0], settings.pixelFootprint[1]}};
        pipeline_.push(cmd, &push, sizeof push);
        pipeline_.traceRays(cmd, width_, height_);
    }
    if (timers_)
        timers_->end(cmd);
    stats_.traceDispatches = dispatches;
}

} // namespace evo::modern
