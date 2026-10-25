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
#include "rt/accel.hpp"
#include "rt/geometry.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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
    float clampIndirect = 64.f;  // luminance limit of a contribution after the first bounce
    // Choose lights by their fixed selectWeight alone, as step 4 did, instead of by their importance at each
    // shading point (power, distance, orientation, gate); only for comparisons.
    bool fixedLightSelection = false;
    // The subpixel window (Task 4 of step 5, post::subpixelWindow): every sample of the frame is box-filtered
    // over a window pixelFootprint wide (pixels, per axis, in (0, 1]) centred at the pixel centre plus
    // pixelJitter; it must lie inside the pixel. The default is the whole pixel, step 4's box filter, byte
    // for byte. The guides (depth, normal, motion) stay at the pixel centre.
    std::array<float, 2> pixelJitter{0.f, 0.f};
    std::array<float, 2> pixelFootprint{1.f, 1.f};
    // The frame's chapter-local show time (show::TimelineFrame::localSeconds), FrameUniforms::seconds: what
    // hit-shader patterns animate with (Tasks 18 and 23) and the export shutter offsets from (Task 9).
    float seconds = 0.f;
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
// - volume, RGBA16F: rgb the radiance in-scattered on the primary segment, a its transmittance; (0, 0, 0, 1)
//   without a medium (the tracer writes that until step 5's Task 11).
// Every image is left in GENERAL after the trace's last write (Image tracks it).
struct GBuffer {
    gfx::Image lighting, emission, albedo, normal, depth, motion, volume;
};

// What the path tracer's buffers hold at most: the instances (the TLAS), lights and per-frame materials of
// any frame it traces. The largest over the scenes prepared (ModernStage).
struct TracerCapacity {
    std::uint32_t instances = 0, lights = 0, materials = 0;
};
// The capacity a scene needs.
TracerCapacity tracerCapacity(const SceneCapacity& scene);
// The larger of two capacities, field by field.
TracerCapacity unite(const TracerCapacity& a, const TracerCapacity& b);

// The step 5 feature resources a frame binds besides the scene's own (bindings EVO_BINDING_ENVIRONMENT ..
// EVO_BINDING_LENSING and EVO_BINDING_CAUSTIC_RECEIVERS in src/rt/bindings.hpp): one pointer per resource,
// owned and written by the feature (its task), which must keep it alive and unchanged until the frame
// completes; a buffer written per frame is one of the feature's per-slot buffers. Null binds a one-element
// dummy of the tracer's own. An image is sampled in GENERAL; trace() records its barrier.
//
// Who owns each binding (Task 1b's rule, so that the feature tasks route their data one way):
// - Small per-frame data derived from the SceneFrame alone is packed by trace() into the tracer's own
//   per-slot buffers (the TraceExtras pointer then stays null, or replaces the packed data in a test; the
//   environment does so today: trace() packs SceneFrame::environment, a non-null `environment` replaces it):
//   15 environment (Task 2), 16/17 the light tree (Task 12), 18 MediumUniforms (Task 11), 21 the previous
//   transforms (Task 9) and 25 lensing (Task 25).
// - Resources another object builds and owns come through TraceExtras: 19/20 the density and majorant grids
//   (Task 11's medium volume, baked once in ModernStage), 23 the particle field with its BLAS (ModernStage's
//   rt::ParticleField, Task 8: `particleSpheres` and `particleBlas` of the slot it recorded this frame), 24
//   the caustic maps and 27 the receiver table (Task 13's caustic maps object, whose photon pass reads the
//   same table). 22, the static spheres, are the SceneGpu's own, which trace() binds (a non-null
//   `staticSpheres` replaces them, a hook).
// Per-frame feature resources (particle fields, medium volumes, splat accumulators, caustic maps) live once
// in ModernStage, sized to the largest prepared scene, not per scene.
struct TraceExtras {
    const gfx::Buffer* environment = nullptr;        // 15: replaces the frame's packed environment (a hook)
    const gfx::Buffer* treeNodes = nullptr;          // 16: LightTreeNode[] (Task 12)
    const gfx::Buffer* treeLights = nullptr;         // 17: TreeLight[] (Task 12)
    const gfx::Buffer* medium = nullptr;             // 18: MediumUniforms (Task 11)
    gfx::Image* density = nullptr;                   // 19: 3D RGBA16F density + emission (Task 11)
    gfx::Image* majorant = nullptr;                  // 20: 3D R16F majorant per cell (Task 11)
    const gfx::Buffer* previousTransforms = nullptr; // 21: Mat34[] per instance (Task 9)
    const gfx::Buffer* staticSpheres = nullptr;      // 22: replaces the scene's SphereRecord[] (a hook)
    const gfx::Buffer* particleSpheres = nullptr;    // 23: SphereRecord[], the particle field (Task 8)
    VkDeviceAddress particleBlas = 0;                // the particle field's BLAS (Task 8; no binding)
    gfx::Image* causticMaps = nullptr;               // 24: 2D array RGBA16F irradiance (Task 13)
    const gfx::Buffer* lensing = nullptr;            // 25: LensingUniforms (Task 25)
    const gfx::Buffer* causticReceivers = nullptr;   // 27: CausticReceiver[] (Task 13)
};

// The path tracer every 2026 scene shares (step 5, Task 1): the ray tracing pipeline and its SBT (8 hit
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
    // FrameUniforms, instance records, lights and (when the frame has its own table) materials, then
    // ceil(spp / kSppPerDispatch) trace dispatches (lighting, emission and albedo keep a running mean across
    // them; the other guides come from the first). previousCamera feeds the motion guide (nullopt: motion
    // invalid). retroScreen (R8G8B8A8_SRGB, mipmapped) is sampled through sampledView() in GENERAL when the
    // scene samples the retro screen (else it is ignored and may be null); trace() records the barrier.
    // Throws, before recording anything, std::invalid_argument for spp 0 or a scene that samples the retro
    // screen without a retro image or a frame that places its particle field without extras.particleSpheres
    // and particleBlas, std::length_error for a frame larger than the tracer's capacity, what
    // checkSceneFrame throws, and std::logic_error outside ring.begin()/submit() or for a second trace in one
    // frame. The particle field must have been recorded earlier in this frame (ModernStage does).
    void trace(VkCommandBuffer cmd, const SceneGpu& scene, const SceneFrame& frame,
               const std::optional<Camera>& previousCamera, const TraceSettings& settings,
               gfx::Image* retroScreen, const TraceExtras& extras = {});

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
    // the shader binding table and the feature dummies. A task that adds a buffer to the tracer adds it here
    // (shared_tracer_test (b) compares the sum with the allocator's statistics).
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
    // The per-slot instance, light and material buffers of a capacity.
    struct FrameBuffers {
        SlotBuffers instanceRecords, lights, materials;
    };
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
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> uniforms_, instanceRecords_, lights_, materials_;
    // The frame's environment (SceneFrame::environment packed, binding EVO_BINDING_ENVIRONMENT), per slot.
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> environments_;
    GBuffer gbuffer_;
    // Last: destroyed first, its destructor waits for the ring's frames in flight before anything above goes.
    std::unique_ptr<rt::Tlas> tlas_;
};

} // namespace evo::modern
