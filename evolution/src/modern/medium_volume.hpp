#pragma once

// The grid medium's GPU side (the medium itself is described in modern/medium.hpp).

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "modern/medium.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>

namespace evo::modern {

// One per ModernStage, sized to the largest prepared scene (like the particle field): a 3D RGBA16F density +
// emission image (storage and sampled: EVO_BINDING_DENSITY) and a 3D R16F majorant image (one texel per
// kMajorantCell^3 cell: EVO_BINDING_MAJORANT), baked every frame by record(), a pure function of (the desc,
// seconds): a seek needs nothing, and two bakes give the same bytes. The images are shared by the frames in
// flight: the next frame's bake waits for this frame's trace through the images' barriers (one queue).
//
// Lifetime: like a ParticleField, it binds descriptor sets from its ring and must go before the ring is
// reused by anything else; its destructor waits for the ring's submits while the ring lives.
class MediumVolume {
public:
    static constexpr std::uint32_t kParams = 16;

    // Builds the bake pipeline of `shader` (setShader), the majorant pipeline
    // (modern_medium_majorant.comp.spv in gfx::shaderDir()) and the images for maxSize voxels (setup). Throws
    // std::invalid_argument for a size of 0 or above kMaxMediumGrid on an axis, std::runtime_error for a
    // shader that cannot load.
    MediumVolume(gfx::Context& ctx, gfx::FrameRing& ring, std::array<std::uint32_t, 3> maxSize,
                 const std::filesystem::path& shader);
    ~MediumVolume();
    MediumVolume(const MediumVolume&) = delete;
    MediumVolume& operator=(const MediumVolume&) = delete;

    // Grows the images to hold maxSize (per axis the larger of it and the current capacity; never shrinks);
    // waits for the queue to idle first when anything grows (setup: before a frame, or before record() in the
    // frame being recorded).
    void reserve(std::array<std::uint32_t, 3> maxSize);
    // The bake shader the next record() runs, its pipeline built on first use (setup work). A relative path
    // names a file in gfx::shaderDir() (a scene's "modern_medium_<scene>.comp.spv").
    void setShader(const std::filesystem::path& shader);

    // Records into the frame being recorded on the ring: the bake of desc.size voxels at `seconds` with
    // desc.params into the density image (from voxel (0, 0, 0); the rest of the image is left as it was and
    // never sampled), the majorant of each of its cells, and the barriers between them. The density is left
    // in GENERAL after compute reads and the majorant in TRANSFER_DST after a copy; PathTracer::trace()
    // records the barriers to its sampled reads. Throws, before recording anything, std::length_error for a
    // size beyond the capacity, std::invalid_argument for a size of 0 and std::logic_error outside
    // ring.begin()/submit().
    void record(VkCommandBuffer cmd, const GridMediumDesc& desc, double seconds);

    gfx::Image& density() { return density_; }
    gfx::Image& majorant() { return majorant_; }
    std::array<std::uint32_t, 3> capacity() const { return capacity_; }
    // How many bakes record() has recorded (a test hook).
    std::uint64_t bakes() const { return bakes_; }
    // The device memory held, as allocated (VMA): both images and the majorant staging buffer.
    VkDeviceSize bytes() const;

private:
    struct Resources {
        gfx::Image density, majorant;
        gfx::Buffer majorantWords; // the majorant as packed halves, copied into the majorant image
    };
    Resources createResources(std::array<std::uint32_t, 3> size) const;

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::array<std::uint32_t, 3> capacity_{};
    std::map<std::filesystem::path, std::unique_ptr<gfx::ComputePipeline>> pipelines_;
    gfx::ComputePipeline* current_ = nullptr;
    std::unique_ptr<gfx::ComputePipeline> majorantPipeline_;
    gfx::Image density_, majorant_;
    gfx::Buffer majorantWords_;
    std::uint64_t bakes_ = 0;
    gfx::FrameRing::Subscription ringAlive_; // active while ring_ lives
};

} // namespace evo::modern
