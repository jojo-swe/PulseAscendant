#pragma once

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "rt/geometry.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace evo::rt {

static_assert(sizeof(VkAccelerationStructureInstanceKHR) == 64);

// The input of one bottom-level acceleration structure: one geometry (triangles or AABBs, whose buffers live
// until the BlasSet's constructor returns) and its primitive count.
struct BlasInput {
    VkAccelerationStructureGeometryKHR geometry;
    std::uint32_t primitiveCount;
};

// One bottom-level acceleration structure per mesh of a GeometryStore (or per BlasInput), all built in one
// immediate submit at setup, opaque geometry, PREFER_FAST_TRACE | ALLOW_COMPACTION. With `compact` the
// compacted sizes are queried, compacted copies made in a second immediate submit, and only once that copy
// completed are the originals and the scratch buffer destroyed. The store may go once this is built as far as
// the BLASes are concerned, but shaders that fetch vertices read its buffers.
//
// Lifetime, for scene owners: declare the GeometryStore and the BlasSet before the FrameRing and the Tlas
// (members are destroyed in reverse order), so they outlive both: the ring's destructor and the Tlas's wait
// for the frames in flight, and only then may the BLASes and the vertex and index buffers those frames
// traced go.
class BlasSet {
public:
    // noDuplicateAnyHit: every mesh's geometry also NO_DUPLICATE_ANY_HIT_INVOCATION (an any-hit that sums or
    // multiplies what it meets must see each triangle once; the traces' results are otherwise the same).
    BlasSet(gfx::Context& ctx, const GeometryStore& store, bool compact = true,
            bool noDuplicateAnyHit = false);
    // One BLAS per input, in order (address(i) is input i's): the analytic spheres' AABBs (rt/spheres.hpp).
    // Throws std::invalid_argument for no inputs.
    BlasSet(gfx::Context& ctx, std::span<const BlasInput> inputs, bool compact = true);
    ~BlasSet();
    BlasSet(const BlasSet&) = delete;
    BlasSet& operator=(const BlasSet&) = delete;

    // The device address of mesh `mesh`'s BLAS, for a TLAS instance. Throws std::out_of_range.
    VkDeviceAddress address(std::uint32_t mesh) const;
    std::uint32_t size() const { return static_cast<std::uint32_t>(blases_.size()); }
    // The bytes of the BLASes as built, and as kept (compactedBytes == builtBytes without compaction).
    VkDeviceSize builtBytes() const { return builtBytes_; }
    VkDeviceSize compactedBytes() const { return compactedBytes_; }
    // The device memory the kept BLASes' storage buffers hold, as allocated (VMA).
    VkDeviceSize bytes() const;

private:
    struct Blas {
        gfx::Buffer storage;
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        VkDeviceAddress address = 0;
    };
    void build(std::span<const BlasInput> inputs, bool compact);
    void destroy() noexcept;

    gfx::Context& ctx_;
    std::vector<Blas> blases_;
    VkDeviceSize builtBytes_ = 0;
    VkDeviceSize compactedBytes_ = 0;
};

// One TLAS instance: mesh `mesh` of a BlasSet placed by `transform`, or, when `blas` is not 0, the BLAS at
// that device address (an analytic sphere BLAS, rt/spheres.hpp and rt/particles.hpp; `mesh` is then
// ignored). customIndex (gl_InstanceCustomIndexEXT) and hitGroup (the SBT record offset) have 24 bits.
// notOpaque: the instance is not opaque (FORCE_NO_OPAQUE) to a ray traced without the opaque flag, which then
// runs its hit group's any-hit (every ray traced with the opaque flag still sees it opaque).
struct Instance {
    std::uint32_t mesh;
    Mat34 transform;
    std::uint32_t customIndex;
    std::uint32_t hitGroup;
    std::uint8_t mask = 0xFF;
    VkDeviceAddress blas = 0;
    bool notOpaque = false;
};

// A top-level acceleration structure rebuilt every frame. Owns one TLAS and one scratch buffer sized for
// maxInstances, and one host-visible instance buffer per ring slot, so the CPU never writes instances a frame
// in flight still reads. Every instance has FACING_CULL_DISABLE; its facing is Vulkan's default.
//
// build() records into the frame being recorded on the ring: it writes this slot's instance buffer (the
// ring's begin() already waited for the slot's previous frame), then a barrier (RAY_TRACING_SHADER |
// COMPUTE_SHADER acceleration structure reads, ACCELERATION_STRUCTURE_BUILD writes -> BUILD reads and
// writes) so the previous frame's traces and builds of the shared TLAS and scratch finish first, the build,
// and a barrier (BUILD writes -> RAY_TRACING_SHADER | COMPUTE_SHADER acceleration structure reads) for this
// frame's traces.
//
// Builds per frame (maxBuildsPerFrame, 1 by default): a build beyond them throws std::logic_error. Each
// build of a frame gets its own region of the slot's instance buffer (the CPU writes them all while the
// frame is recorded, before the GPU runs the first build), and rebuilds the same TLAS after the traces
// recorded since the build before it (its barrier makes the previous traces and builds finish first): the
// export shutter traces one TLAS per time slice this way. A second build with only one allowed would
// overwrite the TLAS that the first build's traces in this command buffer read.
//
// Lifetime: a Tlas belongs to the ring it was made with. Its destructor waits for every submit of that ring
// while the ring lives (a frame in flight may still trace the TLAS); that is a full stall of the ring, fine
// at shutdown or between scenes outside the show, not for scenes torn down mid-show. A ring destroyed first
// has waited itself. The BlasSet and GeometryStore a build used must outlive the frame: declare them before
// the ring and the Tlas.
//
// The barriers assume one queue: the ring's frames and every trace of the TLAS are submitted to the
// Context's single queue in order. A second queue would need semaphores between its submits and these.
class Tlas {
public:
    // Throws std::invalid_argument for maxInstances 0 or maxBuildsPerFrame 0.
    Tlas(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t maxInstances,
         std::uint32_t maxBuildsPerFrame = 1);
    ~Tlas();
    Tlas(const Tlas&) = delete;
    Tlas& operator=(const Tlas&) = delete;

    // Throws, before recording anything, std::logic_error outside ring.begin()/submit() or for a build
    // beyond maxBuildsPerFrame in the frame (a refused build does not count), std::length_error
    // for more than maxInstances instances, std::out_of_range for a mesh not in `blases` (an instance without
    // its own `blas`) and
    // std::invalid_argument for a customIndex or hitGroup over 24 bits.
    void build(VkCommandBuffer cmd, const BlasSet& blases, std::span<const Instance> instances);
    VkAccelerationStructureKHR handle() const { return tlas_; }
    std::uint32_t maxInstances() const { return maxInstances_; }
    std::uint32_t maxBuildsPerFrame() const { return maxBuilds_; }
    // The device memory of the TLAS's storage, its scratch and the per-slot instance buffers, as allocated.
    VkDeviceSize bytes() const;

private:
    void destroy() noexcept;

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t maxInstances_ = 0, maxBuilds_ = 1;
    gfx::Buffer storage_;
    gfx::Buffer scratch_;
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> instanceBuffers_;
    VkAccelerationStructureKHR tlas_ = VK_NULL_HANDLE;
    std::uint32_t builtThisFrame_ = 0;       // builds so far this frame; reset by the ring's begin()
    gfx::FrameRing::Subscription ringAlive_; // active while ring_ lives
};

} // namespace evo::rt
