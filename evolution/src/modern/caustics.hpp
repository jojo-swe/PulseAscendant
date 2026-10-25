#pragma once

// Caustic photon maps on receiver surfaces (caustic photon maps; Decision 20: biased but deterministic).
//
// A frame with caustics (SceneFrame::caustics) shoots `photons` photons from one light (a rect Emitter, or
// the environment's disc) at the bounding sphere of its caster instances. A photon must hit a smooth (delta)
// dielectric or metal first; it makes up to kMaxPhotonDeltaEvents delta events (Fresnel picks reflection or
// refraction; a dispersive dielectric, Material::abbe > 0, collapses it to one hero wavelength carrying
// spectralRgb's weight, shaders/rt/spectral.glsl) and, on reaching a kMaterialCausticReceiver surface after
// at least one, adds its flux, through the Fresnel transmission of the receiver's Lambert lobe, bilinearly
// into 2x2 texels of that receiver's map, in fixed point with 32-bit integer atomics (Decision 4: the sum is
// order independent). A resolve pass turns the sums into irradiance (with a 3x3 tent, which keeps the
// energy), RGBA16F, one array layer per receiver (binding EVO_BINDING_CAUSTIC_MAPS). Every photon is a pure
// function of (its index, the seed): the map is a pure function of the show time, nothing carried over.
//
// The path tracer renders it (shaders/rt/caustics.glsl): a receiver adds the radiance its Lambert lobe
// reflects under the map's irradiance (albedo / pi * E, with the lobe's Fresnel factors) to its emission, so
// the primary hit's caustic bypasses the denoiser and stays sharp. The raygen excludes the paths the map
// already holds (shaders/rt/caustic_paths.glsl): light of the photons' light reached from a receiver's
// non-delta bounce through 1..kMaxPhotonDeltaEvents delta surfaces, inside the photons' beam, loses the
// receiver's Lambert share. Without that the caustic would be counted twice.

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "modern/math.hpp"
#include "rt/gpu_types.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <vector>

namespace evo::modern {

struct SceneStatic;
struct SceneFrame;

inline constexpr std::uint32_t kMaxCausticReceivers = 2;  // array layers of the maps
inline constexpr std::uint32_t kMaxPhotonDeltaEvents = 10; // per photon, and per excluded camera path
inline constexpr std::uint32_t kExportPhotonFactor = 4;   // photons x this at >= kExportPhotonSpp spp
inline constexpr std::uint32_t kExportPhotonSpp = 16;
// Such exports also resolve each receiver at this times its resolution (and chroma radius) where the maps
// hold it: 2048^2 for a scene that asks for 1024^2 and has the capacity.
inline constexpr std::uint32_t kExportMapFactor = 2;
inline constexpr std::uint32_t kMaxChromaRadius = 32; // CausticReceiver::chromaRadius, in texels

// A receiver: worldToUv's rows 0 and 1 map a world point of the receiver's plane to its map's uv in
// [0, 1]^2 (row 2 is free; receiverForQuad puts the distance to the plane there). The plane must contain
// the directions of rows 0 and 1 (its normal is row 0 x row 1), as receiverForQuad makes it: the world area
// of the uv square is then 1 / |row 0 x row 1|. The map holds resolution^2 texels.
//
// chromaRadius (texels; 0: off) turns on the resolve's chroma filter (shaders/modern/caustic_resolve.comp):
// the tent's luminance with the colour of a window about 2 chromaRadius + 1 texels wide, for the rainbows of
// dispersive glass, whose single-wavelength photons leave hue noise long after the brightness has converged.
struct CausticReceiver {
    rt::Mat34 worldToUv;
    std::uint32_t resolution = 1024;
    std::uint32_t chromaRadius = 0;
};

// The receiver of a modern::quad(width, height) placed by `transform`: u along the quad's x, v along its y,
// (0, 0) at its (-x, -y) corner. Throws std::invalid_argument for a degenerate transform or size.
CausticReceiver receiverForQuad(const rt::Mat34& transform, float width, float height,
                                std::uint32_t resolution);

// What a frame's caustics are made of. Materials name their receiver: Material::flags has
// kMaterialCausticReceiver and Material::receiver indexes `receivers`.
struct CausticFrame {
    std::vector<CausticReceiver> receivers; // at most kMaxCausticReceivers
    std::uint32_t photons = 1u << 20;       // at the scene's detail; 0: empty maps (the exclusion stays)
    std::int32_t light = -1; // a RectLight index (an Emitter), or -1 for the environment's disc
    // The instances photons are aimed at (their bounding sphere): the glass and mirrors that cast.
    std::uint32_t casterInstanceFirst = 0, casterInstanceCount = 0;
};

// The bounding sphere of a frame's caster instances in world space: each mesh's own bounding sphere, or each
// static sphere of a sphere instance, through its instance's transform.
struct CasterBounds {
    Vec3 center;
    float radius;
};
CasterBounds casterBounds(const SceneStatic& scene, const SceneFrame& frame);

// The frame's caustics against its scene: at most kMaxCausticReceivers receivers of a resolution in
// [1, maxResolution] with a non-degenerate uv map and a chroma radius of at most kMaxChromaRadius; the light
// an Emitter rect light of the frame, or (-1) an environment with a disc of light; a non-empty range of caster instances, none of them the particle field;
// every material flagged kMaterialCausticReceiver (the frame's table, or the static one) naming a receiver
// and opaque with a Lambert lobe (transmission <= 0.5, metallic < 1). Throws std::invalid_argument, or
// std::length_error for too many receivers or a resolution above maxResolution. Without
// SceneFrame::caustics there is nothing to check.
void checkCausticFrame(const SceneStatic& scene, const SceneFrame& frame, std::uint32_t maxResolution);

// An export's caustics (see kExportMapFactor): every receiver at kExportMapFactor times its resolution and
// chroma radius, where that stays within maxResolution; the rest unchanged.
CausticFrame exportCaustics(const CausticFrame& frame, std::uint32_t maxResolution);

// The resources of a traced frame the photon pass reads, as the path tracer binds them: the frame's TLAS,
// this slot's instance records, lights and environment, the material table, the scene's geometry and
// spheres, the particle field (or the tracer's dummies). PathTracer::trace() hands them to
// TraceExtras::photons after the TLAS build and before its trace dispatches.
struct PhotonScene {
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    const gfx::Buffer* instances = nullptr;
    const gfx::Buffer* materials = nullptr;
    const gfx::Buffer* vertices = nullptr;
    const gfx::Buffer* indices = nullptr;
    const gfx::Buffer* meshRecords = nullptr;
    const gfx::Buffer* staticSpheres = nullptr;
    const gfx::Buffer* particleSpheres = nullptr;
    const gfx::Buffer* lights = nullptr;
    const gfx::Buffer* environment = nullptr;
};

// The caustic maps (one per ModernStage, sized to the largest receiver resolution prepared): the R32_UINT x3
// fixed-point accumulator (kMaxCausticReceivers x maxResolution^2 texels), the RGBA16F irradiance array
// (binding EVO_BINDING_CAUSTIC_MAPS) and, per ring slot, the receiver table (binding
// EVO_BINDING_CAUSTIC_RECEIVERS: the receivers, then one source record the raygen's exclusion reads, see
// shaders/rt/caustic_common.glsl).
//
// Per frame: prepare() (CPU: the checks, the caster bounds, this slot's table, the photon constants), then
// the trace with TraceExtras {causticMaps = &maps(), causticReceivers = &receiverTable(), photons =
// record()}. record() clears the accumulator, shoots the photons with ray queries against the frame's TLAS
// and resolves the maps, after the previous frame's reads of both.
//
// Lifetime: the maps belong to the ring they were made with and bind descriptor sets from it; the
// destructor waits for the ring's frames in flight.
class CausticMaps {
public:
    static constexpr std::uint32_t kPhotonsPerRow = 1u << 16; // photons.comp: photon = y * this + x

