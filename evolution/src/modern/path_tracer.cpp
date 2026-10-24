#include "modern/path_tracer.hpp"

#include "gfx/descriptors.hpp"
#include "gfx/paths.hpp"
#include "gfx/sync.hpp"
#include "rt/gpu_types.hpp"
#include "rt/random.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>

namespace evo::modern {

namespace {

// The set shaders/rt/common.glsl declares.
enum : std::uint32_t {
    kTlasBinding = 0,
    kLightingBinding,
    kEmissionBinding,
    kAlbedoBinding,
    kNormalBinding,
    kDepthBinding,
    kMotionBinding,
    kFrameBinding,
    kMaterialsBinding,
    kLightsBinding,
    kInstancesBinding,
    kVerticesBinding,
    kIndicesBinding,
    kMeshRecordsBinding,
    kRetroScreenBinding,
};

const std::array<gfx::Binding, 15> kBindings = {
    gfx::Binding{kTlasBinding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR},
    gfx::Binding{kLightingBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{kEmissionBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{kAlbedoBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{kNormalBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{kDepthBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{kMotionBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{kFrameBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{kMaterialsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{kLightsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{kInstancesBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{kVerticesBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{kIndicesBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{kMeshRecordsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{kRetroScreenBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
};

// pathtrace.rgen's push constants: which of the frame's samples this dispatch traces.
struct TracePush {
    std::uint32_t sampleBase, sampleCount;
};

constexpr std::uint32_t kHitGroups = 4; // = MaterialClass

gfx::RtShaders shaders() {
    const std::filesystem::path dir = gfx::shaderDir();
    gfx::RtShaders s;
    s.raygen = dir / "rt_pathtrace.rgen.spv";
    s.misses = {dir / "rt_sky.rmiss.spv", dir / "rt_shadow.rmiss.spv"}; // miss 0: sky, miss 1: shadow
    s.closestHits = {dir / "rt_pbr.rchit.spv", dir / "rt_glass.rchit.spv", dir / "rt_emissive.rchit.spv",
                     dir / "rt_checker.rchit.spv"}; // in MaterialClass order
    return s;
}

const SceneStatic& checked(const SceneStatic& scene, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0)
        throw std::invalid_argument("PathTracer: empty size");
    if (scene.materials.empty())
        throw std::invalid_argument("PathTracer: a scene needs at least one material");
    for (std::size_t i = 0; i < scene.materials.size(); ++i)
        if (scene.materials[i].materialClass >= kHitGroups)
            throw std::invalid_argument("PathTracer: material " + std::to_string(i) + " has class " +
                                        std::to_string(scene.materials[i].materialClass) +
                                        ", which is no hit group");
    return scene;
}

gfx::Buffer materialBuffer(gfx::Context& ctx, const std::vector<rt::Material>& materials) {
    const auto bytes = std::as_bytes(std::span(materials));
    return gfx::createDeviceBuffer(
        ctx, {bytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "materials"}, bytes);
}

// The texture of a scene that shows no retro screen: one black texel, ready for sampling in GENERAL.
gfx::Image blackTexture(gfx::Context& ctx) {
    gfx::Image image(ctx, {1, 1, VK_FORMAT_R8G8B8A8_SRGB,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "black screen"});
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

bool usesRetroScreen(const SceneStatic& scene) {
    return std::any_of(scene.materials.begin(), scene.materials.end(),
                       [](const rt::Material& m) { return m.texture == rt::kRetroScreenTexture; });
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

} // namespace

std::uint32_t frameSeedFor(double showSeconds, std::uint32_t framesSinceReset) {
    const long long sampleIndex = std::llround(showSeconds * 48000.0);
    const auto index = static_cast<std::uint32_t>(static_cast<unsigned long long>(sampleIndex));
    return rt::pcgHash(index + 0x9E3779B9u * framesSinceReset);
}

PathTracer::PathTracer(gfx::Context& ctx, gfx::FrameRing& ring, const SceneStatic& scene, std::uint32_t width,
                       std::uint32_t height)
    : ctx_(ctx), ring_(ring), width_(width), height_(height),
      store_(ctx, checked(scene, width, height).meshes), blases_(ctx, store_),
      materials_(materialBuffer(ctx, scene.materials)), blackScreen_(blackTexture(ctx)),
      pipeline_(ctx, shaders(), kBindings, sizeof(TracePush)), tlas_(ctx, ring, kMaxInstances) {
    meshCount_ = static_cast<std::uint32_t>(scene.meshes.size());
    for (const rt::Material& m : scene.materials)
        materialClasses_.push_back(m.materialClass);
    samplesRetro_ = usesRetroScreen(scene);
    for (std::uint32_t slot = 0; slot < gfx::FrameRing::kFramesInFlight; ++slot) {
        uniforms_[slot] = gfx::Buffer(
            ctx, {sizeof(rt::FrameUniforms), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "frame uniforms"});
        instanceRecords_[slot] =
            gfx::Buffer(ctx, {sizeof(rt::InstanceRecord) * kMaxInstances, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              true, "instance records"});
        lights_[slot] = gfx::Buffer(ctx, {sizeof(rt::RectLight) * kMaxLights,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "rect lights"});
    }
    createImages();
}

PathTracer::~PathTracer() = default;

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

void PathTracer::trace(VkCommandBuffer cmd, const SceneFrame& frame,
                       const std::optional<Camera>& previousCamera, const TraceSettings& settings,
                       gfx::Image& retroScreen) {
    // Everything that can be refused is checked before anything is written or recorded.
    if (!ring_.recording())
        throw std::logic_error("PathTracer::trace() outside FrameRing::begin()/submit()");
    if (settings.spp == 0)
        throw std::invalid_argument("PathTracer::trace(): spp 0");
    if (frame.instances.size() > kMaxInstances)
        throw std::length_error("PathTracer::trace(): " + std::to_string(frame.instances.size()) +
                                " instances, at most " + std::to_string(kMaxInstances));
    if (frame.lights.size() > kMaxLights)
        throw std::length_error("PathTracer::trace(): " + std::to_string(frame.lights.size()) +
                                " lights, at most " + std::to_string(kMaxLights));
    std::vector<rt::Instance> instances;
    std::vector<rt::InstanceRecord> records;
    instances.reserve(frame.instances.size());
    records.reserve(frame.instances.size());
    for (std::size_t i = 0; i < frame.instances.size(); ++i) {
        const SceneInstance& in = frame.instances[i];
        if (in.mesh >= meshCount_ || in.material >= materialClasses_.size() ||
            (in.light != rt::kNoLight && in.light >= frame.lights.size()))
            throw std::out_of_range("PathTracer::trace(): instance " + std::to_string(i) +
                                    " names a mesh, material or light that does not exist");
        const auto index = static_cast<std::uint32_t>(i);
        instances.push_back({in.mesh, in.transform, index, materialClasses_[in.material]});
        records.push_back({in.mesh, in.material, in.light, 0});
    }

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
    u.spp = settings.spp;
    u.maxBounces = settings.maxBounces;
    u.flags = (settings.nee ? rt::kTraceFlagNee : 0u) |
              (settings.russianRoulette ? rt::kTraceFlagRussianRoulette : 0u);
    u.lightCount = static_cast<std::uint32_t>(frame.lights.size());
    u.instanceCount = static_cast<std::uint32_t>(frame.instances.size());
    u.clampIndirect = settings.clampIndirect;
    for (const rt::RectLight& light : frame.lights)
        u.lightWeightSum += std::max(light.selectWeight, 0.f);

    // This slot's buffers: the ring's begin() waited for the frame that last read them.
    const std::uint32_t slot = ring_.slot();
    writeBuffer(uniforms_[slot], &u, sizeof u);
    writeBuffer(instanceRecords_[slot], records.data(), records.size() * sizeof(rt::InstanceRecord));
    writeBuffer(lights_[slot], frame.lights.data(), frame.lights.size() * sizeof(rt::RectLight));

    tlas_.build(cmd, blases_, instances);

    gfx::Image& screen = samplesRetro_ ? retroScreen : blackScreen_;
    screen.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    auto storageImage = [](std::uint32_t binding, const gfx::Image& image) {
        return gfx::DescriptorWrite::image(binding, {VK_NULL_HANDLE, image.view(), VK_IMAGE_LAYOUT_GENERAL});
    };
    auto storageBuffer = [](std::uint32_t binding, const gfx::Buffer& buffer) {
        return gfx::DescriptorWrite::buffer(binding, {buffer.handle(), 0, VK_WHOLE_SIZE});
    };
    const std::array<gfx::DescriptorWrite, 15> writes = {
        gfx::DescriptorWrite::accelerationStructure(kTlasBinding, tlas_.handle()),
        storageImage(kLightingBinding, gbuffer_.lighting),
        storageImage(kEmissionBinding, gbuffer_.emission),
        storageImage(kAlbedoBinding, gbuffer_.albedo),
        storageImage(kNormalBinding, gbuffer_.normal),
        storageImage(kDepthBinding, gbuffer_.depth),
        storageImage(kMotionBinding, gbuffer_.motion),
        storageBuffer(kFrameBinding, uniforms_[slot]),
        storageBuffer(kMaterialsBinding, materials_),
        storageBuffer(kLightsBinding, lights_[slot]),
        storageBuffer(kInstancesBinding, instanceRecords_[slot]),
        storageBuffer(kVerticesBinding, store_.vertices()),
        storageBuffer(kIndicesBinding, store_.indices()),
        storageBuffer(kMeshRecordsBinding, store_.meshRecords()),
        gfx::DescriptorWrite::image(kRetroScreenBinding,
                                    {ctx_.trilinearSampler(), screen.sampledView(), VK_IMAGE_LAYOUT_GENERAL}),
    };
    pipeline_.bind(cmd, ring_, writes);

    // ceil(spp / kSppPerDispatch) dispatches. lighting, emission and albedo keep a running mean, so each
    // dispatch reads what the one before wrote; the other guides come from the first dispatch alone.
    constexpr VkAccessFlags2 kReadWrite =
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    const std::uint32_t dispatches = (settings.spp + kSppPerDispatch - 1) / kSppPerDispatch;
    for (std::uint32_t d = 0; d < dispatches; ++d) {
        for (gfx::Image* image : {&gbuffer_.lighting, &gbuffer_.emission, &gbuffer_.albedo})
            image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                              kReadWrite);
        if (d == 0)
            for (gfx::Image* image : {&gbuffer_.normal, &gbuffer_.depth, &gbuffer_.motion})
                image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL,
                                  VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const TracePush push{d * kSppPerDispatch,
                             std::min(kSppPerDispatch, settings.spp - d * kSppPerDispatch)};
        pipeline_.push(cmd, &push, sizeof push);
        pipeline_.traceRays(cmd, width_, height_);
    }
    stats_.traceDispatches = dispatches;
}

} // namespace evo::modern
