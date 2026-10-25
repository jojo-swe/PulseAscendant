#pragma once

// The 2026 side's path tracer: one raygen that loops over bounces with next-event estimation and multiple
// importance sampling against rectangular area lights (emitters, the retro screen, portals onto the sky) and
// the environment; one closest-hit per material class that only evaluates its material into the payload;
// misses for the sky and for shadow rays. Only the raygen traces (recursion depth 1). One PathTracer serves
// every scene (its SceneGpu holds the scene's geometry, BLASes and materials).

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/rt_pipeline.hpp"
#include "modern/scene.hpp"
#include "modern/scene_gpu.hpp"
#include "modern/shutter.hpp"
#include "rt/accel.hpp"
#include "rt/geometry.hpp"
#include "rt/light_tree.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace evo::gfx {
class GpuTimers;
} // namespace evo::gfx

namespace evo::modern {

struct TraceSettings {
    std::uint32_t spp = 1, maxBounces = 4; // maxBounces: scattering events after the primary hit
    std::uint32_t frameSeed = 0;
    bool nee = true;
    bool russianRoulette = true; // from bounce 3
    // the luminance limit of a contribution after a rough (non-delta) vertex: the primary
    // hit's NEE and camera -> delta chain -> light paths are never clamped.
    float clampIndirect = 16.f;
    // Path regularisation: after the first rough vertex every lobe (and the coat) has at least this
    // roughness, in sampling and evaluation alike (MIS stays consistent); 0 turns it off.
    float regularizeRoughness = 0.25f;
    // Choose lights by their fixed selectWeight alone, as step 4 did, instead of by their importance at each
    // shading point (power, distance, orientation, gate); only for comparisons.
    bool fixedLightSelection = false;
    // The subpixel window (post::subpixelWindow): every sample of the frame is box-filtered
    // over a window pixelFootprint wide (pixels, per axis, in (0, 1]) centred at the pixel centre plus
    // pixelJitter; it must lie inside the pixel. The default is the whole pixel, the original box filter, byte
    // for byte. The guides (depth, normal, motion) stay at the pixel centre.
    std::array<float, 2> pixelJitter{0.f, 0.f};
    std::array<float, 2> pixelFootprint{1.f, 1.f};
    // The frame's chapter-local show time (show::TimelineFrame::localSeconds), FrameUniforms::seconds: what
    // hit-shader patterns animate with (Tasks 18 and 23) and the export shutter offsets from.
    float seconds = 0.f;
    // The export shutter: the offset in seconds of slice k from `seconds`
    // (FrameUniforms::shutterOffset), for the slices of a trace() (the slice frames themselves are the
    // caller's). Shaders that animate with the time use seconds + shutterOffset. Unused slices stay 0.
    std::array<float, kMaxShutterSlices> sliceOffsets{};
};

// Samples per trace dispatch: an export at many spp is split so that no dispatch runs long enough to trip a
// GPU watchdog (Windows TDR).
inline constexpr std::uint32_t kSppPerDispatch = 4;

// The frame seed (Global Constraints): pcgHash(sampleIndex + 0x9E3779B9 * framesSinceReset) with sampleIndex
// = llround(showSeconds * 48000), the audio sample clock (mod 2^32). Export passes framesSinceReset 0.
std::uint32_t frameSeedFor(double showSeconds, std::uint32_t framesSinceReset);

// The path tracer's outputs and guides, all at the trace size:
// - lighting, RGBA32F: the mean over the frame's samples of path radiance without primary-hit emission;
// - emission, RGBA16F: the mean primary-hit emission (and the sky on a primary miss);
// - albedo, RGBA16F: the mean primary-hit demodulation albedo (diffuse: baseColor; metal: F0; glass and a
//   miss: 1);
// - normal, RGBA16F: xyz the world shading normal of the primary hit, facing the camera; w its roughness (0
//   on a miss);
// - depth, R32F: the linear view depth of the primary hit (centre ray, unjittered), -1 on a miss;
// - motion, RGBA16F: xy = previous pixel centre - current pixel centre from the previous camera; z = 1 if
//   valid;
// - volume, RGBA16F: rgb the radiance in-scattered on the primary segment, a its transmittance (the mean over
//   the frame's samples; modern/medium.hpp); (0, 0, 0, 1) without a medium.
// Every image is left in GENERAL after the trace's last write (Image tracks it).
struct GBuffer {
    gfx::Image lighting, emission, albedo, normal, depth, motion, volume;
};

// What the path tracer's buffers hold at most: the instances (the TLAS), lights and per-frame materials of
// any frame it traces. The largest over the scenes prepared (ModernStage).
struct TracerCapacity {
    std::uint32_t instances = 0, lights = 0, materials = 0;
    std::uint32_t treeLights = 0; // the light tree's lights (its nodes: 2 treeLights - 1)
};
// The capacity a scene needs.
TracerCapacity tracerCapacity(const SceneCapacity& scene);
// The larger of two capacities, field by field.
TracerCapacity unite(const TracerCapacity& a, const TracerCapacity& b);

// The feature resources a frame binds besides the scene's own (bindings EVO_BINDING_ENVIRONMENT ..
// EVO_BINDING_LENSING and EVO_BINDING_CAUSTIC_RECEIVERS in src/rt/bindings.hpp): one pointer per resource,
// owned and written by the feature (its task), which must keep it alive and unchanged until the frame
// completes; a buffer written per frame is one of the feature's per-slot buffers. Null binds a one-element
// dummy of the tracer's own. An image is sampled in GENERAL; trace() records its barrier.
//
// Who owns each binding (the feature ABI's rule, so that the feature tasks route their data one way):
// - Small per-frame data derived from the SceneFrame alone is packed by trace() into the tracer's own
//   per-slot buffers (the TraceExtras pointer then stays null, or replaces the packed data in a test; the
//   environment does so today: trace() packs SceneFrame::environment, a non-null `environment` replaces it):
//   15 environment, 16/17 the light tree, 18 MediumUniforms, 21 the previous
//   transforms and 25 lensing.
// - Resources another object builds and owns come through TraceExtras: 19/20 the density and majorant grids
//   (the media's medium volume, baked once in ModernStage), 23 the particle field with its BLAS (ModernStage's
//   rt::ParticleField: `particleSpheres` and `particleBlas` of the slot it recorded this frame), 24
//   the caustic maps and 27 the receiver table (the caustics' caustic maps object, whose photon pass reads the
//   same table). 22, the static spheres, are the SceneGpu's own, which trace() binds (a non-null
//   `staticSpheres` replaces them, a hook).
// Per-frame feature resources (particle fields, medium volumes, splat accumulators, caustic maps) live once
// in ModernStage, sized to the largest prepared scene, not per scene.
struct TraceExtras {
    const gfx::Buffer* environment = nullptr; // 15: replaces the frame's packed environment (a hook)
    const gfx::Buffer* treeNodes = nullptr;   // 16: LightTreeNode[]
    const gfx::Buffer* treeLights = nullptr;  // 17: TreeLight[]
    const gfx::Buffer* medium = nullptr;      // 18: replaces the frame's packed MediumUniforms (a hook)
    gfx::Image* density = nullptr;            // 19: 3D RGBA16F density + emission
    gfx::Image* majorant = nullptr;           // 20: 3D R16F majorant per cell
    const gfx::Buffer* previousTransforms = nullptr; // 21: replaces the packed Mat34[] (a hook)
    // The previous frame's instances, while the history continues (empty: none): what trace() packs into
    // binding 21. Instance i of the frame moved from previousInstances[i].transform when both frames have the
    // same count and the same sequence of (mesh, kind, sphere range) and the same dynamic-sphere flags; else
    // (a different scene state, a seek, an instance that appeared) the frame's own transforms are used and
    // only the camera moves the surface in the motion guide.
    std::span<const SceneInstance> previousInstances;
    const gfx::Buffer* staticSpheres = nullptr;    // 22: replaces the scene's SphereRecord[] (a hook)
    const gfx::Buffer* particleSpheres = nullptr;  // 23: SphereRecord[], the particle field
    VkDeviceAddress particleBlas = 0;              // the particle field's BLAS (no binding)
    gfx::Image* causticMaps = nullptr;             // 24: 2D array RGBA16F irradiance
    const gfx::Buffer* lensing = nullptr;          // 25: LensingUniforms
    const gfx::Buffer* causticReceivers = nullptr; // 27: CausticReceiver[]
    // Caustics: recorded after the TLAS build and before the trace dispatches, with the frame's TLAS and the
    // buffers the trace binds (modern::CausticMaps::record shoots the photons there). Required, with
    // causticMaps and causticReceivers, by a frame with caustics.
    std::function<void(VkCommandBuffer, const PhotonScene&)> photons;
};

// The path tracer every 2026 scene shares: the ray tracing pipeline and its SBT (8 hit
// groups: hitGroupFor(MaterialClass, GeometryKind), triangles then analytic spheres; misses: sky, shadow),
// the TLAS and its scratch, the per-slot uniform, instance, light and material buffers, the dummies of the
// feature bindings and the G-buffer. A scene's own GPU side is a SceneGpu, which trace() binds. One trace per
// frame: a frame shows one scene.
class PathTracer {
public:
    // Builds the pipeline once (setup work) and sizes its buffers and TLAS for `capacity`. Throws
    // std::invalid_argument for an empty size.
    PathTracer(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height,
               const TracerCapacity& capacity);
    ~PathTracer();
    PathTracer(const PathTracer&) = delete;
    PathTracer& operator=(const PathTracer&) = delete;

