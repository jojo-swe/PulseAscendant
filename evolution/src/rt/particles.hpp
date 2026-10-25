#pragma once

// The particle field (step 5, Task 8): up to millions of analytic spheres made every frame by a stateless
// compute shader and traced through a per-frame AABB BLAS. A particle is a pure function of (id, seconds,
// params): the scene's shaders/modern/particles_<scene>.comp defines
//     SphereRecord particle(uint id, float seconds, vec4 params[4]);
// and includes shaders/modern/particles_common.glsl, whose main() (local_size_x 256) writes each particle's
// SphereRecord (prevCenter from the same function at the previous time, material from the frame) and AABB.
// Nothing carries over from one frame to the next, so seeking needs nothing (Review Focus 1).

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <span>

namespace evo::rt {

// Per ring slot: a SphereRecord buffer (the binding EVO_BINDING_PARTICLE_SPHERES of that slot's frame), an
// AABB buffer, and a BLAS with its scratch, all sized for `capacity` particles, so a frame in flight never
// shares one with the frame being recorded. One field serves every scene (ModernStage keeps one, sized to
// the largest prepared scene); each scene's shader is a pipeline of its own (setShader).
//
// Lifetime: a field belongs to the ring it was made with; its destructor waits for the ring's submits while
// the ring lives (a frame in flight may still build or trace its BLASes), like a Tlas's. Its pipelines bind
// descriptor sets from that ring, so it must also go before the ring is reused by anything else.
class ParticleField {
public:
    static constexpr std::uint32_t kParams = 16;     // params: vec4 params[4] in the shader
    static constexpr std::uint32_t kLocalSize = 256; // the shader's local_size_x

    // Builds the shader's pipeline (setShader) and the slots' buffers and BLASes (setup). Throws
    // std::invalid_argument for
    // capacity 0, std::runtime_error for a shader that cannot load.
    ParticleField(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t capacity,
                  const std::filesystem::path& shader);
    ~ParticleField();
    ParticleField(const ParticleField&) = delete;
    ParticleField& operator=(const ParticleField&) = delete;

    // Grows every slot to `capacity` particles (never shrinks); waits for the queue to idle first when it
    // grows (setup: before a frame, or before record() in the frame being recorded).
    void reserve(std::uint32_t capacity);
    // The shader the next record() runs, its pipeline built on first use (setup work: a pipeline creation). A
    // relative path names a file in gfx::shaderDir() (a scene's "modern_particles_<scene>.comp.spv").
    void setShader(const std::filesystem::path& shader);

    // Records into the frame being recorded on the ring, for this slot: a barrier (the previous traces,
    // copies and builds that read this slot's buffers and BLAS -> the compute writes and the build), the
    // shader over `count` particles at `seconds` (prevCenter at `previousSeconds`; every particle's material
    // is `material`), a barrier (compute writes -> the AS build's input reads, the ray tracing and compute
    // shaders' storage reads), the BLAS build (PREFER_FAST_BUILD, a full rebuild: no state from another
    // frame), and a barrier (build writes -> TLAS builds and traces). It may run more than once in a frame
    // (shutter slices, Task 9): each later run waits for the reads of the one before. Throws, before
    // recording anything, std::length_error for count > capacity and std::logic_error outside
    // ring.begin()/submit().
    void record(VkCommandBuffer cmd, std::uint32_t count, double seconds, double previousSeconds,
                std::span<const float, kParams> params, std::uint32_t material = 0);

    // This slot's BLAS and SphereRecord buffer (the frame being recorded, or the next).
    VkDeviceAddress blasAddress() const;
    const gfx::Buffer& records() const;
    // The particles this slot's last record() made.
    std::uint32_t count() const;
    std::uint32_t capacity() const { return capacity_; }
    // The device memory held, as allocated (VMA): every slot's buffers, BLAS storage and scratch.
    VkDeviceSize bytes() const;

private:
    struct Slot {
        gfx::Buffer records, aabbs, storage, scratch;
        VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
        VkDeviceAddress address = 0;
        std::uint32_t count = 0;
    };
    using Slots = std::array<Slot, gfx::FrameRing::kFramesInFlight>;
    Slots createSlots(std::uint32_t capacity) const;
    void destroySlots(Slots& slots) noexcept;

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t capacity_ = 0;
    std::map<std::filesystem::path, std::unique_ptr<gfx::ComputePipeline>> pipelines_;
    gfx::ComputePipeline* current_ = nullptr;
    Slots slots_;
    gfx::FrameRing::Subscription ringAlive_; // active while ring_ lives
};

} // namespace evo::rt
