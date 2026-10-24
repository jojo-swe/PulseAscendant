#pragma once

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace evo::gfx {

// One descriptor in set 0 of a compute shader: STORAGE_IMAGE, SAMPLED_IMAGE, SAMPLER, STORAGE_BUFFER or
// UNIFORM_BUFFER (or COMBINED_IMAGE_SAMPLER), one descriptor per binding.
struct Binding {
    std::uint32_t binding;
    VkDescriptorType type;
};

// A compute shader with its descriptor set layout (set 0, the given bindings) and push constant range. Every
// bind() writes a fresh descriptor set, so a pass can bind different resources each time; the sets come from
// pools kept per FrameRing slot that are reset when the ring reuses the slot, and a pool is added when a
// frame needs more sets than the slot's pools hold.
//
// The first bind() ties the pipeline to that ring; destroy the pipeline before the ring, or let the ring go
// first (the pipeline then attaches to the next ring it is bound with).
class ComputePipeline {
public:
    // The SPIR-V's entry point is "main". Throws std::runtime_error naming the file when it cannot load.
    ComputePipeline(Context& ctx, const std::filesystem::path& spv, std::span<const Binding> bindings,
                    std::uint32_t pushConstantBytes);
    ~ComputePipeline();
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    // Binds the pipeline and a descriptor set written with the given resources: `images` for the image and
    // sampler bindings and `buffers` for the buffer bindings, each in binding-list order. Between
    // ring.begin() and ring.submit() only. Throws std::invalid_argument when the counts do not match.
    void bind(VkCommandBuffer cmd, FrameRing& ring, std::span<const VkDescriptorImageInfo> images,
              std::span<const VkDescriptorBufferInfo> buffers);
    // Throws std::invalid_argument for more bytes than the pipeline's push constant range.
    void push(VkCommandBuffer cmd, const void* data, std::uint32_t bytes);
    // ceil(width / localX) x ceil(height / localY) workgroups; the shader must skip the pixels outside.
    void dispatch(VkCommandBuffer cmd, std::uint32_t width, std::uint32_t height, std::uint32_t localX = 8,
                  std::uint32_t localY = 8);

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }
    // Descriptor pools across all slots; for tests that pin that the pools are reused rather than added.
    std::size_t descriptorPoolCount() const;

private:
    struct SlotPools {
        std::vector<VkDescriptorPool> pools;
        std::size_t current = 0;         // pools before this one are full this frame
        std::uint32_t usedInCurrent = 0; // counted, because some drivers allocate past maxSets
    };

    void attach(FrameRing& ring);
    void resetSlot(std::uint32_t slot);
    VkDescriptorSet allocateSet(std::uint32_t slot);
    VkDescriptorPool createPool();
    void destroy() noexcept;

    Context& ctx_;
    std::vector<Binding> bindings_;
    std::uint32_t pushConstantBytes_ = 0;
    std::vector<VkDescriptorPoolSize> poolSizes_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::array<SlotPools, FrameRing::kFramesInFlight> slots_{};
    FrameRing::Subscription subscription_;
};

} // namespace evo::gfx
