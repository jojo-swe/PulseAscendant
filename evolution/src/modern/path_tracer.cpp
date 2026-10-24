#include "modern/path_tracer.hpp"

#include "gfx/descriptors.hpp"
#include "gfx/paths.hpp"
#include "gfx/sync.hpp"
#include "rt/gpu_types.hpp"
#include "rt/random.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
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

// Equal within a relative 1e-5 (rounding between two ways of computing one value).
bool nearlyEqual(float a, float b) {
    return std::abs(double(a) - double(b)) <= 1e-5 * std::max(std::abs(double(a)), std::abs(double(b)));
}

Vec3 vec3(const float v[3]) {
    return {v[0], v[1], v[2]};
}

// NEE uses a light's emission, area, kind and facing; a BSDF ray that hits the light's instance uses that
// instance's material and facing. Multiple importance sampling weighs the two as estimators of one integral,
// so they must describe the same emitter, or the picture is silently biased (Task 7 review I1). Expects the
// instances' mesh, material and light indices checked.
void checkLights(const SceneFrame& frame, const std::vector<rt::Material>& materials) {
    for (std::size_t i = 0; i < frame.lights.size(); ++i) {
        const rt::RectLight& l = frame.lights[i];
        const std::string what = "PathTracer::trace(): light " + std::to_string(i) + " ";
        if (l.kind > static_cast<std::uint32_t>(rt::LightKind::Portal))
            throw std::invalid_argument(what + "has kind " + std::to_string(l.kind) +
                                        ", which is no LightKind");
        const float area = length(cross(vec3(l.edgeU), vec3(l.edgeV)));
        if (!nearlyEqual(l.area, area))
            throw std::invalid_argument(what + "has area " + std::to_string(l.area) +
                                        ", but |edgeU x edgeV| is " + std::to_string(area));
    }
    for (std::size_t i = 0; i < frame.instances.size(); ++i) {
        const SceneInstance& in = frame.instances[i];
        if (in.light == rt::kNoLight)
            continue;
        const rt::RectLight& l = frame.lights[in.light];
        const rt::Material& m = materials[in.material];
        const std::string what = "PathTracer::trace(): instance " + std::to_string(i) + " is light " +
                                 std::to_string(in.light) + ", ";
        const auto kind = static_cast<rt::LightKind>(l.kind);
        if (kind == rt::LightKind::Portal)
            throw std::invalid_argument(what + "a portal: an opening, not a surface");
        if (linearDeterminant(in.transform) < 0.f)
            throw std::invalid_argument(what + "but its transform mirrors: it emits from the light's back");
        const bool screen = m.texture == rt::kRetroScreenTexture;
        if (kind == rt::LightKind::Emitter) {
            if (screen)
                throw std::invalid_argument(what + "an Emitter, but its material shows the retro screen");
            for (int c = 0; c < 3; ++c)
                if (!nearlyEqual(l.emission[c], m.emission[c] * m.emissionScale))
                    throw std::invalid_argument(what + "whose emission is not its material's emission * "
                                                       "emissionScale");
        } else {
            if (!screen || m.materialClass != static_cast<std::uint32_t>(rt::MaterialClass::Emissive))
                throw std::invalid_argument(what + "a RetroScreen, but its material is no Emissive material "
                                                   "with the retro screen texture");
            for (int c = 0; c < 3; ++c)
                if (!nearlyEqual(l.emission[c], m.emissionScale))
                    throw std::invalid_argument(what +
                                                "a RetroScreen, whose emission is not (s, s, s) for its "
                                                "material's emissionScale s");
        }
    }
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
    sceneMaterials_ = scene.materials;
    samplesRetro_ = usesRetroScreen(scene);
    ringBegin_ = ring.onBegin([this](std::uint32_t) { tracedThisFrame_ = false; });
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
    if (tracedThisFrame_)
        throw std::logic_error(
            "PathTracer::trace(): a second trace in one frame (the first reads this slot's "
            "uniforms, instances and lights when it runs)");
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
        if (in.mesh >= meshCount_ || in.material >= sceneMaterials_.size() ||
            (in.light != rt::kNoLight && in.light >= frame.lights.size()))
            throw std::out_of_range("PathTracer::trace(): instance " + std::to_string(i) +
                                    " names a mesh, material or light that does not exist");
        const auto index = static_cast<std::uint32_t>(i);
        instances.push_back({in.mesh, in.transform, index, sceneMaterials_[in.material].materialClass});
        records.push_back({in.mesh, in.material, in.light, 0});
    }
    checkLights(frame, sceneMaterials_);
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
    // dispatch reads what the one before wrote; the other guides come from the first dispatch alone. Every
    // dispatch still gets a barrier on all six: the raygen writes the guides statically, and synchronization
    // validation judges a dispatch by the accesses its pipeline could make, not the ones it makes.
    constexpr VkAccessFlags2 kReadWrite =
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    const std::uint32_t dispatches = (settings.spp + kSppPerDispatch - 1) / kSppPerDispatch;
    for (std::uint32_t d = 0; d < dispatches; ++d) {
        for (gfx::Image* image : {&gbuffer_.lighting, &gbuffer_.emission, &gbuffer_.albedo})
            image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                              kReadWrite);
        for (gfx::Image* image : {&gbuffer_.normal, &gbuffer_.depth, &gbuffer_.motion})
            image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const TracePush push{d * kSppPerDispatch,
                             std::min(kSppPerDispatch, settings.spp - d * kSppPerDispatch)};
        pipeline_.push(cmd, &push, sizeof push);
        pipeline_.traceRays(cmd, width_, height_);
    }
    stats_.traceDispatches = dispatches;
}

} // namespace evo::modern