    // Grows the TLAS and the per-slot buffers to hold `capacity` (field by field the larger of it and the
    // current one); waits for the queue to idle first when anything grows (setup, like resize()). Setup:
    // before a frame, or before trace() in the frame being recorded (nothing of this frame uses them yet).
    // Never shrinks. capacity() changes only once the new buffers and TLAS exist: if creating them throws,
    // the tracer keeps its old capacity and stays usable.
    void reserve(const TracerCapacity& capacity);
    // Recreates the images (waits for the queue first: a resize path).
    void resize(std::uint32_t width, std::uint32_t height);

    // Records into the frame being recorded on the ring: the TLAS build over the scene's BLASes, this slot's
    // FrameUniforms, instance records, previous transforms, lights and (when the frame has its own table)
    // materials, then ceil(spp / kSppPerDispatch) trace dispatches (lighting, emission and albedo keep a
    // running mean across them; the other guides come from one dispatch). previousCamera feeds the motion
    // guide (nullopt: motion invalid); extras.previousInstances makes it a per-object motion (see
    // TraceExtras). retroScreen (R8G8B8A8_SRGB, mipmapped) is sampled through sampledView() in GENERAL when
    // the scene samples the retro screen (else it is ignored and may be null); trace() records the barrier.
    //
    // The export shutter: `slices` are the frame at the times of up to kMaxShutterSlices time slices, in time
    // order (shutterSlices(), sliceOffset() and sliceFrame()/sliceSeconds() in shutter.hpp give the times).
    // Each slice has its own TLAS (built after the previous slice's dispatches), uniforms, instances, lights,
    // materials and environment; the frame's dispatches are split evenly over them (sliceOfDispatch()), so
    // the picture is the mean over the slices; the guides (depth, normal, motion, volume) come from the first
    // dispatch of slice slices.size() / 2. beforeSlice(cmd, k), if given, is called while recording just
    // before slice k's TLAS build: the caller's per-slice work that the slice's TLAS and traces must see (the
    // particle field's rebuild at the slice's time; it may be called with cmd outside the tracer's passes and
    // must order itself against the earlier slices' traces, as ParticleField::record does). All slices belong
    // to the same scene (SceneGpu), and each must pass checkSceneFrame.
    //
    // Throws, before recording anything, std::invalid_argument for spp 0, no slices, more than
    // kMaxShutterSlices slices, more slices than dispatches, or a scene that samples the retro screen
    // without a retro image or a slice that places its particle field without extras.particleSpheres and
    // particleBlas or has a grid medium without extras.density and majorant, std::length_error for a slice
    // larger than the tracer's capacity or a grid medium larger than those images, what checkSceneFrame
    // throws, and std::logic_error outside ring.begin()/submit() or for a second trace in one frame. The
    // particle field must have been recorded earlier in this frame (ModernStage does, per slice), and so must
    // a grid medium's bake (ModernStage bakes it once per frame, at the frame's time).
    using SliceCallback = std::function<void(VkCommandBuffer, std::uint32_t)>;
    void trace(VkCommandBuffer cmd, const SceneGpu& scene, std::span<const SceneFrame> slices,
               const std::optional<Camera>& previousCamera, const TraceSettings& settings,
               gfx::Image* retroScreen, const TraceExtras& extras = {},
               const SliceCallback& beforeSlice = {});
    // One slice: the frame at its own time.
    void trace(VkCommandBuffer cmd, const SceneGpu& scene, const SceneFrame& frame,
               const std::optional<Camera>& previousCamera, const TraceSettings& settings,
               gfx::Image* retroScreen, const TraceExtras& extras = {}) {
        trace(cmd, scene, std::span<const SceneFrame>(&frame, 1), previousCamera, settings, retroScreen,
              extras);
    }

