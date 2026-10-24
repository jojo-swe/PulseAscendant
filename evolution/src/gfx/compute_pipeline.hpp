#pragma once

#include "gfx/context.hpp"
#include "gfx/descriptors.hpp"
#include "gfx/frame_ring.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace evo::gfx {

// A compute shader with its descriptor set layout (set 0, the given bindings; see Binding) and push constant
// range. Every bind() writes a fresh descriptor set, so a pass can bind different resources each time; the
// sets come from the pipeline's own DescriptorAllocator (pools kept per FrameRing slot, reset when the ring
// reuses the slot, and added when a frame needs more than the slot's pools hold).
//
// Lifetime: the VkPipeline and the descriptor sets a frame binds are used by the GPU until that frame's
// submit completes, so a pipeline must outlive every submit that bound it. Declare the FrameRing after its
// pipelines (the ring's destructor waits for its submits), or wait for the ring (waitAll) or the device
// before the pipeline goes. The first bind() that writes a set ties the pipeline to that ring; binding it
// with a second live ring throws, and once the ring is gone the pipeline attaches to the next ring it is
// bound with. A pipeline with no bindings writes no sets and is not tied to a ring.
class ComputePipeline {
public:
    // The SPIR-V's entry point is "main". Throws std::runtime_error naming the file when it cannot load, and
    // std::invalid_argument when pushConstantBytes is not a multiple of 4.
    ComputePipeline(Context& ctx, const std::filesystem::path& spv, std::span<const Binding> bindings,
                    std::uint32_t pushConstantBytes);
    ~ComputePipeline();
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    // Binds the pipeline and a descriptor set written with `writes` (see writeDescriptors, which throws
    // std::invalid_argument for writes that do not match the bindings; nothing is recorded then). Between
    // ring.begin() and ring.submit() only. A pipeline with no bindings takes no writes and binds the
    // pipeline alone: it allocates no descriptor set and creates no descriptor pool.
    void bind(VkCommandBuffer cmd, FrameRing& ring, std::span<const DescriptorWrite> writes);
    // The same with the resources in binding-list order: `images` for the image and sampler bindings and
    // `buffers` for the buffer bindings, `count` of them for an array binding. Throws std::invalid_argument
    // when the counts do not match, and for a pipeline with an acceleration structure binding (use writes).
    void bind(VkCommandBuffer cmd, FrameRing& ring, std::span<const VkDescriptorImageInfo> images,
              std::span<const VkDescriptorBufferInfo> buffers);
    // Throws std::invalid_argument for more bytes than the pipeline's push constant range, or a size that is
    // not a multiple of 4. Zero bytes record nothing.
    void push(VkCommandBuffer cmd, const void* data, std::uint32_t bytes);
    // ceil(width / localX) x ceil(height / localY) workgroups; the shader must skip the pixels outside.
    void dispatch(VkCommandBuffer cmd, std::uint32_t width, std::uint32_t height, std::uint32_t localX = 8,
                  std::uint32_t localY = 8);
    // How many dispatch() calls this pipeline has recorded; for tests that pin that a pass was skipped.
    std::uint64_t dispatchCount() const { return dispatches_; }

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }
    const DescriptorLayout& descriptorLayout() const { return setLayout_; }
    // Descriptor pools across all slots; for tests that pin that the pools are reused rather than added.
    std::size_t descriptorPoolCount() const { return descriptors_.poolCount(); }

private:
    void destroy() noexcept;

    Context& ctx_;
    std::uint32_t pushConstantBytes_ = 0;
    DescriptorLayout setLayout_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    DescriptorAllocator descriptors_;
    std::uint64_t dispatches_ = 0;
};

} // namespace evo::gfx