    // Throws std::invalid_argument for a resolution of 0.
    CausticMaps(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t maxResolution);
    ~CausticMaps();
    CausticMaps(const CausticMaps&) = delete;
    CausticMaps& operator=(const CausticMaps&) = delete;

    // Grows the maps to `maxResolution` (never shrinks; setup: waits for the queue when it grows).
    void reserve(std::uint32_t maxResolution);

    // Checks the frame (checkCausticFrame), writes this ring slot's receiver table and sets the photon pass
    // up for `photons` photons and `seed`. Between ring.begin() and the trace that records this frame's maps;
    // throws before writing anything (std::logic_error outside a frame, std::invalid_argument for a frame
    // without caustics).
    void prepare(const SceneStatic& scene, const SceneFrame& frame, std::uint32_t photons,
                 std::uint32_t seed);
    // Records the clear, the photons and the resolve (TraceExtras::photons calls it with the trace's
    // resources). Throws std::logic_error without a prepare() in this frame.
    void record(VkCommandBuffer cmd, const PhotonScene& scene);

    gfx::Image& maps() { return maps_; }
    // This slot's receiver table.
    const gfx::Buffer& receiverTable() const;
    std::uint32_t maxResolution() const { return resolution_; }
    // The device memory held, as allocated (VMA).
    VkDeviceSize bytes() const;

    // A test hook: photon i is shot as photon (i * multiplier + offset) mod count; with an odd multiplier and
    // a power-of-two count that is a permutation, and the maps must not change.
    void setPhotonOrder(std::uint32_t multiplier, std::uint32_t offset);

private:
    void createMaps();

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t resolution_;
    gfx::ComputePipeline photons_, resolve_;
    gfx::Buffer accumulator_;
    gfx::Image maps_;
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> tables_;
    std::uint32_t orderMultiplier_ = 1, orderOffset_ = 0;
    bool prepared_ = false; // prepare() ran in the frame being recorded
    std::uint32_t photonCount_ = 0, receiverCount_ = 0, seed_ = 0;
    float unitsPerFlux_ = 0, sourceDistance_ = 0;
    std::array<float, kMaxCausticReceivers> irradianceScale_{}; // flux unit / texel area per receiver
    std::array<std::uint32_t, kMaxCausticReceivers> receiverResolution_{};
    std::array<std::uint32_t, kMaxCausticReceivers> receiverChroma_{};
    gfx::FrameRing::Subscription ringBegin_;
};

} // namespace evo::modern