    // GPU timer scopes for the TLAS build ("tlas") and the trace dispatches ("trace") of every later trace()
    // (null: none). The timers must outlive the tracer's use of them.
    void setTimers(gfx::GpuTimers* timers) { timers_ = timers; }

    GBuffer& gbuffer() { return gbuffer_; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    const TracerCapacity& capacity() const { return capacity_; }
    // The device memory of the G-buffer's images at the current size, as allocated (VMA).
    VkDeviceSize imageBytes() const;
    // The device memory of everything else the tracer allocates, as allocated (VMA): the TLAS (storage,
    // scratch, instance buffers), the per-slot uniform, environment, instance, light and material buffers,
    // the light tree's per-slot buffers, the shader binding table and the feature dummies. A task that adds a
    // buffer to the tracer adds it here (shared_tracer_test (b) compares the sum with the allocator's
    // statistics).
    VkDeviceSize bufferBytes() const;

    struct Stats {
        std::uint32_t traceDispatches = 0; // in the last trace()
    };
    Stats lastStats() const { return stats_; }

    // How many ray tracing pipelines PathTracers have created in this process (a test hook: one per tracer,
    // whatever the number of scenes it traces).
    static std::uint32_t pipelinesCreated();

private:
    using SlotBuffers = std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight>;
    void createImages();
    // The per-slot instance, light, material and previous transform buffers of a capacity: kMaxShutterSlices
    // regions each, one per time slice (region k at k * the region stride, 256-byte aligned), and the light
    // tree's nodes and lights (the same regions).
    struct FrameBuffers {
        SlotBuffers instanceRecords, lights, materials, previousTransforms, treeNodes, treeLights;
    };
    // The frame's light tree: built from the frame's tree lights when their centres, radii,
    // emissions or spheres change (treeKey_ holds the last ones), else refit for their scales alone. The
    // tree is a pure function of the frame either way (the build is deterministic), so a seek never sees
    // another frame's tree.
    const rt::LightTree& lightTreeFor(const SceneFrame& frame);
    FrameBuffers createFrameBuffers(const TracerCapacity& capacity) const;

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t width_ = 0, height_ = 0;
    TracerCapacity capacity_;
    Stats stats_{};
    bool tracedThisFrame_ = false; // reset by the ring's begin()
    gfx::GpuTimers* timers_ = nullptr;
    gfx::FrameRing::Subscription ringBegin_; // active while ring_ lives

    gfx::Image blackScreen_;
    gfx::RayTracingPipeline pipeline_;
    // The feature bindings' stand-ins: a zeroed buffer, and one-texel images of the right types.
    gfx::Buffer dummyBuffer_;
    gfx::Image dummyDensity_, dummyMajorant_, dummyCaustics_;
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> uniforms_, instanceRecords_, lights_, materials_,
        previousTransforms_;
    // The light tree's nodes and lights (bindings EVO_BINDING_TREE_NODES and EVO_BINDING_TREE_LIGHTS), per
    // slot, and the tree they were last packed from.
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> treeNodes_, treeLights_;
    std::vector<rt::TreeLight> treeKey_;
    rt::LightTree tree_;
    // The frame's environment (SceneFrame::environment packed, binding EVO_BINDING_ENVIRONMENT), per slot.
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> environments_;
    // The frame's media (SceneFrame::medium packed, binding EVO_BINDING_MEDIUM), per slot.
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> media_;
    // The frame's lens (SceneFrame::lensing packed, binding EVO_BINDING_LENSING), per slot.
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> lenses_;
    GBuffer gbuffer_;
    // Last: destroyed first, its destructor waits for the ring's frames in flight before anything above goes.
    std::unique_ptr<rt::Tlas> tlas_;
};

} // namespace evo::modern
