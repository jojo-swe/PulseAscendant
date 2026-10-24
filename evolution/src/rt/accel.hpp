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

// One bottom-level acceleration structure per mesh of a GeometryStore, all built in one immediate submit at
// setup, opaque geometry, PREFER_FAST_TRACE | ALLOW_COMPACTION. With `compact` the compacted sizes are
// queried, compacted copies made in a second immediate submit, and only once that copy completed are the
// originals and the scratch buffer destroyed. The store may go once this is built; the set must outlive
// every submit that traces a TLAS referencing it.
class BlasSet {
public:
    BlasSet(gfx::Context& ctx, const GeometryStore& store, bool compact = true);
    ~BlasSet();
    BlasSet(const BlasSet&) = delete;
    BlasSet& operator=(const BlasSet&) = delete;

    // The device address of mesh `mesh`'s BLAS, for a TLAS instance. Throws std::out_of_range.
    VkDeviceAddress address(std::uint32_t mesh) const;
    std::uint32_t size() const { return static_cast<std::uint32_t>(blases_.size()); }
    // The bytes of the BLASes as built, and as kept (compactedBytes == builtBytes without compaction).
    VkDeviceSize builtBytes() const { return builtBytes_; }
    VkDeviceSize compactedBytes() const { return compactedBytes_; }

private:
    struct Blas {
        gfx::Buffer storage;
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        VkDeviceAddress address = 0;
    };
    void destroy() noexcept;

    gfx::Context& ctx_;
    std::vector<Blas> blases_;
    VkDeviceSize builtBytes_ = 0;
    VkDeviceSize compactedBytes_ = 0;
};

// One TLAS instance: mesh `mesh` of a BlasSet placed by `transform`. customIndex (gl_InstanceCustomIndexEXT)
// and hitGroup (the SBT record offset) have 24 bits.
struct Instance {
    std::uint32_t mesh;
    Mat34 transform;
    std::uint32_t customIndex;
    std::uint32_t hitGroup;
    std::uint8_t mask = 0xFF;
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
// Lifetime: a Tlas belongs to the ring it was made with. Its destructor waits for every submit of that ring
// while the ring lives (a frame in flight may still trace the TLAS); a ring destroyed first has waited
// itself. The BlasSet a build used must outlive the frame.
class Tlas {
public:
    // Throws std::invalid_argument for maxInstances 0.
    Tlas(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t maxInstances);
    ~Tlas();
    Tlas(const Tlas&) = delete;
    Tlas& operator=(const Tlas&) = delete;

    // Throws, before recording anything, std::logic_error outside ring.begin()/submit(), std::length_error
    // for more than maxInstances instances, std::out_of_range for a mesh not in `blases` and
    // std::invalid_argument for a customIndex or hitGroup over 24 bits.
    void build(VkCommandBuffer cmd, const BlasSet& blases, std::span<const Instance> instances);
    VkAccelerationStructureKHR handle() const { return tlas_; }
    std::uint32_t maxInstances() const { return maxInstances_; }

private:
    void destroy() noexcept;

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t maxInstances_ = 0;
    gfx::Buffer storage_;
    gfx::Buffer scratch_;
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> instanceBuffers_;
    VkAccelerationStructureKHR tlas_ = VK_NULL_HANDLE;
    gfx::FrameRing::Subscription ringAlive_; // active while ring_ lives
};

} // namespace evo::rt
